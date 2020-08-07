// Single instruction, multiple data instructions.

#define NEED_STRUCT

#include "cpu/simd.h"
#include "cpu/cpu.h"
#include "cpu/fpu.h"
#include "cpu/instrument.h"
#include "io.h"
#include <string.h>
#define EXCEPTION_HANDLER return 1

///////////////////////////////////////////////////////////////////////////////
// Floating point routines
///////////////////////////////////////////////////////////////////////////////
#include "softfloat/softfloat-compare.h"
#include "softfloat/softfloat.h"
static float_status_t status;

// Raise an exception if SSE is not enabled
int cpu_sse_exception(void)
{
    // https://xem.github.io/minix86/manual/intel-x86-and-64-manual-vol3/o_fe12b1e2a880e0ce-457.html
    if ((cpu.cr[4] & CR4_OSFXSR) == 0)
        EXCEPTION_UD();
    if (cpu.cr[0] & CR0_EM)
        EXCEPTION_UD();
    if (cpu.cr[0] & CR0_TS)
        EXCEPTION_NM();
    return 0;
}
int cpu_mmx_check(void)
{
    if (cpu.cr[0] & CR0_EM)
        EXCEPTION_UD();
    if (cpu.cr[0] & CR0_TS)
        EXCEPTION_NM();

    if (fpu_fwait())
        EXCEPTION_HANDLER;

    // MMX transitions clear the tag word and reset the stack
    fpu.ftop = 0;
    fpu.tag_word = 0;
    return 0;
}
#ifdef INSTRUMENT
#define INSTRUMENT_MMX cpu_instrument_pre_fpu()
#else
#define INSTRUMENT_MMX NOP()
#endif
#define CHECK_SSE            \
    if (cpu_sse_exception()) \
    return 1
#define CHECK_MMX        \
    INSTRUMENT_MMX;      \
    if (cpu_mmx_check()) \
    return 1

void cpu_update_mxcsr(void)
{
    // Regenerates the data inside of "status"
    status.float_exception_flags = 0;
    status.float_nan_handling_mode = float_first_operand_nan;
    status.float_rounding_mode = cpu.mxcsr >> 13 & 3;
    status.flush_underflow_to_zero = (cpu.mxcsr >> 15) & (cpu.mxcsr >> 11) & 1;
    status.float_exception_masks = cpu.mxcsr >> 7 & 63;
    status.float_suppress_exception = 0;
    status.denormals_are_zeros = cpu.mxcsr >> 6 & 1;
}
int cpu_sse_handle_exceptions(void)
{
    // Check if any of the exceptions are masked.

    int flags = status.float_exception_flags, unmasked = flags & ~status.float_exception_masks & 0x3F;
    status.float_exception_flags = 0;
    if (unmasked & 7)
        flags &= 7;
    cpu.mxcsr |= flags;
    if (unmasked) {
        // https://wiki.osdev.org/Exceptions#SIMD_Floating-Point_Exception
        if (cpu.cr[4] & CR4_OSXMMEXCPT)
            EXCEPTION(19);
        else
            EXCEPTION_UD(); // According to Bochs
    }
    return 0;
}

#define MM32(n) fpu.mm[n].reg.r32[0]

#define FAST_BRANCHLESS_MASK(addr, i) (addr & ((i << 12 & 65536) - 1))
static inline uint32_t cpu_get_linaddr(uint32_t i, struct decoded_instruction* j)
{
    uint32_t addr = cpu.reg32[I_BASE(i)];
    addr += cpu.reg32[I_INDEX(i)] << (I_SCALE(i));
    addr += j->disp32;
    return FAST_BRANCHLESS_MASK(addr, i) + cpu.seg_base[I_SEG_BASE(i)];
}

///////////////////////////////////////////////////////////////////////////////
// Operand access functions
///////////////////////////////////////////////////////////////////////////////

// A temporary "data cache" that holds read data/write data to be flushed out to regular RAM.
// Note that this isn't much of a cache since it only holds 16 bytes and is not preserved across instruction boundaries
union {
    uint32_t d32;
    uint32_t d64[2];
    uint32_t d128[4];
} temp;
static void* result_ptr;
static int write_back, write_back_dwords, write_back_linaddr;

// Flush data in temp.d128 back out to memory. This is required if write_back == 1
static int write_back_handler(void)
{
    for (int i = 0; i < write_back_dwords; i++)
        cpu_write32(write_back_linaddr + (i * 4), temp.d128[i], cpu.tlb_shift_write);
    return 0;
}
#define WRITE_BACK()                        \
    if (write_back && write_back_handler()) \
    return 1

static int get_read_ptr(uint32_t flags, struct decoded_instruction* i, int dwords, int unaligned_exception)
{
    uint32_t linaddr = cpu_get_linaddr(flags, i);
    if (linaddr & ((dwords << 2) - 1)) {
        if (unaligned_exception)
            EXCEPTION_GP(0);
        for (int i = 0, j = 0; i < dwords; i++, j += 4)
            cpu_read32(linaddr + j, temp.d128[i], cpu.tlb_shift_read);
        result_ptr = temp.d128;
        write_back_dwords = dwords;
        write_back_linaddr = linaddr;
        return 0;
    }
    uint8_t tag = cpu.tlb_tags[linaddr >> 12] >> cpu.tlb_shift_read;
    if (tag & 2) {
        if (cpu_mmu_translate(linaddr, cpu.tlb_shift_read))
            return 1;
    }

    uint32_t* host_ptr = cpu.tlb[linaddr >> 12] + linaddr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    if ((phys >= 0xA0000 && phys < 0xC0000) || (phys >= cpu.memory_size)) {
        for (int i = 0, j = 0; i < dwords; i++, j += 4)
            temp.d128[i] = io_handle_mmio_read(phys + j, 2);
        result_ptr = temp.d128;
        write_back_dwords = dwords;
        write_back_linaddr = linaddr;
        return 0;
    }
    result_ptr = host_ptr;
    return 0;
}
static int get_write_ptr(uint32_t flags, struct decoded_instruction* i, int dwords, int unaligned_exception)
{

    uint32_t linaddr = cpu_get_linaddr(flags, i);
    if (linaddr & ((dwords << 2) - 1)) {
        if (unaligned_exception)
            EXCEPTION_GP(0);
        result_ptr = temp.d128;
        write_back = 1;
        write_back_dwords = dwords;
        write_back_linaddr = linaddr;
        return 0;
    }
    uint8_t tag = cpu.tlb_tags[linaddr >> 12] >> cpu.tlb_shift_write;
    if (tag & 2) {
        if (cpu_mmu_translate(linaddr, cpu.tlb_shift_write))
            return 1;
    }

    uint32_t* host_ptr = cpu.tlb[linaddr >> 12] + linaddr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    if ((phys >= 0xA0000 && phys < 0xC0000) || (phys >= cpu.memory_size)) {
        write_back = 1;
        result_ptr = temp.d128;
        write_back_dwords = dwords;
        write_back_linaddr = linaddr;
        return 0;
    }
    write_back = 0;
    result_ptr = host_ptr;
    return 0;
}
static int get_sse_read_ptr(uint32_t flags, struct decoded_instruction* i, int dwords, int unaligned_exception)
{
    if (I_OP2(flags)) {
        result_ptr = &XMM32(I_RM(flags));
        return 0;
    } else
        return get_read_ptr(flags, i, dwords, unaligned_exception);
}
static int get_sse_write_ptr(uint32_t flags, struct decoded_instruction* i, int dwords, int unaligned_exception)
{
    if (I_OP2(flags)) {
        result_ptr = &XMM32(I_RM(flags));
        write_back = 0;
        return 0;
    } else
        return get_write_ptr(flags, i, dwords, unaligned_exception);
}
static int get_mmx_read_ptr(uint32_t flags, struct decoded_instruction* i, int dwords)
{
    if (I_OP2(flags)) {
        result_ptr = &MM32(I_RM(flags));
        return 0;
    } else
        return get_read_ptr(flags, i, dwords, 0);
}
static int get_mmx_write_ptr(uint32_t flags, struct decoded_instruction* i, int dwords)
{
    if (I_OP2(flags)) {
        int reg = I_RM(flags);
        result_ptr = &MM32(reg);
        fpu.mm[reg].dummy = 0xFFFF;
        write_back = 0;
        return 0;
    } else
        return get_write_ptr(flags, i, dwords, 0);
}
static int get_reg_read_ptr(uint32_t flags, struct decoded_instruction* i)
{
    if (I_OP2(flags)) {
        result_ptr = &cpu.reg32[I_RM(flags)];
        return 0;
    } else
        return get_read_ptr(flags, i, 1, 0);
}
static int get_reg_write_ptr(uint32_t flags, struct decoded_instruction* i)
{
    if (I_OP2(flags)) {
        result_ptr = &cpu.reg32[I_RM(flags)];
        write_back = 0;
        return 0;
    } else
        return get_write_ptr(flags, i, 1, 0);
}
static void* get_mmx_reg_dest(int x)
{
    fpu.mm[x].dummy = 0xFFFF; // STn.exponent is set to all ones
    return &fpu.mm[x].reg;
}
static void* get_mmx_reg_src(int x)
{
    return &fpu.mm[x].reg;
}
static void* get_sse_reg_dest(int x)
{
    return &XMM32(x);
}
static void* get_reg_dest(int x)
{
    return &cpu.reg32[x];
}

static void punpckh(void* dst, void* src, int size, int copysize)
{
    // XXX -- make this faster
    // too many xors
    uint8_t *dst8 = dst, *src8 = src, tmp[16];
    int idx = 0, nidx = 0;
    const int xormask = (size - 1) ^ (copysize - 1);
    while (idx < size) {
        for (int i = 0; i < copysize; i++)
            tmp[idx++ ^ xormask] = src8[(nidx + i) ^ xormask]; // Copy source bytes
        for (int i = 0; i < copysize; i++)
            tmp[idx++ ^ xormask] = dst8[(nidx + i) ^ xormask]; // Copy destination bytes
        nidx += copysize;
    }
    memcpy(dst, tmp, size);
}
static inline uint16_t pack_i32_to_i16(uint32_t x)
{
    //printf("i32 -> i16: %08x\n", x);
    if (x >= 0x80000000) {
        if (x >= 0xFFFF8000)
            x &= 0xFFFF;
        else
            return 0x8000; // x <= -65536
    } else {
        // x <= 0x7FFFFFFF
        if (x > 0x7FFF)
            return 0x7FFF;
    }
    return x;
}
static uint16_t pack_i16_to_u8(int16_t x)
{
    if (x >= 0xFF)
        return 0xFF;
    else if (x < 0)
        return 0;
    return x;
}
static inline uint8_t pack_i16_to_i8(uint16_t x)
{
    if (x >= 0x8000) {
        if (x >= 0xFF80)
            x &= 0xFF;
        else
            return 0x80; // x <= -128
    } else {
        // x <= 0x7FFF
        if (x > 0x7F)
            return 0x7F;
    }
    return x;
}
static void packssdw(void* dest, void* src, int dwordcount)
{
    uint16_t res[8];
    uint32_t *dest32 = dest, *src32 = src;
    for (int i = 0; i < dwordcount; i++) {
        res[i] = pack_i32_to_i16(dest32[i]);
        res[i | dwordcount] = pack_i32_to_i16(src32[i]);
    }
    memcpy(dest, res, dwordcount << 2);
}
static void punpckl(void* dst, void* src, int size, int copysize)
{
    // XXX -- make this faster
    uint8_t *dst8 = dst, *src8 = src, tmp[16];
    int idx = 0, nidx = 0, xor = copysize - 1;
    UNUSED (xor);
    const int xormask = (size - 1) ^ (copysize - 1);
    UNUSED(xormask);
    while (idx < size) {
        for (int i = 0; i < copysize; i++)
            tmp[idx++] = dst8[(nidx + i)]; // Copy destination bytes
        for (int i = 0; i < copysize; i++)
            tmp[idx++] = src8[(nidx + i)]; // Copy source bytes
        nidx += copysize;
    }
    memcpy(dst, tmp, size);
}
static void psubsb(uint8_t* dest, uint8_t* src, int bytecount)
{
    for (int i = 0; i < bytecount; i++) {
        uint8_t x = dest[i], y = src[i], res = x - y;
        x = (x >> 7) + 0x7F;
        if ((int8_t)((x ^ y) & (x ^ res)) < 0)
            res = x;
        dest[i] = res;
    }
}
static void psubsw(uint16_t* dest, uint16_t* src, int wordcount)
{
    for (int i = 0; i < wordcount; i++) {
        uint16_t x = dest[i], y = src[i], res = x - y;
        //printf("%x - %x = %x\n", x, y, res);
        x = (x >> 15) + 0x7FFF;
        if ((int16_t)((x ^ y) & (x ^ res)) < 0)
            res = x;
        dest[i] = res;
    }
}
static void pminub(uint8_t* dest, uint8_t* src, int bytecount)
{
    for (int i = 0; i < bytecount; i++)
        if (src[i] < dest[i])
            dest[i] = src[i];
}
static void pmaxub(uint8_t* dest, uint8_t* src, int bytecount)
{
    for (int i = 0; i < bytecount; i++)
        if (dest[i] < src[i])
            dest[i] = src[i];
}
static void pminsw(int16_t* dest, int16_t* src, int wordcount)
{
    for (int i = 0; i < wordcount; i++)
        if (src[i] < dest[i])
            dest[i] = src[i];
}
static void pmaxsw(int16_t* dest, int16_t* src, int wordcount)
{
    for (int i = 0; i < wordcount; i++)
        if (src[i] > dest[i])
            dest[i] = src[i];
}
static void paddsb(uint8_t* dest, uint8_t* src, int bytecount)
{
    // https://locklessinc.com/articles/sat_arithmetic/
    for (int i = 0; i < bytecount; i++) {
        uint8_t x = dest[i], y = src[i], res = x + y;
        x = (x >> 7) + 0x7F;
        if ((int8_t)((x ^ y) | ~(y ^ res)) >= 0)
            res = x;
        dest[i] = res;
    }
}
static void paddsw(uint16_t* dest, uint16_t* src, int wordcount)
{
    for (int i = 0; i < wordcount; i++) {
        uint16_t x = dest[i], y = src[i], res = x + y;
        x = (x >> 15) + 0x7FFF;
        if ((int16_t)((x ^ y) | ~(y ^ res)) >= 0)
            res = x;
        dest[i] = res;
    }
}
static void pshuf(void* dest, void* src, int imm, int shift)
{
    uint8_t* src8 = src;
    uint8_t res[16];
    int id = 0;
    for (int i = 0; i < 4; i++) {
        int index = imm & 3, index4 = index << shift;
        if (shift == 2) { // Doubleword size
            //printf("index: %d resid=%d %02x%02x%02x%02x\n", index, id, src8[index4 + 0], src8[index4 + 1], src8[index4 + 2], src8[index4 + 3]);
            res[id + 0] = src8[index4 + 0];
            res[id + 1] = src8[index4 + 1];
            res[id + 2] = src8[index4 + 2];
            res[id + 3] = src8[index4 + 3];
            id += 4;
        } else { // shift == 1: Word size
            res[id + 0] = src8[index4 + 0];
            res[id + 1] = src8[index4 + 1];
            id += 2;
        }
        imm >>= 2;
    }
    memcpy(dest, res, 4 << shift);
}

// Not the same as pshuf
static void pshufb(void* dest, void* src, int bytes)
{
    int8_t* src8 = src;
    uint8_t res[16], *dest8 = dest;
    int mask = bytes - 1;
    // https://www.chessprogramming.org/SSSE3#PSHUFB
    for (int i = 0; i < bytes; i++) {
        res[i] = src8[i] < 0 ? 0 : dest8[src8[i] & mask];
    }
    memcpy(dest, res, bytes);
}

static void cpu_psraw(uint16_t* a, int shift, int mask, int wordcount)
{
    // SAR but with MMX/SSE operands
    for (int i = 0; i < wordcount; i++)
        a[i] = (int16_t)a[i] >> shift & mask;
}
static void cpu_psrlw(uint16_t* a, int shift, int mask, int wordcount)
{
    // SHR but with MMX/SSE operands
    for (int i = 0; i < wordcount; i++)
        a[i] = a[i] >> shift & mask;
}
static void cpu_psllw(uint16_t* a, int shift, int mask, int wordcount)
{
    // SHL but with MMX/SSE operands
    for (int i = 0; i < wordcount; i++)
        a[i] = a[i] << shift & mask;
}
static void cpu_psrad(uint32_t* a, int shift, int mask, int wordcount)
{
    int dwordcount = wordcount >> 1;
    for (int i = 0; i < dwordcount; i++)
        a[i] = (int32_t)a[i] >> shift & mask;
}
static void cpu_psrld(uint32_t* a, int shift, int mask, int wordcount)
{
    int dwordcount = wordcount >> 1;
    for (int i = 0; i < dwordcount; i++)
        a[i] = a[i] >> shift & mask;
}
static void cpu_pslld(uint32_t* a, int shift, int mask, int wordcount)
{
    int dwordcount = wordcount >> 1;
    for (int i = 0; i < dwordcount; i++)
        a[i] = a[i] << shift & mask;
}
static void cpu_psrlq(uint64_t* a, int shift, int mask, int wordcount)
{
    int qwordcount = wordcount >> 2;
    for (int i = 0; i < qwordcount; i++) {
        if (mask)
            a[i] = a[i] >> shift;
        else
            a[i] = 0;
    }
}
static void cpu_psllq(uint64_t* a, int shift, int mask, int wordcount)
{
    int qwordcount = wordcount >> 2;
    for (int i = 0; i < qwordcount; i++)
        if (mask)
            a[i] = a[i] << shift;
        else
            a[i] = 0;
}
static void cpu_pslldq(uint64_t* a, int shift, int mask)
{
    if (mask == 0) {
        a[0] = 0;
        a[1] = 0;
        return;
    }
    // This is a 128 bit SHL shift for xmm registers only
    if (shift == 64) {
        a[1] = a[0];
        a[0] = 0;
    } else if (shift > 64) {
        a[1] = a[0] << (shift - 64L);
        a[0] = 0;
    } else {
        a[0] <<= shift; // Bottom bits should be 0
        a[0] |= a[1] >> (64L - shift);
        a[1] <<= shift;
    }
}
static void cpu_psrldq(uint64_t* a, int shift, int mask)
{
    if (mask == 0) {
        a[0] = 0;
        a[1] = 0;
        return;
    }
    // This is a 128 bit SHR shift for xmm registers only
    if (shift == 64) {
        a[0] = a[1];
        a[1] = 0;
    } else if (shift > 64) {
        a[0] = a[1] >> (shift - 64L);
        a[1] = 0;
    } else {
        a[0] >>= shift;
        a[0] |= a[1] << (64L - shift);
        a[1] >>= shift;
    }
}
static void pcmpeqb(uint8_t* dest, uint8_t* src, int count)
{
    for (int i = 0; i < count; i++)
        if (src[i] == dest[i])
            dest[i] = 0xFF;
        else
            dest[i] = 0;
}
static void pcmpeqw(uint16_t* dest, uint16_t* src, int count)
{
    for (int i = 0; i < count; i++)
        if (src[i] == dest[i])
            dest[i] = 0xFFFF;
        else
            dest[i] = 0;
}
static void pcmpeqd(uint32_t* dest, uint32_t* src, int count)
{
    for (int i = 0; i < count; i++)
        if (src[i] == dest[i])
            dest[i] = 0xFFFFFFFF;
        else
            dest[i] = 0;
}
static void pcmpgtb(int8_t* dest, int8_t* src, int count)
{
    for (int i = 0; i < count; i++)
        if (dest[i] > src[i])
            dest[i] = 0xFF;
        else
            dest[i] = 0;
}
static void pcmpgtw(int16_t* dest, int16_t* src, int count)
{
    for (int i = 0; i < count; i++)
        if (dest[i] > src[i])
            dest[i] = 0xFFFF;
        else
            dest[i] = 0;
}
static void pcmpgtd(int32_t* dest, int32_t* src, int count)
{
    for (int i = 0; i < count; i++)
        if (dest[i] > src[i])
            dest[i] = 0xFFFFFFFF;
        else
            dest[i] = 0;
}
static void packuswb(void* dest, void* src, int wordcount)
{
    uint8_t res[16];
    uint16_t *dest16 = dest, *src16 = src;
    for (int i = 0; i < wordcount; i++) {
        res[i] = pack_i16_to_u8(dest16[i]);
        res[i | wordcount] = pack_i16_to_u8(src16[i]);
    }
    memcpy(dest, res, wordcount << 1);
}
static void packsswb(void* dest, void* src, int wordcount)
{
    uint8_t res[16];
    uint16_t *dest16 = dest, *src16 = src;
    for (int i = 0; i < wordcount; i++) {
        res[i] = pack_i16_to_i8(dest16[i]);
        res[i | wordcount] = pack_i16_to_i8(src16[i]);
    }
    memcpy(dest, res, wordcount << 1);
}
static void pmullw(uint16_t* dest, uint16_t* src, int wordcount, int shift)
{
    for (int i = 0; i < wordcount; i++) {
        uint32_t result = (uint32_t)(int16_t)dest[i] * (uint32_t)(int16_t)src[i];
        dest[i] = result >> shift;
    }
}
static void pmuluw(void* dest, void* src, int wordcount, int shift)
{
    uint16_t *dest16 = dest, *src16 = src;
    for (int i = 0; i < wordcount; i++) {
        uint32_t result = (uint32_t)dest16[i] * (uint32_t)src16[i];
        dest16[i] = result >> shift;
    }
}
static void pmuludq(void* dest, void* src, int dwordcount)
{
    uint32_t *dest32 = dest, *src32 = src;
    for (int i = 0; i < dwordcount; i += 2) {
        uint64_t result = (uint64_t)dest32[i] * (uint64_t)src32[i];
        dest32[i] = result;
        dest32[i + 1] = result >> 32L;
    }
}
static int pmovmskb(uint8_t* src, int bytecount)
{
    int dest = 0;
    for (int i = 0; i < bytecount; i++) {
        dest |= (src[i] >> 7) << i;
    }
    return dest;
}
static void psubusb(uint8_t* dest, uint8_t* src, int bytecount)
{
    for (int i = 0; i < bytecount; i++) {
        uint8_t result = dest[i] - src[i];
        dest[i] = -(result <= dest[i]) & result;
    }
}
static void psubusw(uint16_t* dest, uint16_t* src, int wordcount)
{
    for (int i = 0; i < wordcount; i++) {
        uint16_t result = dest[i] - src[i];
        dest[i] = -(result <= dest[i]) & result;
    }
}
static void paddusb(uint8_t* dest, uint8_t* src, int bytecount)
{
    for (int i = 0; i < bytecount; i++) {
        uint8_t result = dest[i] + src[i];
        dest[i] = -(result < dest[i]) | result;
    }
}
static void paddusw(uint16_t* dest, uint16_t* src, int wordcount)
{
    for (int i = 0; i < wordcount; i++) {
        uint16_t result = dest[i] + src[i];
        dest[i] = -(result < dest[i]) | result;
    }
}
static void paddb(uint8_t* dest, uint8_t* src, int bytecount)
{
    if (dest == src) // Faster alternative
        for (int i = 0; i < bytecount; i++)
            dest[i] <<= 1;
    else
        for (int i = 0; i < bytecount; i++)
            dest[i] += src[i];
}
static void paddw(uint16_t* dest, uint16_t* src, int wordcount)
{
    if (dest == src)
        for (int i = 0; i < wordcount; i++)
            dest[i] <<= 1;
    else
        for (int i = 0; i < wordcount; i++)
            dest[i] += src[i];
}
static void paddd(uint32_t* dest, uint32_t* src, int dwordcount)
{
    if (dest == src)
        for (int i = 0; i < dwordcount; i++)
            dest[i] <<= 1;
    else
        for (int i = 0; i < dwordcount; i++)
            dest[i] += src[i];
}
static void psubb(uint8_t* dest, uint8_t* src, int bytecount)
{
    if (dest == src)
        for (int i = 0; i < bytecount; i++)
            dest[i] >>= 1;
    else
        for (int i = 0; i < bytecount; i++)
            dest[i] -= src[i];
}
static void psubw(uint16_t* dest, uint16_t* src, int wordcount)
{
    if (dest == src)
        for (int i = 0; i < wordcount; i++)
            dest[i] >>= 1;
    else
        for (int i = 0; i < wordcount; i++)
            dest[i] -= src[i];
}
static void psubd(uint32_t* dest, uint32_t* src, int dwordcount)
{
    if (dest == src)
        for (int i = 0; i < dwordcount; i++)
            dest[i] >>= 1;
    else
        for (int i = 0; i < dwordcount; i++)
            dest[i] -= src[i];
}
static void psubq(uint64_t* dest, uint64_t* src, int qwordcount)
{
    if (dest == src)
        for (int i = 0; i < qwordcount; i++)
            dest[i] >>= 1;
    else
        for (int i = 0; i < qwordcount; i++)
            dest[i] -= src[i];
}
static uint32_t cmpps(float32 dest, float32 src, int cmp)
{
    // Idea:
    // switch(cmp & 3) { return -(float32_eq_ordered_quiet(dest, src, &status) ^ (cmp >> 2)); }
    switch (cmp & 7) {
    // https://www.felixcloutier.com/x86/cmpps
    // Note that some compares are quiet and some are signalling
    case 0:
        return -float32_eq_ordered_quiet(dest, src, &status);
    case 1:
        return -float32_lt_ordered_signalling(dest, src, &status);
    case 2:
        return -float32_le_ordered_signalling(dest, src, &status);
    case 3:
        return -float32_unordered_quiet(dest, src, &status);
    case 4:
        return -float32_neq_ordered_quiet(dest, src, &status);
    case 5:
        return -float32_nlt_unordered_signalling(dest, src, &status);
    case 6:
        return -float32_nle_unordered_signalling(dest, src, &status);
    case 7:
        return -float32_ordered_quiet(dest, src, &status);
    }
    abort();
}
static uint64_t cmppd(float64 dest, float64 src, int cmp)
{
    switch (cmp & 7) {
    case 0:
        return -float64_eq_ordered_quiet(dest, src, &status);
    case 1:
        return -float64_lt_ordered_signalling(dest, src, &status);
    case 2:
        return -float64_le_ordered_signalling(dest, src, &status);
    case 3:
        return -float64_unordered_quiet(dest, src, &status);
    case 4:
        return -float64_neq_ordered_quiet(dest, src, &status);
    case 5:
        return -float64_nlt_unordered_signalling(dest, src, &status);
    case 6:
        return -float64_nle_unordered_signalling(dest, src, &status);
    case 7:
        return -float64_ordered_quiet(dest, src, &status);
    }
    abort();
}
static void shufps(void* dest, void* src, int imm)
{
    uint32_t *src32 = src, *dest32 = dest;
    uint32_t res[4];
    res[0] = dest32[imm >> 0 & 3];
    res[1] = dest32[imm >> 2 & 3];
    res[2] = src32[imm >> 4 & 3];
    res[3] = src32[imm >> 6 & 3];
    memcpy(dest32, res, 16);
}
static void shufpd(void* dest, void* src, int imm)
{
    uint32_t *src32 = src, *dest32 = dest;
    if (imm & 1) {
        dest32[0] = dest32[2];
        dest32[1] = dest32[3];
    }
    if (imm & 2) {
        dest32[2] = src32[2];
        dest32[3] = src32[3];
    } else {
        dest32[2] = dest32[0];
        dest32[3] = dest32[1];
    }
}
static void pavgb(void* dest, void* src, int bytecount)
{
    uint8_t *dest8 = dest, *src8 = src;
    for (int i = 0; i < bytecount; i++)
        dest8[i] = (dest8[i] + src8[i]) >> 1;
}
static void pavgw(void* dest, void* src, int wordcount)
{
    uint16_t *dest16 = dest, *src16 = src;
    for (int i = 0; i < wordcount; i++)
        dest16[i] = (dest16[i] + src16[i]) >> 1;
}
static void pmaddwd(void* dest, void* src, int dwordcount)
{
    uint16_t *src16 = src, *dest16 = dest;
    uint32_t res[4];
    int idx = 0;
    for (int i = 0; i < dwordcount; i++) {
        // "Multiplies the individual signed words of the destination operand (first operand) by the corresponding signed words of the source operand (second operand), producing temporary signed, doubleword results."
        res[i] = ((uint32_t)(int16_t)src16[idx] * (uint32_t)(int16_t)dest16[idx]) + ((uint32_t)(int16_t)src16[idx + 1] * (uint32_t)(int16_t)dest16[idx + 1]);
        idx += 2;
    }
    memcpy(dest, res, dwordcount << 2);
}
static void psadbw(void* dest, void* src, int qwordcount)
{
    uint8_t *src8 = src, *dest8 = dest;
    for (int i = 0; i < qwordcount; i++) {
        uint32_t sum = 0, offs = i << 3;
        for (int j = 0; j < 8; j++) {
            int diff = src8[j | offs] - dest8[j | offs];
            if (diff < 0)
                diff = -diff;
            sum += diff;
            dest8[j | offs] = 0;
        }
        dest8[offs | 0] = sum;
        dest8[offs | 1] = sum >> 8;
    }
}

static void pabsb(void* dest, void* src, int bytecount)
{
    int8_t* src8 = src;
    uint8_t* dest8 = dest;
    for (int i = 0; i < bytecount; i++)
        dest8[i] = src8[i] < 0 ? -src8[i] : src8[i];
}
static void pabsw(void* dest, void* src, int wordcount)
{
    int16_t* src16 = src;
    uint16_t* dest16 = dest;
    for (int i = 0; i < wordcount; i++)
        dest16[i] = src16[i] < 0 ? -src16[i] : src16[i];
}
static void pabsd(void* dest, void* src, int dwordcount)
{
    int32_t* src32 = src;
    uint32_t* dest32 = dest;
    for (int i = 0; i < dwordcount; i++)
        dest32[i] = src32[i] < 0 ? -src32[i] : src32[i];
}

///////////////////////////////////////////////////////////////////////////////
// Actual operations
///////////////////////////////////////////////////////////////////////////////

#define EX(n) \
    if ((n))  \
    return 1
int execute_0F10_17(struct decoded_instruction* i)
{
    CHECK_SSE;
    // All opcodes from 0F 10 through 0F 17
    uint32_t *dest32, *src32, flags = i->flags;
    switch (i->imm8 & 31) {
    case MOVUPS_XGoXEo:
        // xmm128 <<== r/m128
        EX(get_sse_read_ptr(flags, i, 4, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)(result_ptr);
        dest32[1] = *(uint32_t*)(result_ptr + 4);
        dest32[2] = *(uint32_t*)(result_ptr + 8);
        dest32[3] = *(uint32_t*)(result_ptr + 12);
        break;
    case MOVSS_XGdXEd:
        // xmm32 <<== r/m32
        // Clear top 96 bits if source is memory
        EX(get_sse_read_ptr(flags, i, 1, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)(result_ptr);
        if (!I_OP2(flags)) {
            // MOVSS mem --> reg clears upper bits
            dest32[1] = 0;
            dest32[2] = 0;
            dest32[3] = 0;
        }
        break;
    case MOVSD_XGqXEq:
        // xmm64 <<== r/m64
        // Clear top 64 bits if source is memory
        EX(get_sse_read_ptr(flags, i, 2, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)(result_ptr);
        dest32[1] = *(uint32_t*)(result_ptr + 4);
        if (!I_OP2(flags)) {
            // MOVSD mem --> reg clears upper bits
            dest32[2] = 0;
            dest32[3] = 0;
        }
        break;
    case MOVUPS_XEoXGo:
        // r/m128 <<== xmm128
        EX(get_sse_write_ptr(flags, i, 4, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)(result_ptr) = dest32[0];
        *(uint32_t*)(result_ptr + 4) = dest32[1];
        *(uint32_t*)(result_ptr + 8) = dest32[2];
        *(uint32_t*)(result_ptr + 12) = dest32[3];
        WRITE_BACK();
        break;
    case MOVSS_XEdXGd:
        // r/m32 <<== xmm32
        EX(get_sse_write_ptr(flags, i, 1, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)(result_ptr) = dest32[0];
        WRITE_BACK();
        break;
    case MOVSD_XEqXGq:
        // r/m64 <<== xmm64
        EX(get_sse_write_ptr(flags, i, 2, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)(result_ptr) = dest32[0];
        *(uint32_t*)(result_ptr + 4) = dest32[1];
        WRITE_BACK();
        break;

    case MOVHLPS_XGqXEq:
        // DEST[00...1F] = SRC[40...5F]
        // DEST[20...3F] = SRC[60...7F]
        dest32 = get_sse_reg_dest(I_REG(flags));
        src32 = get_sse_reg_dest(I_RM(flags));
        dest32[0] = src32[2];
        dest32[1] = src32[3];
        break;
    case MOVLPS_XGqXEq:
        // xmm64 <== r/m64
        // Upper bits are NOT cleared
        EX(get_sse_read_ptr(flags, i, 2, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)(result_ptr);
        dest32[1] = *(uint32_t*)(result_ptr + 4);
        break;
    case UNPCKLPS_XGoXEq:
        // DEST[00...1F] = DEST[00...1F] <-- NOP
        // DEST[20...3F] = SRC[00...1F]
        // DEST[40...5F] = DEST[20...3F]
        // DEST[60...7F] = SRC[20...3F]
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        // Do the dest <-- dest moves before we destroy the data in dest
        dest32[2] = dest32[1];
        dest32[1] = *(uint32_t*)(result_ptr);
        dest32[3] = *(uint32_t*)(result_ptr + 4);
        break;
    case UNPCKLPD_XGoXEo:
        // DEST[00...3F] = DEST[00...3F] <-- NOP
        // DEST[40...7F] = SRC[00...3F]
        EX(get_sse_read_ptr(flags, i, 4, 1)); // Some implementations only access 8 bytes; for simplicity, we access all 16 bytes
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[2] = *(uint32_t*)(result_ptr);
        dest32[3] = *(uint32_t*)(result_ptr + 4);
        break;
    case UNPCKHPS_XGoXEq:
        // DEST[00...1F] = DEST[40...5F]
        // DEST[20...3F] = SRC[40...5F]
        // DEST[40...5F] = DEST[60...7F]
        // DEST[60...7F] = SRC[60...7F]
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        // Do the dest <-- dest moves before we destroy the data in dest
        dest32[0] = dest32[2];
        dest32[2] = dest32[3];
        dest32[1] = *(uint32_t*)(result_ptr + 8);
        dest32[3] = *(uint32_t*)(result_ptr + 12);
        break;
    case UNPCKHPD_XGoXEo:
        // DEST[00...3F] = DEST[40...7F]
        // DEST[40...7F] = SRC[40...7F]
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        // Do the dest <-- dest moves before we destroy the data in dest
        dest32[0] = dest32[2];
        dest32[1] = dest32[3];
        dest32[2] = *(uint32_t*)(result_ptr + 8);
        dest32[3] = *(uint32_t*)(result_ptr + 12);
        break;
    case MOVLHPS_XGqXEq:
        // DEST[40...7F] = SRC[00...3F]
        dest32 = get_sse_reg_dest(I_REG(flags));
        src32 = get_sse_reg_dest(I_RM(flags));
        dest32[2] = src32[0];
        dest32[3] = src32[1];
        break;
    case MOVHPS_XGqXEq:
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[2] = *(uint32_t*)(result_ptr);
        dest32[3] = *(uint32_t*)(result_ptr + 4);
        break;
    case MOVSHDUP_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)(result_ptr + 4);
        dest32[1] = *(uint32_t*)(result_ptr + 4);
        dest32[2] = *(uint32_t*)(result_ptr + 12);
        dest32[3] = *(uint32_t*)(result_ptr + 12);
        break;
    case MOVHPS_XEqXGq:
        EX(get_sse_write_ptr(flags, i, 2, 1));
        src32 = get_sse_reg_dest(I_REG(flags));
        if (I_OP2(flags)) {
            // register --> register moves: upper two quadwords
            *(uint32_t*)(result_ptr + 8) = src32[0];
            *(uint32_t*)(result_ptr + 12) = src32[1];
        } else {
            // register --> memory
            *(uint32_t*)(result_ptr) = src32[2];
            *(uint32_t*)(result_ptr + 4) = src32[3];
        }
        WRITE_BACK();
        break;
    }
    return 0;
}
int execute_0F28_2F(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t *dest32, *src32, flags = i->flags;
    int fp_exception = 0;
    switch (i->imm8 & 15) {
    case MOVAPS_XGoXEo:
        // xmm128 <== r/m128
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)(result_ptr);
        dest32[1] = *(uint32_t*)(result_ptr + 4);
        dest32[2] = *(uint32_t*)(result_ptr + 8);
        dest32[3] = *(uint32_t*)(result_ptr + 12);
        break;
    case MOVAPS_XEoXGo:
        // r.m128 <== xmm128
        EX(get_sse_write_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)(result_ptr) = dest32[0];
        *(uint32_t*)(result_ptr + 4) = dest32[1];
        *(uint32_t*)(result_ptr + 8) = dest32[2];
        *(uint32_t*)(result_ptr + 12) = dest32[3];
        WRITE_BACK();
        break;
    case CVTPI2PS_XGqMEq:
        // DEST[00...1F] = Int32ToFloat(SRC[00...1F])
        // DEST[20...3F] = Int32ToFloat(SRC[20...3F])
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_sse_reg_dest(I_REG(flags));
        src32 = result_ptr;
        dest32[0] = int32_to_float32(src32[0], &status);
        dest32[1] = int32_to_float32(src32[1], &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTSI2SS_XGdEd:
        // DEST[00...1F] = Int32ToFloat(SRC[00...1F])
        EX(get_reg_read_ptr(flags, i));
        dest32 = get_sse_reg_dest(I_REG(flags));
        src32 = result_ptr;
        dest32[0] = int32_to_float32(src32[0], &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTPI2PD_XGoMEq:
        // DEST[00...3F] = Int32ToDouble(SRC[00...1F])
        // DEST[40...6F] = Int32ToDouble(SRC[20...3F])
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_sse_reg_dest(I_REG(flags));
        src32 = result_ptr;
        *(uint64_t*)(&dest32[0]) = int32_to_float64(src32[0]);
        *(uint64_t*)(&dest32[2]) = int32_to_float64(src32[1]);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTSI2SD_XGqMEd:
        // DEST[00...1F] = Int32ToDouble(SRC[00...1F])
        EX(get_reg_read_ptr(flags, i));
        dest32 = get_sse_reg_dest(I_REG(flags));
        src32 = result_ptr;
        *(uint64_t*)(&dest32[0]) = int32_to_float64(src32[0]);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTPS2PI_MGqXEq:
        // DEST[00...1F] = Int32ToDouble(SRC[00...1F])
        // DEST[20...3F] = Int32ToDouble(SRC[20...3F])
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        src32 = result_ptr;
        if (i->imm8 & 16) {
            dest32[0] = float32_to_int32(src32[0], &status);
            dest32[1] = float32_to_int32(src32[1], &status);
        } else {
            dest32[0] = float32_to_int32_round_to_zero(src32[0], &status);
            dest32[1] = float32_to_int32_round_to_zero(src32[1], &status);
        }
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTSS2SI_GdXEd:
        // DEST[00...1F] = Int32ToDouble(SRC[00...1F])
        EX(get_sse_read_ptr(flags, i, 1, 1));
        dest32 = get_reg_dest(I_REG(flags));
        src32 = result_ptr;
        if (i->imm8 & 16)
            dest32[0] = float32_to_int32(src32[0], &status);
        else
            dest32[0] = float32_to_int32_round_to_zero(src32[0], &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTPD2PI_MGqXEo:
        // DEST[00...1F] = Int32ToDouble(SRC[00...3F])
        // DEST[20...3F] = Int32ToDouble(SRC[40...7F])
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        src32 = result_ptr;
        if (i->imm8 & 16) {
            dest32[0] = float64_to_int32(*(uint64_t*)(&src32[0]), &status);
            dest32[1] = float64_to_int32(*(uint64_t*)(&src32[2]), &status);
        } else {
            dest32[0] = float64_to_int32_round_to_zero(*(uint64_t*)(&src32[0]), &status);
            dest32[1] = float64_to_int32_round_to_zero(*(uint64_t*)(&src32[2]), &status);
        }
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTSD2SI_GdXEq:
        // DEST[00...1F] = Int32ToDouble(SRC[00...3F])
        EX(get_sse_read_ptr(flags, i, 1, 1));
        dest32 = get_reg_dest(I_REG(flags));
        src32 = result_ptr;
        if (i->imm8 & 16)
            dest32[0] = float64_to_int32(*(uint64_t*)(&src32[0]), &status);
        else
            dest32[0] = float64_to_int32_round_to_zero(*(uint64_t*)(&src32[0]), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case UCOMISS_XGdXEd: {
        EX(get_sse_read_ptr(flags, i, 1, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        int result;
        if (i->imm8 & 16) // UCOMISS
            result = float32_compare(dest32[0], *(uint32_t*)result_ptr, &status);
        else
            result = float32_compare_quiet(dest32[0], *(uint32_t*)result_ptr, &status);
        int eflags = 0;
        switch (result) {
        case float_relation_unordered:
            eflags = EFLAGS_ZF | EFLAGS_PF | EFLAGS_CF;
            break;
        case float_relation_less:
            eflags = EFLAGS_CF;
            break;
        case float_relation_greater:
            eflags = 0;
            break;
        case float_relation_equal:
            eflags = EFLAGS_ZF;
            break;
        }
        cpu_set_eflags(eflags | (cpu.eflags & ~arith_flag_mask));
        fp_exception = cpu_sse_handle_exceptions();
        break;
    }
    case UCOMISD_XGqXEq: {
        EX(get_sse_read_ptr(flags, i, 2, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        int result;
        if (i->imm8 & 16) // UCOMISD
            result = float64_compare(*(uint64_t*)&dest32[0], *(uint64_t*)result_ptr, &status);
        else
            result = float64_compare_quiet(*(uint64_t*)&dest32[0], *(uint64_t*)result_ptr, &status);
        int eflags = 0;
        switch (result) {
        case float_relation_unordered:
            eflags = EFLAGS_ZF | EFLAGS_PF | EFLAGS_CF;
            break;
        case float_relation_less:
            eflags = EFLAGS_CF;
            break;
        case float_relation_greater:
            eflags = 0;
            break;
        case float_relation_equal:
            eflags = EFLAGS_ZF;
            break;
        }
        cpu_set_eflags(eflags | (cpu.eflags & ~arith_flag_mask));
        fp_exception = cpu_sse_handle_exceptions();
        break;
    }
    }
    return fp_exception;
}

static const float32 float32_one = 0x3f800000;
static float32 rsqrt(float32 a)
{
    //TODO: Use 11-bit approximation instead of this
    return float32_div(float32_one, float32_sqrt(a, &status), &status);
}
static float32 rcp(float32 a)
{
    //TODO: Use 11-bit approximation instead of this
    return float32_div(float32_one, a, &status);
}

int execute_0F50_57(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t *dest32, *src32, flags = i->flags;
    int fp_exception = 0, result;
    switch (i->imm8 & 15) {
    case MOVMSKPS_GdXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        src32 = result_ptr;
        result = 0;
        result = src32[0] >> 31;
        result |= src32[1] >> 30 & 2;
        result |= src32[2] >> 29 & 4;
        result |= src32[3] >> 28 & 8;
        cpu.reg32[I_REG(flags)] = result;
        break;
    case MOVMSKPD_GdXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        src32 = result_ptr;
        result = 0;
        result = src32[1] >> 31;
        result |= src32[3] >> 30 & 2;
        cpu.reg32[I_REG(flags)] = result;
        break;
    case SQRTPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        src32 = result_ptr;
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_sqrt(src32[0], &status);
        dest32[1] = float32_sqrt(src32[1], &status);
        dest32[2] = float32_sqrt(src32[2], &status);
        dest32[3] = float32_sqrt(src32[3], &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case SQRTSS_XGdXEd:
        EX(get_sse_read_ptr(flags, i, 1, 1));
        src32 = result_ptr;
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_sqrt(src32[0], &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case SQRTPD_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        src32 = result_ptr;
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint64_t*)&dest32[0] = float64_sqrt(*(uint64_t*)&src32[0], &status);
        *(uint64_t*)&dest32[2] = float64_sqrt(*(uint64_t*)&src32[2], &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case SQRTSD_XGqXEq:
        EX(get_sse_read_ptr(flags, i, 2, 0));
        src32 = result_ptr;
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint64_t*)&dest32[0] = float64_sqrt(*(uint64_t*)&src32[0], &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case RSQRTSS_XGdXEd:
        // XXX - According to https://stackoverflow.com/a/59186778, we are supposed to round to 11 bits.
        // However, this would be too complicated, so we use the slower, less correct way
        EX(get_sse_read_ptr(flags, i, 1, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = rsqrt(*(uint32_t*)result_ptr);
        fp_exception = cpu_sse_handle_exceptions();
#ifdef INSTRUMENT
        cpu_instrument_approximate_sse(I_REG(flags), 1);
#endif
        break;
    case RSQRTPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = rsqrt(*(uint32_t*)(result_ptr));
        dest32[1] = rsqrt(*(uint32_t*)(result_ptr + 4));
        dest32[2] = rsqrt(*(uint32_t*)(result_ptr + 8));
        dest32[3] = rsqrt(*(uint32_t*)(result_ptr + 12));
        fp_exception = cpu_sse_handle_exceptions();
#ifdef INSTRUMENT
        cpu_instrument_approximate_sse(I_REG(flags), 4);
#endif
        break;
    case RCPSS_XGdXEd:
        EX(get_sse_read_ptr(flags, i, 1, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = rcp(*(uint32_t*)result_ptr);
        fp_exception = cpu_sse_handle_exceptions();
#ifdef INSTRUMENT
        cpu_instrument_approximate_sse(I_REG(flags), 1);
#endif
        break;
    case RCPPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = rcp(*(uint32_t*)(result_ptr));
        dest32[1] = rcp(*(uint32_t*)(result_ptr + 4));
        dest32[2] = rcp(*(uint32_t*)(result_ptr + 8));
        dest32[3] = rcp(*(uint32_t*)(result_ptr + 12));
        fp_exception = cpu_sse_handle_exceptions();
#ifdef INSTRUMENT
        cpu_instrument_approximate_sse(I_REG(flags), 4);
#endif
        break;
    case ANDPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] &= *(uint32_t*)(result_ptr);
        dest32[1] &= *(uint32_t*)(result_ptr + 4);
        dest32[2] &= *(uint32_t*)(result_ptr + 8);
        dest32[3] &= *(uint32_t*)(result_ptr + 12);
        break;
    case ORPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] |= *(uint32_t*)(result_ptr);
        dest32[1] |= *(uint32_t*)(result_ptr + 4);
        dest32[2] |= *(uint32_t*)(result_ptr + 8);
        dest32[3] |= *(uint32_t*)(result_ptr + 12);
        break;
    case ANDNPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = ~dest32[0] & *(uint32_t*)(result_ptr);
        dest32[1] = ~dest32[1] & *(uint32_t*)(result_ptr + 4);
        dest32[2] = ~dest32[2] & *(uint32_t*)(result_ptr + 8);
        dest32[3] = ~dest32[3] & *(uint32_t*)(result_ptr + 12);
        break;
    case XORPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] ^= *(uint32_t*)(result_ptr);
        dest32[1] ^= *(uint32_t*)(result_ptr + 4);
        dest32[2] ^= *(uint32_t*)(result_ptr + 8);
        dest32[3] ^= *(uint32_t*)(result_ptr + 12);
        break;
    }
    return fp_exception;
}

int execute_0F68_6F(struct decoded_instruction* i)
{
    uint32_t *dest32, flags = i->flags;
    switch (i->imm8 & 15) {
    case PUNPCKHBW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        punpckh(dest32, result_ptr, 8, 1);
        break;
    case PUNPCKHBW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        punpckh(dest32, result_ptr, 16, 1);
        break;
    case PUNPCKHWD_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        punpckh(dest32, result_ptr, 8, 2);
        break;
    case PUNPCKHWD_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        punpckh(dest32, result_ptr, 16, 2);
        break;
    case PUNPCKHDQ_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        punpckh(dest32, result_ptr, 8, 4);
        break;
    case PUNPCKHDQ_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        punpckh(dest32, result_ptr, 16, 4);
        break;
    case PACKSSDW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        packssdw(dest32, result_ptr, 2);
        break;
    case PACKSSDW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        packssdw(dest32, result_ptr, 4);
        break;
    case PUNPCKLQDQ_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        punpckl(dest32, result_ptr, 16, 8);
        break;
    case PUNPCKHQDQ_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        punpckh(dest32, result_ptr, 16, 8);
        break;
    case MOVD_MGdEd:
        CHECK_MMX;
        EX(get_reg_read_ptr(flags, i));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)result_ptr;
        dest32[1] = 0;
        break;
    case MOVD_XGdEd:
        CHECK_SSE;
        EX(get_reg_read_ptr(flags, i));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)result_ptr;
        dest32[1] = 0;
        dest32[2] = 0;
        dest32[3] = 0;
        break;
    case MOVQ_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)(result_ptr + 0);
        dest32[1] = *(uint32_t*)(result_ptr + 4);
        break;
    case MOVDQA_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)(result_ptr + 0);
        dest32[1] = *(uint32_t*)(result_ptr + 4);
        dest32[2] = *(uint32_t*)(result_ptr + 8);
        dest32[3] = *(uint32_t*)(result_ptr + 12);
        break;
    case MOVDQU_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 0)); // Note: Unaligned move
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)(result_ptr + 0);
        dest32[1] = *(uint32_t*)(result_ptr + 4);
        dest32[2] = *(uint32_t*)(result_ptr + 8);
        dest32[3] = *(uint32_t*)(result_ptr + 12);
        break;
    case OP_68_6F_INVALID:
        EXCEPTION_UD();
    }
    return 0;
}
int execute_0FE8_EF(struct decoded_instruction* i)
{
    uint32_t *dest32, flags = i->flags;
    if (i->imm8 & 1) {
        CHECK_SSE;
    } else {
        CHECK_MMX;
    }
    switch (i->imm8 & 15) {
    case PSUBSB_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        psubsb((uint8_t*)dest32, result_ptr, 8);
        break;
    case PSUBSB_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        psubsb((uint8_t*)dest32, result_ptr, 16);
        break;
    case PSUBSW_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        psubsw((uint16_t*)dest32, result_ptr, 4);
        break;
    case PSUBSW_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        psubsw((uint16_t*)dest32, result_ptr, 8);
        break;
    case PMINSW_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pminsw((int16_t*)dest32, result_ptr, 4);
        break;
    case PMINSW_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pminsw((int16_t*)dest32, result_ptr, 8);
        break;
    case POR_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        dest32[0] |= *(uint32_t*)result_ptr;
        dest32[1] |= *(uint32_t*)(result_ptr + 4);
        break;
    case POR_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] |= *(uint32_t*)result_ptr;
        dest32[1] |= *(uint32_t*)(result_ptr + 4);
        dest32[2] |= *(uint32_t*)(result_ptr + 8);
        dest32[3] |= *(uint32_t*)(result_ptr + 12);
        break;
    case PADDSB_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        paddsb((uint8_t*)dest32, result_ptr, 8);
        break;
    case PADDSB_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        paddsb((uint8_t*)dest32, result_ptr, 16);
        break;
    case PADDSW_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        paddsw((uint16_t*)dest32, result_ptr, 4);
        break;
    case PADDSW_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        paddsw((uint16_t*)dest32, result_ptr, 8);
        break;
    case PMAXSW_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pmaxsw((int16_t*)dest32, result_ptr, 4);
        break;
    case PMAXSW_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pmaxsw((int16_t*)dest32, result_ptr, 8);
        break;
    case PXOR_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        dest32[0] ^= *(uint32_t*)result_ptr;
        dest32[1] ^= *(uint32_t*)(result_ptr + 4);
        break;
    case PXOR_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] ^= *(uint32_t*)result_ptr;
        dest32[1] ^= *(uint32_t*)(result_ptr + 4);
        dest32[2] ^= *(uint32_t*)(result_ptr + 8);
        dest32[3] ^= *(uint32_t*)(result_ptr + 12);
        break;
    }
    return 0;
}

static void pshift(void* dest, int opcode, int wordcount, int imm)
{
    int mask = -1;
    switch (opcode) {
    case PSHIFT_PSRLW:
        if (imm >= 16)
            mask = 0;
        cpu_psrlw(dest, imm & 15, mask, wordcount);
        break;
    case PSHIFT_PSRAW:
        if (imm >= 16)
            mask = 0;
        cpu_psraw(dest, imm & 15, mask, wordcount);
        break;
    case PSHIFT_PSLLW:
        if (imm >= 16)
            mask = 0;
        cpu_psllw(dest, imm & 15, mask, wordcount);
        break;
    case PSHIFT_PSRLD:
        if (imm >= 32)
            mask = 0;
        cpu_psrld(dest, imm & 31, mask, wordcount);
        break;
    case PSHIFT_PSRAD:
        if (imm >= 32)
            mask = 0;
        cpu_psrad(dest, imm & 31, mask, wordcount);
        break;
    case PSHIFT_PSLLD:
        if (imm >= 32)
            mask = 0;
        cpu_pslld(dest, imm & 31, mask, wordcount);
        break;
    case PSHIFT_PSRLQ:
        if (imm >= 64)
            mask = 0;
        cpu_psrlq(dest, imm & 63, mask, wordcount);
        break;
    case PSHIFT_PSRLDQ:
        if (imm >= 128)
            mask = 0;
        cpu_psrldq(dest, imm & 127, mask);
        break;
    case PSHIFT_PSLLQ:
        if (imm >= 64)
            mask = 0;
        cpu_psllq(dest, imm & 63, mask, wordcount);
        break;
    case PSHIFT_PSLLDQ:
        if (imm >= 128)
            mask = 0;
        cpu_pslldq(dest, imm & 127, mask);
        break;
    }
}

int execute_0F70_76(struct decoded_instruction* i)
{
    uint32_t *dest32, flags = i->flags;
    int imm;
    switch (i->imm8 & 15) {
    case PSHUFW_MGqMEqIb:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        imm = i->imm16 >> 8;
        pshuf(dest32, result_ptr, imm, 1);
        break;
    case PSHUFHW_XGoXEoIb:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)(result_ptr);
        dest32[1] = *(uint32_t*)(result_ptr + 4);
        imm = i->imm16 >> 8;
        pshuf(dest32 + 2, result_ptr + 8, imm, 1);
        break;
    case PSHUFLW_XGoXEoIb:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[2] = *(uint32_t*)(result_ptr + 8);
        dest32[3] = *(uint32_t*)(result_ptr + 12);
        imm = i->imm16 >> 8;
        pshuf(dest32, result_ptr, imm, 1);
        break;
    case PSHUFD_XGoXEoIb:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        imm = i->imm16 >> 8;
        pshuf(dest32, result_ptr, imm, 2);
        break;
    case PSHIFT_MGqIb:
        CHECK_MMX;
        dest32 = get_mmx_reg_dest(I_RM(flags)); // Note: R/M is dest, but RMW accesses are not allowed
        imm = i->imm16 >> 8;
        pshift(dest32, i->imm8 >> 4 & 15, 4, imm);
        break;
    case PSHIFT_XEoIb:
        CHECK_SSE;
        dest32 = get_sse_reg_dest(I_RM(flags));
        imm = i->imm16 >> 8;
        pshift(dest32, i->imm8 >> 4 & 15, 8, imm);
        break;
    case PCMPEQB_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pcmpeqb((uint8_t*)dest32, result_ptr, 8);
        break;
    case PCMPEQB_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pcmpeqb((uint8_t*)dest32, result_ptr, 16);
        break;
    case PCMPEQW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pcmpeqw((uint16_t*)dest32, result_ptr, 4);
        break;
    case PCMPEQW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pcmpeqw((uint16_t*)dest32, result_ptr, 8);
        break;
    case PCMPEQD_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pcmpeqd(dest32, result_ptr, 2);
        break;
    case PCMPEQD_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pcmpeqd(dest32, result_ptr, 4);
        break;
    }
    return 0;
}
int execute_0F60_67(struct decoded_instruction* i)
{
    uint32_t *dest32, flags = i->flags;
    if (i->imm8 & 1) {
        CHECK_SSE;
    } else {
        CHECK_MMX;
    }
    switch (i->imm8 & 15) {
    case PUNPCKLBW_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        punpckl(dest32, result_ptr, 8, 1);
        break;
    case PUNPCKLBW_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        punpckl(dest32, result_ptr, 16, 1);
        break;
    case PUNPCKLWD_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        punpckl(dest32, result_ptr, 8, 2);
        break;
    case PUNPCKLWD_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        punpckl(dest32, result_ptr, 16, 2);
        break;
    case PUNPCKLDQ_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        punpckl(dest32, result_ptr, 8, 4);
        break;
    case PUNPCKLDQ_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        punpckl(dest32, result_ptr, 16, 4);
        break;
    case PACKSSWB_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        packsswb(dest32, result_ptr, 4);
        break;
    case PACKSSWB_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        packsswb(dest32, result_ptr, 8);
        break;
    case PCMPGTB_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pcmpgtb((int8_t*)dest32, result_ptr, 8);
        break;
    case PCMPGTB_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pcmpgtb((int8_t*)dest32, result_ptr, 16);
        break;
    case PCMPGTW_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pcmpgtw((int16_t*)dest32, result_ptr, 4);
        break;
    case PCMPGTW_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pcmpgtw((int16_t*)dest32, result_ptr, 8);
        break;
    case PCMPGTD_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pcmpgtd((int32_t*)dest32, result_ptr, 2);
        break;
    case PCMPGTD_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pcmpgtd((int32_t*)dest32, result_ptr, 4);
        break;
    case PACKUSWB_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        packuswb(dest32, result_ptr, 4);
        break;
    case PACKUSWB_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        packuswb(dest32, result_ptr, 8);
        break;
    }
    return 0;
}

// Packed shifts return 0 if their operands are too large.
// All shifts should fit in one byte. A non-zero value in any other byte signifies that the value is > 16
static uint32_t get_shift(void* x, int bytes)
{
    uint8_t* dest = x;
    for (int i = 1; i < bytes; i++)
        if (dest[i])
            return 0xFF;
    return dest[0];
}
int execute_0FD0_D7(struct decoded_instruction* i)
{
    uint32_t *dest32, *src32, flags = i->flags;
    switch (i->imm8 & 15) {
    case PSRLW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSRLW, 4, get_shift(result_ptr, 8));
        break;
    case PSRLW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSRLW, 8, get_shift(result_ptr, 8));
        break;
    case PSRLD_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSRLD, 4, get_shift(result_ptr, 8));
        break;
    case PSRLD_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSRLD, 8, get_shift(result_ptr, 8));
        break;
    case PSRLQ_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSRLQ, 4, get_shift(result_ptr, 8));
        break;
    case PSRLQ_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSRLQ, 8, get_shift(result_ptr, 8));
        break;
    case PADDQ_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        *((uint64_t*)dest32) += *(uint64_t*)result_ptr;
        break;
    case PADDQ_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *((uint64_t*)(dest32)) += *(uint64_t*)(result_ptr);
        *((uint64_t*)(&dest32[2])) += *(uint64_t*)(result_ptr + 8);
        break;
    case PMULLW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pmullw((uint16_t*)dest32, result_ptr, 4, 0);
        break;
    case PMULLW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pmullw((uint16_t*)dest32, result_ptr, 8, 0);
        break;
    case MOVQ_XEqXGq:
        CHECK_SSE;
        EX(get_sse_write_ptr(flags, i, 2, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)result_ptr = dest32[0];
        *(uint32_t*)(result_ptr + 4) = dest32[1];
        if (I_OP2(flags)) {
            // Register destination -- clear upper bits
            *(uint32_t*)(result_ptr + 8) = 0;
            *(uint32_t*)(result_ptr + 12) = 0;
        }
        WRITE_BACK();
        break;
    case MOVQ2DQ_XGoMEq:
        CHECK_MMX;
        CHECK_SSE;
        dest32 = get_sse_reg_dest(I_REG(flags));
        src32 = get_mmx_reg_dest(I_REG(flags));
        dest32[0] = src32[0];
        dest32[1] = src32[1];
        dest32[2] = 0;
        dest32[3] = 0;
        break;
    case MOVDQ2Q_MGqXEo:
        CHECK_MMX;
        CHECK_SSE;
        src32 = get_sse_reg_dest(I_REG(flags));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        dest32[0] = src32[0];
        dest32[1] = src32[1];
        break;
    case PMOVMSKB_GdMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        cpu.reg32[I_REG(flags)] = pmovmskb(result_ptr, 8);
        break;
    case PMOVMSKB_GdXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        cpu.reg32[I_REG(flags)] = pmovmskb(result_ptr, 16);
        break;
    }
    return 0;
}

int execute_0FD8_DF(struct decoded_instruction* i)
{
    uint32_t *dest32, flags = i->flags;
    if (i->imm8 & 1) {
        CHECK_SSE;
    } else {
        CHECK_MMX;
    }
    switch (i->imm8 & 15) {
    case PSUBUSB_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        psubusb((uint8_t*)dest32, result_ptr, 8);
        break;
    case PSUBUSB_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        psubusb((uint8_t*)dest32, result_ptr, 16);
        break;
    case PSUBUSW_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        psubusw((uint16_t*)dest32, result_ptr, 4);
        break;
    case PSUBUSW_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        psubusw((uint16_t*)dest32, result_ptr, 8);
        break;
    case PMINUB_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pminub((uint8_t*)dest32, result_ptr, 8);
        break;
    case PMINUB_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pminub((uint8_t*)dest32, result_ptr, 16);
        break;
    case PAND_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        dest32[0] &= *(uint32_t*)result_ptr;
        dest32[1] &= *(uint32_t*)(result_ptr + 4);
        break;
    case PAND_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] &= *(uint32_t*)result_ptr;
        dest32[1] &= *(uint32_t*)(result_ptr + 4);
        dest32[2] &= *(uint32_t*)(result_ptr + 8);
        dest32[3] &= *(uint32_t*)(result_ptr + 12);
        break;
    case PADDUSB_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        paddusb((uint8_t*)dest32, result_ptr, 8);
        break;
    case PADDUSB_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        paddusb((uint8_t*)dest32, result_ptr, 16);
        break;
    case PADDUSW_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        paddusw((uint16_t*)dest32, result_ptr, 4);
        break;
    case PADDUSW_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        paddusw((uint16_t*)dest32, result_ptr, 8);
        break;
    case PMAXUB_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pmaxub((uint8_t*)dest32, result_ptr, 8);
        break;
    case PMAXUB_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pmaxub((uint8_t*)dest32, result_ptr, 16);
        break;
    case PANDN_MGqMEq:
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        dest32[0] = ~dest32[0] & *(uint32_t*)result_ptr;
        dest32[1] = ~dest32[1] & *(uint32_t*)(result_ptr + 4);
        break;
    case PANDN_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = ~dest32[0] & *(uint32_t*)result_ptr;
        dest32[1] = ~dest32[1] & *(uint32_t*)(result_ptr + 4);
        dest32[2] = ~dest32[2] & *(uint32_t*)(result_ptr + 8);
        dest32[3] = ~dest32[3] & *(uint32_t*)(result_ptr + 12);
        break;
    }
    return 0;
}
int execute_0F7E_7F(struct decoded_instruction* i)
{
    uint32_t *dest32, flags = i->flags;
    switch (i->imm8 & 7) {
    case MOVD_EdMGd:
        CHECK_MMX;
        EX(get_reg_write_ptr(flags, i));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        *(uint32_t*)result_ptr = dest32[0];
        WRITE_BACK();
        break;
    case MOVD_EdXGd:
        CHECK_SSE;
        EX(get_reg_write_ptr(flags, i));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)result_ptr = dest32[0];
        WRITE_BACK();
        break;
    case MOVQ_XGqXEq:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 2, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)result_ptr;
        dest32[1] = *(uint32_t*)(result_ptr + 4);
        dest32[2] = 0;
        dest32[3] = 0;
        break;
    case MOVQ_MEqMGq:
        CHECK_MMX;
        EX(get_mmx_write_ptr(flags, i, 2));
        dest32 = get_mmx_reg_src(I_REG(flags));
        *(uint32_t*)result_ptr = dest32[0];
        *(uint32_t*)(result_ptr + 4) = dest32[1];
        WRITE_BACK();
        break;
    case MOVDQA_XEqXGq:
        CHECK_SSE;
        EX(get_sse_write_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)result_ptr = dest32[0];
        *(uint32_t*)(result_ptr + 4) = dest32[1];
        *(uint32_t*)(result_ptr + 8) = dest32[2];
        *(uint32_t*)(result_ptr + 12) = dest32[3];
        WRITE_BACK();
        break;
    case MOVDQU_XEqXGq:
        CHECK_SSE;
        EX(get_sse_write_ptr(flags, i, 4, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)result_ptr = dest32[0];
        *(uint32_t*)(result_ptr + 4) = dest32[1];
        *(uint32_t*)(result_ptr + 8) = dest32[2];
        *(uint32_t*)(result_ptr + 12) = dest32[3];
        WRITE_BACK();
        break;
    }
    return 0;
}

int execute_0FF8_FE(struct decoded_instruction* i)
{
    uint32_t flags = i->flags;
    void* dest;
    switch (i->imm8 & 15) {
    case PSUBB_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest = get_mmx_reg_dest(I_REG(flags));
        psubb(dest, result_ptr, 8);
        break;
    case PSUBB_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest = get_sse_reg_dest(I_REG(flags));
        psubb(dest, result_ptr, 16);
        break;
    case PSUBW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest = get_mmx_reg_dest(I_REG(flags));
        psubw(dest, result_ptr, 4);
        break;
    case PSUBW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest = get_sse_reg_dest(I_REG(flags));
        psubw(dest, result_ptr, 8);
        break;
    case PSUBD_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest = get_mmx_reg_dest(I_REG(flags));
        psubd(dest, result_ptr, 2);
        break;
    case PSUBD_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest = get_sse_reg_dest(I_REG(flags));
        psubd(dest, result_ptr, 4);
        break;
    case PSUBQ_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest = get_mmx_reg_dest(I_REG(flags));
        psubq(dest, result_ptr, 1);
        break;
    case PSUBQ_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest = get_sse_reg_dest(I_REG(flags));
        psubq(dest, result_ptr, 2);
        break;
    case PADDB_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest = get_mmx_reg_dest(I_REG(flags));
        paddb(dest, result_ptr, 8);
        break;
    case PADDB_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest = get_sse_reg_dest(I_REG(flags));
        paddb(dest, result_ptr, 16);
        break;
    case PADDW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest = get_mmx_reg_dest(I_REG(flags));
        paddw(dest, result_ptr, 4);
        break;
    case PADDW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest = get_sse_reg_dest(I_REG(flags));
        paddw(dest, result_ptr, 8);
        break;
    case PADDD_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest = get_mmx_reg_dest(I_REG(flags));
        paddd(dest, result_ptr, 2);
        break;
    case PADDD_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest = get_sse_reg_dest(I_REG(flags));
        paddd(dest, result_ptr, 4);
        break;
    }
    return 0;
}
int execute_0FC2_C6(struct decoded_instruction* i)
{
    uint32_t flags = i->flags, *dest32;
    uint16_t op, *dest16;
    int imm = i->imm16 >> 8, fp_exception = 0;
    switch (i->imm8 & 15) {
    case CMPPS_XGoXEoIb:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = cmpps(dest32[0], *(float32*)(result_ptr), imm);
        dest32[1] = cmpps(dest32[1], *(float32*)(result_ptr + 4), imm);
        dest32[2] = cmpps(dest32[2], *(float32*)(result_ptr + 8), imm);
        dest32[3] = cmpps(dest32[3], *(float32*)(result_ptr + 12), imm);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CMPSS_XGdXEdIb:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 1, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = cmpps(dest32[0], *(float32*)(result_ptr), imm);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CMPPD_XGoXEoIb:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = dest32[1] = cmppd(*(float64*)(&dest32[0]), *(float64*)(result_ptr), imm);
        dest32[2] = dest32[3] = cmppd(*(float64*)(&dest32[2]), *(float64*)(result_ptr + 8), imm);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CMPSD_XGqXEqIb:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 2, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = dest32[1] = cmppd(*(float64*)(&dest32[0]), *(float64*)(result_ptr), imm);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MOVNTI_EdGd:
        EX(get_reg_write_ptr(flags, i));
        dest32 = get_reg_dest(I_REG(flags));
        *(uint32_t*)result_ptr = *dest32;
        WRITE_BACK();
        break;
    case PINSRW_MGqEdIb:
        CHECK_SSE;
        if (I_OP2(flags))
            op = cpu.reg32[I_RM(flags)];
        else
            cpu_read16(cpu_get_linaddr(flags, i), op, cpu.tlb_shift_read);
        dest16 = get_mmx_reg_dest(I_REG(flags));
        dest16[imm & 3] = op;
        break;
    case PINSRW_XGoEdIb:
        CHECK_SSE;
        if (I_OP2(flags))
            op = cpu.reg32[I_RM(flags)];
        else
            cpu_read16(cpu_get_linaddr(flags, i), op, cpu.tlb_shift_read);
        dest16 = get_sse_reg_dest(I_REG(flags));
        dest16[imm & 7] = op;
        break;
    case PEXTRW_GdMEqIb:
        CHECK_MMX;
        dest16 = get_mmx_reg_dest(I_RM(flags));
        cpu.reg32[I_REG(flags)] = dest16[imm & 3]; // Zero-extend to 32-bits
        break;
    case PEXTRW_GdXEoIb:
        CHECK_SSE;
        dest16 = get_sse_reg_dest(I_RM(flags));
        cpu.reg32[I_REG(flags)] = dest16[imm & 7]; // Zero-extend to 32-bits
        break;
    case SHUFPS_XGoXEoIb:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        shufps(dest32, result_ptr, imm);
        break;
    case SHUFPD_XGoXEoIb:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        shufpd(dest32, result_ptr, imm);
        break;
    }
    return fp_exception;
}
int execute_0F58_5F(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, *dest32;
    float64* dest64;
    int fp_exception = 0;
    switch (i->imm8 & 31) {
    case ADDPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_add(dest32[0], *(float32*)(result_ptr), &status);
        dest32[1] = float32_add(dest32[1], *(float32*)(result_ptr + 4), &status);
        dest32[2] = float32_add(dest32[2], *(float32*)(result_ptr + 8), &status);
        dest32[3] = float32_add(dest32[3], *(float32*)(result_ptr + 12), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case ADDSS_XGdXEd:
        EX(get_sse_read_ptr(flags, i, 1, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_add(dest32[0], *(float32*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case ADDPD_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 0));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_add(dest64[0], *(float64*)(result_ptr), &status);
        dest64[1] = float64_add(dest64[1], *(float64*)(result_ptr + 8), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case ADDSD_XGqXEq:
        EX(get_sse_read_ptr(flags, i, 2, 0));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_add(dest64[0], *(float64*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MULPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_mul(dest32[0], *(float32*)(result_ptr), &status);
        dest32[1] = float32_mul(dest32[1], *(float32*)(result_ptr + 4), &status);
        dest32[2] = float32_mul(dest32[2], *(float32*)(result_ptr + 8), &status);
        dest32[3] = float32_mul(dest32[3], *(float32*)(result_ptr + 12), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MULSS_XGdXEd:
        EX(get_sse_read_ptr(flags, i, 1, 0));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_mul(dest32[0], *(float32*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MULPD_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 0));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_mul(dest64[0], *(float64*)(result_ptr), &status);
        dest64[1] = float64_mul(dest64[1], *(float64*)(result_ptr + 8), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MULSD_XGqXEq:
        EX(get_sse_read_ptr(flags, i, 2, 0));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_mul(dest64[0], *(float64*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTPS2PD_XGoXEo: { // float --> double
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest64 = get_sse_reg_dest(I_REG(flags));
        // The second dword might get overwritten by the first.
        float32 temp = *(float32*)(result_ptr + 4);
        dest64[0] = float32_to_float64(*(float32*)result_ptr, &status);
        dest64[1] = float32_to_float64(temp, &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    }
    case CVTPD2PS_XGoXEo: // double --> float
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float64_to_float32(*(float64*)(result_ptr), &status);
        dest32[1] = float64_to_float32(*(float64*)(result_ptr + 8), &status);
        dest32[2] = 0;
        dest32[3] = 0;
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTSS2SD_XGoXEd: // float --> double
        EX(get_sse_read_ptr(flags, i, 1, 0));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float32_to_float64(*(float32*)result_ptr, &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTSD2SS_XGoXEq: // double --> float
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float64_to_float32(*(float64*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTDQ2PS_XGoXEo: // int32 --> float
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = int32_to_float32(*(int32_t*)(result_ptr), &status);
        dest32[1] = int32_to_float32(*(int32_t*)(result_ptr + 4), &status);
        dest32[2] = int32_to_float32(*(int32_t*)(result_ptr + 8), &status);
        dest32[3] = int32_to_float32(*(int32_t*)(result_ptr + 12), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTPS2DQ_XGoXEo: // float --> int32
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_to_int32(*(float32*)(result_ptr), &status);
        dest32[1] = float32_to_int32(*(float32*)(result_ptr + 4), &status);
        dest32[2] = float32_to_int32(*(float32*)(result_ptr + 8), &status);
        dest32[3] = float32_to_int32(*(float32*)(result_ptr + 12), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTTPS2DQ_XGoXEo: // float --> int32
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_to_int32_round_to_zero(*(float32*)(result_ptr), &status);
        dest32[1] = float32_to_int32_round_to_zero(*(float32*)(result_ptr + 4), &status);
        dest32[2] = float32_to_int32_round_to_zero(*(float32*)(result_ptr + 8), &status);
        dest32[3] = float32_to_int32_round_to_zero(*(float32*)(result_ptr + 12), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case SUBPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_sub(dest32[0], *(float32*)(result_ptr), &status);
        dest32[1] = float32_sub(dest32[1], *(float32*)(result_ptr + 4), &status);
        dest32[2] = float32_sub(dest32[2], *(float32*)(result_ptr + 8), &status);
        dest32[3] = float32_sub(dest32[3], *(float32*)(result_ptr + 12), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case SUBSS_XGdXEd:
        EX(get_sse_read_ptr(flags, i, 1, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_sub(dest32[0], *(float32*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case SUBPD_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_sub(dest64[0], *(float64*)(result_ptr), &status);
        dest64[1] = float64_sub(dest64[1], *(float64*)(result_ptr + 8), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case SUBSD_XGqXEq:
        EX(get_sse_read_ptr(flags, i, 2, 0));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_sub(dest64[0], *(float64*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MINPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_min(dest32[0], *(float32*)(result_ptr), &status);
        dest32[1] = float32_min(dest32[1], *(float32*)(result_ptr + 4), &status);
        dest32[2] = float32_min(dest32[2], *(float32*)(result_ptr + 8), &status);
        dest32[3] = float32_min(dest32[3], *(float32*)(result_ptr + 12), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MINSS_XGdXEd:
        EX(get_sse_read_ptr(flags, i, 1, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_min(dest32[0], *(float32*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MINPD_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_min(dest64[0], *(float64*)(result_ptr), &status);
        dest64[1] = float64_min(dest64[1], *(float64*)(result_ptr + 8), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MINSD_XGqXEq:
        EX(get_sse_read_ptr(flags, i, 2, 0));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_min(dest64[0], *(float64*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case DIVPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_div(dest32[0], *(float32*)(result_ptr), &status);
        dest32[1] = float32_div(dest32[1], *(float32*)(result_ptr + 4), &status);
        dest32[2] = float32_div(dest32[2], *(float32*)(result_ptr + 8), &status);
        dest32[3] = float32_div(dest32[3], *(float32*)(result_ptr + 12), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case DIVSS_XGdXEd:
        EX(get_sse_read_ptr(flags, i, 1, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_div(dest32[0], *(float32*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case DIVPD_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_div(dest64[0], *(float64*)(result_ptr), &status);
        dest64[1] = float64_div(dest64[1], *(float64*)(result_ptr + 8), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case DIVSD_XGqXEq:
        EX(get_sse_read_ptr(flags, i, 2, 0));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_div(dest64[0], *(float64*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MAXPS_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_max(dest32[0], *(float32*)(result_ptr), &status);
        dest32[1] = float32_max(dest32[1], *(float32*)(result_ptr + 4), &status);
        dest32[2] = float32_max(dest32[2], *(float32*)(result_ptr + 8), &status);
        dest32[3] = float32_max(dest32[3], *(float32*)(result_ptr + 12), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MAXSS_XGdXEd:
        EX(get_sse_read_ptr(flags, i, 1, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float32_max(dest32[0], *(float32*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MAXPD_XGoXEo:
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_max(dest64[0], *(float64*)(result_ptr), &status);
        dest64[1] = float64_max(dest64[1], *(float64*)(result_ptr + 8), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case MAXSD_XGqXEq:
        EX(get_sse_read_ptr(flags, i, 2, 0));
        dest64 = get_sse_reg_dest(I_REG(flags));
        dest64[0] = float64_max(dest64[0], *(float64*)(result_ptr), &status);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    }
    return fp_exception;
}
int execute_0FE0_E7(struct decoded_instruction* i)
{
    uint32_t flags = i->flags, *dest32;
    int fp_exception = 0;
    switch (i->imm8 & 31) {
    case PAVGB_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pavgb(dest32, result_ptr, 8);
        break;
    case PAVGB_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pavgb(dest32, result_ptr, 16);
        break;
    case PSRAW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSRAW, 4, get_shift(result_ptr, 8));
        break;
    case PSRAD_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSRAD, 4, get_shift(result_ptr, 8));
        break;
    case PSRAW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSRAW, 8, get_shift(result_ptr, 16));
        break;
    case PSRAD_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSRAD, 8, get_shift(result_ptr, 16));
        break;
    case PAVGW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pavgw(dest32, result_ptr, 4);
        break;
    case PAVGW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pavgw(dest32, result_ptr, 8);
        break;
    case PMULHUW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pmuluw(dest32, result_ptr, 4, 16);
        break;
    case PMULHUW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pmuluw(dest32, result_ptr, 8, 16);
        break;
    case PMULHW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pmullw((uint16_t*)dest32, result_ptr, 4, 16);
        break;
    case PMULHW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pmullw((uint16_t*)dest32, result_ptr, 8, 16);
        break;
    case CVTPD2DQ_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float64_to_int32(*(float64*)result_ptr, &status);
        dest32[1] = float64_to_int32(*(float64*)(result_ptr + 8), &status);
        dest32[2] = 0;
        dest32[3] = 0;
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTTPD2DQ_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = float64_to_int32_round_to_zero(*(float64*)result_ptr, &status);
        dest32[1] = float64_to_int32_round_to_zero(*(float64*)(result_ptr + 8), &status);
        dest32[2] = 0;
        dest32[3] = 0;
        fp_exception = cpu_sse_handle_exceptions();
        break;
    case CVTDQ2PD_XGoXEq: {
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        uint32_t dword1 = *(uint32_t *)result_ptr, dword2 = *(uint32_t *)(result_ptr + 4);
        *(uint64_t*)(&dest32[0]) = int32_to_float64(dword1);
        *(uint64_t*)(&dest32[2]) = int32_to_float64(dword2);
        fp_exception = cpu_sse_handle_exceptions();
        break;
    }
    case MOVNTQ_MEqMGq:
        CHECK_MMX;
        EX(get_mmx_write_ptr(flags, i, 2));
        dest32 = get_mmx_reg_src(I_REG(flags));
        *(uint32_t*)(result_ptr) = dest32[0];
        *(uint32_t*)(result_ptr + 4) = dest32[1];
        WRITE_BACK();
        break;
    case MOVNTDQ_XEoXGo:
        CHECK_SSE;
        EX(get_sse_write_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)(result_ptr) = dest32[0];
        *(uint32_t*)(result_ptr + 4) = dest32[1];
        *(uint32_t*)(result_ptr + 8) = dest32[2];
        *(uint32_t*)(result_ptr + 12) = dest32[3];
        WRITE_BACK();
        break;
    }
    return fp_exception;
}
int execute_0FF1_F7(struct decoded_instruction* i)
{
    uint32_t flags = i->flags, *dest32, linaddr;
    uint8_t *mask, *src8;
    switch (i->imm8) {
    case PSLLW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_src(I_REG(flags));
        pshift(dest32, PSHIFT_PSLLW, 4, get_shift(result_ptr, 8));
        break;
    case PSLLW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSLLW, 8, get_shift(result_ptr, 8));
        break;
    case PSLLD_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_src(I_REG(flags));
        pshift(dest32, PSHIFT_PSLLD, 4, get_shift(result_ptr, 8));
        break;
    case PSLLD_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSLLD, 8, get_shift(result_ptr, 8));
        break;
    case PSLLQ_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_src(I_REG(flags));
        pshift(dest32, PSHIFT_PSLLQ, 4, get_shift(result_ptr, 8));
        break;
    case PSLLQ_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pshift(dest32, PSHIFT_PSLLQ, 8, get_shift(result_ptr, 8));
        break;
    case PMULLUDQ_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pmuludq(dest32, result_ptr, 2);
        break;
    case PMULLUDQ_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pmuludq(dest32, result_ptr, 4);
        break;
    case PMADDWD_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        pmaddwd(dest32, result_ptr, 2);
        break;
    case PMADDWD_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        pmaddwd(dest32, result_ptr, 4);
        break;
    case PSADBW_MGqMEq:
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        dest32 = get_mmx_reg_dest(I_REG(flags));
        psadbw(dest32, result_ptr, 1);
        break;
    case PSADBW_XGoXEo:
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        psadbw(dest32, result_ptr, 2);
        break;
    case MASKMOVQ_MEqMGq:
        CHECK_MMX;
        linaddr = cpu.reg32[EDI] + cpu.seg_base[I_SEG_BASE(flags)];
        src8 = get_mmx_reg_src(I_REG(flags));
        mask = get_mmx_reg_src(I_RM(flags));
        for (int i = 0; i < 8; i++) {
            if (mask[i] & 0x80)
                cpu_write8(linaddr + i, src8[i], cpu.tlb_shift_write);
        }
        break;
    case MASKMOVDQ_XEoXGo:
        CHECK_SSE;
        linaddr = cpu.reg32[EDI] + cpu.seg_base[I_SEG_BASE(flags)];
        src8 = get_sse_reg_dest(I_REG(flags));
        mask = get_sse_reg_dest(I_RM(flags));
        for (int i = 0; i < 16; i++) {
            if (mask[i] & 0x80)
                cpu_write8(linaddr + i, src8[i], cpu.tlb_shift_write);
        }
        break;
    }
    return 0;
}
int cpu_emms(void)
{
    CHECK_MMX;
    fpu.tag_word = 0xFFFF;
    return 0;
}

// SSE3
int execute_0F7C_7D(struct decoded_instruction* i)
{
    uint32_t flags = i->flags, *dest32;
    union {
        uint32_t b32[4];
        uint64_t b64[2];
    } temp;
    CHECK_SSE;
    EX(get_sse_read_ptr(flags, i, 4, 1)); // alignment forced
    dest32 = get_sse_reg_dest(I_REG(flags));
    switch (i->imm8 & 3) {
    case HADDPD_XGoXEo:
        // dest64[0] = dest64[0] + dest64[1]
        // dest64[1] = src64[0] + src64[1]
        // dest64[2] = dest64[2] + dest64[3]
        // dest64[3] = src64[2] + src64[2]
        temp.b64[0] = float64_add(*(float64*)(&dest32[0]), *(float64*)(&dest32[2]), &status);
        temp.b64[1] = float64_add(*(float64*)(result_ptr), *(float64*)(result_ptr + 8), &status);
        break;
    case HADDPS_XGoXEo:
        temp.b32[0] = float32_add(dest32[0], dest32[1], &status);
        temp.b32[1] = float32_add(dest32[2], dest32[3], &status);
        temp.b32[2] = float32_add(*(float32*)(result_ptr), *(float32*)(result_ptr + 4), &status);
        temp.b32[3] = float32_add(*(float32*)(result_ptr + 8), *(float32*)(result_ptr + 12), &status);
        break;
    case HSUBPD_XGoXEo:
        temp.b64[0] = float64_sub(*(float64*)(&dest32[0]), *(float64*)(&dest32[2]), &status);
        temp.b64[1] = float64_sub(*(float64*)(result_ptr), *(float64*)(result_ptr + 8), &status);
        break;
    case HSUBPS_XGoXEo:
        temp.b32[0] = float32_sub(dest32[0], dest32[1], &status);
        temp.b32[1] = float32_sub(dest32[2], dest32[3], &status);
        temp.b32[2] = float32_sub(*(float32*)(result_ptr), *(float32*)(result_ptr + 4), &status);
        temp.b32[3] = float32_sub(*(float32*)(result_ptr + 8), *(float32*)(result_ptr + 12), &status);
        break;
    }
    memcpy(dest32, temp.b32, 16);
    return cpu_sse_handle_exceptions();
}

// SSSE3
int execute_0F38(struct decoded_instruction* i)
{
    uint32_t flags = i->flags;
    switch (i->imm8) {
    case 0: // PSHUFB
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        pshufb(get_mmx_reg_dest(I_REG(flags)), result_ptr, 8);
        break;
    case 0x1C: // PABSB
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        pabsb(get_mmx_reg_dest(I_REG(flags)), result_ptr, 8);
        break;
    case 0x1D: // PABSW
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        pabsw(get_mmx_reg_dest(I_REG(flags)), result_ptr, 4);
        break;
    case 0x1E: // PABSD
        CHECK_MMX;
        EX(get_mmx_read_ptr(flags, i, 2));
        pabsd(get_mmx_reg_dest(I_REG(flags)), result_ptr, 2);
        break;
    default:
        CPU_FATAL("TODO: implement 0F 38 %02x", i->imm8);
    }
    return 0;
}
int execute_660F38(struct decoded_instruction* i)
{
    uint32_t flags = i->flags;
    switch (i->imm8) {
    case 0: // PSHUFB
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 0)); // no unalign excep
        pshufb(get_sse_reg_dest(I_REG(flags)), result_ptr, 16);
        break;
    case 0x1C: // PABSB
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 0)); // no unalign excep
        pabsb(get_sse_reg_dest(I_REG(flags)), result_ptr, 16);
        break;
    case 0x1D: // PABSW
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 0)); // no unalign excep
        pabsw(get_sse_reg_dest(I_REG(flags)), result_ptr, 8);
        break;
    case 0x1E: // PABSD
        CHECK_SSE;
        EX(get_sse_read_ptr(flags, i, 4, 0)); // no unalign excep
        pabsd(get_sse_reg_dest(I_REG(flags)), result_ptr, 4);
        break;
    default:
        CPU_FATAL("TODO: implement 660F 38 %02x", i->imm8);
    }
    return 0;
}