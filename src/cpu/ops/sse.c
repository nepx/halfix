// SSE operations
// Note: We use the I_OP2 to indicate whether the instruction is register/memory.
#include "cpu/cpu.h"
#include "cpu/fpu.h"
#include "cpu/instrument.h"
#include "cpu/opcodes.h"
#include "cpu/ops.h"
#include "io.h"
#include <string.h>
#define EXCEPTION_HANDLER return 1

// Uncomment below line for faster (but less accurate) floating point emulation.
//#define FAST_FLOAT

#ifndef FAST_FLOAT
#include "softfloat/softfloat.h"
#define float32_t float32
#define float64_t float64
static float_status_t status;
#else
#include <math.h> 
#define float32_t float
#define float64_t double
#endif

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
#define CHECK_SSE            \
    if (cpu_sse_exception()) \
    EXCEP()

void cpu_update_mxcsr(void)
{
// Regenerates the data inside of "status"
#ifndef FAST_FLOAT
    status.float_exception_flags = 0;
    status.float_nan_handling_mode = float_first_operand_nan;
    status.float_rounding_mode = cpu.mxcsr >> 13 & 3;
    status.flush_underflow_to_zero = (cpu.mxcsr >> 15) & (cpu.mxcsr >> 11) & 1;
    status.float_exception_masks = cpu.mxcsr >> 7 & 63;
    status.float_suppress_exception = 0;
    status.denormals_are_zeros = cpu.mxcsr >> 6 & 1;
#endif
}
int cpu_sse_handle_exceptions(void)
{
#ifdef FAST_FLOAT
    return 0;
#endif
    // Check if any of the exceptions are masked.

    int flags = status.float_exception_flags, unmasked = flags & ~status.float_exception_masks;
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

// Transfers 128 bits in one operation
void cpu_mov128(void* dest, void* src)
{
    uint32_t *dest32 = dest, *src32 = src;
    dest32[0] = src32[0];
    dest32[1] = src32[1];
    dest32[2] = src32[2];
    dest32[3] = src32[3];
}
// Transfers 64 bits in one operation
void cpu_mov64(void* dest, void* src)
{
    uint32_t *dest32 = dest, *src32 = src;
    dest32[0] = src32[0];
    dest32[1] = src32[1];
}

// A procedure optimized for 128-bit stores.
// Assumes the address is aligned to 16 bytes
int cpu_write128(uint32_t linaddr, void* x)
{
    int shift = cpu.tlb_shift_write;
    uint32_t* data32 = x;
    uint8_t tag = cpu.tlb_tags[linaddr >> 12] >> shift;
    if (tag & 2) {
        if (cpu_mmu_translate(linaddr, shift))
            return 1;
        tag = cpu.tlb_tags[linaddr >> 12] >> shift;
    }
    uint32_t* host_ptr = cpu.tlb[linaddr >> 12] + linaddr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    // Check for MMIO areas
    if ((phys >= 0xA0000 && phys < 0xC0000) || (phys >= cpu.memory_size)) {
        io_handle_mmio_write(phys, data32[0], 2);
        io_handle_mmio_write(phys + 4, data32[1], 2);
        io_handle_mmio_write(phys + 8, data32[2], 2);
        io_handle_mmio_write(phys + 12, data32[3], 2);
        return 0;
    }

    // Actual write
    host_ptr[0] = data32[0];
    host_ptr[1] = data32[1];
    host_ptr[2] = data32[2];
    host_ptr[3] = data32[3];

    return 0;
}

// A procedure optimized for 128-bit loads.
// Assumes the address is aligned to 16 bytes
int cpu_read128(uint32_t linaddr, void* x)
{
    int shift = cpu.tlb_shift_read;
    uint32_t* data32 = x;
    uint8_t tag = cpu.tlb_tags[linaddr >> 12] >> shift;
    if (tag & 2) {
        if (cpu_mmu_translate(linaddr, shift))
            return 1;
        tag = cpu.tlb_tags[linaddr >> 12] >> shift;
    }
    uint32_t* host_ptr = cpu.tlb[linaddr >> 12] + linaddr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    // Check for MMIO areas
    if ((phys >= 0xA0000 && phys < 0xC0000) || (phys >= cpu.memory_size)) {
        data32[0] = io_handle_mmio_read(phys, 2);
        data32[1] = io_handle_mmio_read(phys + 4, 2);
        data32[2] = io_handle_mmio_read(phys + 8, 2);
        data32[3] = io_handle_mmio_read(phys + 12, 2);
        return 0;
    }

    // Actual read
    data32[0] = host_ptr[0];
    data32[1] = host_ptr[1];
    data32[2] = host_ptr[2];
    data32[3] = host_ptr[3];

    return 0;
}

// Unaligned reads/writes
int cpu_write128u(uint32_t linaddr, void* x)
{
    if (linaddr & 15) {
        uint32_t* data32 = x;
        cpu_write32(linaddr, data32[0], cpu.tlb_shift_write);
        cpu_write32(linaddr + 4, data32[1], cpu.tlb_shift_write);
        cpu_write32(linaddr + 8, data32[2], cpu.tlb_shift_write);
        cpu_write32(linaddr + 12, data32[3], cpu.tlb_shift_write);
        return 0;
    } else
        return cpu_write128(linaddr, x);
}
int cpu_read128u(uint32_t linaddr, void* x)
{
    if (linaddr & 15) {
        uint32_t* data32 = x;
        cpu_read32(linaddr, data32[0], cpu.tlb_shift_read);
        cpu_read32(linaddr + 4, data32[1], cpu.tlb_shift_read);
        cpu_read32(linaddr + 8, data32[2], cpu.tlb_shift_read);
        cpu_read32(linaddr + 12, data32[3], cpu.tlb_shift_read);
        return 0;
    } else
        return cpu_read128(linaddr, x);
}

// A procedure optimized for 64-bit stores.
int cpu_write64(uint32_t linaddr, void* x)
{
    int shift = cpu.tlb_shift_write;
    uint32_t* data32 = x;
    uint8_t tag = cpu.tlb_tags[linaddr >> 12] >> shift;
    if ((tag & 3) | (linaddr & 7)) {
        cpu_write32(linaddr, data32[0], shift);
        cpu_write32(linaddr + 4, data32[1], shift);
        return 0;
    }
    uint32_t* host_ptr = cpu.tlb[linaddr >> 12] + linaddr;
    host_ptr[0] = data32[0];
    host_ptr[1] = data32[1];

    return 0;
}

// A procedure optimized for 64-bit loads.
int cpu_read64(uint32_t linaddr, void* x)
{
    int shift = cpu.tlb_shift_read;
    uint32_t* data32 = x;
    uint8_t tag = cpu.tlb_tags[linaddr >> 12] >> shift;
    if ((tag & 3) | (linaddr & 7)) {
        cpu_read32(linaddr, data32[0], shift);
        cpu_read32(linaddr + 4, data32[1], shift);
        return 0;
    }
    uint32_t* host_ptr = cpu.tlb[linaddr >> 12] + linaddr;
    data32[0] = host_ptr[0];
    data32[1] = host_ptr[1];

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// Pointer functions
///////////////////////////////////////////////////////////////////////////////
// get_ptr128_read: result_ptr contains either a direct pointer to memory or to
//                  temp.d128. 16 bytes of memory at this location are
//                  guaranteed to be valid and contain the exact bytes at linaddr.
//                  Modifying the bytes at this location
//                  may or may not modify actual memory, and you shouldn't be
//                  doing this anyways since the read TLB is used.
// get_ptr128_write: result_ptr contains either a direct pointer to memory or to
//                   temp.d128. 16 bytes of memory at this location are
//                   guaranteed to be writable, but they may or may not contain
//                   valid memory bytes. All 16 bytes must be written or else
//                   memory corruption may occur. After you are done, call the
//                   WRITE_BACK() macro to commit the write to memory.
#define FAST_BRANCHLESS_MASK(addr, i) (addr & ((i << 12 & 65536) - 1))
static inline uint32_t cpu_get_linaddr(uint32_t i, struct decoded_instruction* j)
{
    uint32_t addr = cpu.reg32[I_BASE(i)];
    addr += cpu.reg32[I_INDEX(i)] << (I_SCALE(i));
    addr += j->disp32;
    return FAST_BRANCHLESS_MASK(addr, i) + cpu.seg_base[I_SEG_BASE(i)];
}

static union {
    uint32_t d32;
    uint32_t d128[4];
} temp __attribute__((aligned(16)));
// This is the actual pointer we can read/write.
static void* result_ptr;

// This is the linear address to the memory just in case a write was not aligned.
static uint32_t write_linaddr,
    // Indicate if we need a separate write-back procedure
    write_back;

static int get_ptr128_read(uint32_t flags, struct decoded_instruction* i)
{
    uint32_t linaddr;
    if(I_OP2(flags)){
        result_ptr = &XMM32(I_RM(flags));
        return 0;
    }
    linaddr = cpu_get_linaddr(flags, i);
    if (linaddr & 15)
        EXCEPTION_GP(0);
    int shift = cpu.tlb_shift_read;
    uint8_t tag = cpu.tlb_tags[linaddr >> 12] >> shift;
    if (tag & 2) {
        if (cpu_mmu_translate(linaddr, shift))
            return 1;
        tag = cpu.tlb_tags[linaddr >> 12] >> shift;
    }
    uint32_t* host_ptr = cpu.tlb[linaddr >> 12] + linaddr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    // Check for MMIO areas
    if ((phys >= 0xA0000 && phys < 0xC0000) || (phys >= cpu.memory_size)) {
        temp.d128[0] = io_handle_mmio_read(phys, 2);
        temp.d128[1] = io_handle_mmio_read(phys + 4, 2);
        temp.d128[2] = io_handle_mmio_read(phys + 8, 2);
        temp.d128[3] = io_handle_mmio_read(phys + 12, 2);
        return 0;
    }
    result_ptr = host_ptr;
    return 0;
}

static int get_ptr128_write(uint32_t flags, struct decoded_instruction* i)
{
    uint32_t linaddr;
    if(I_OP2(flags)) {
        write_back = 0;
        result_ptr = &XMM32(I_RM(flags));
        return 0;
    }
    linaddr = cpu_get_linaddr(flags, i);
    // We just need to provide 16 valid bytes to the caller.
    // We can deal with writing later
    if (linaddr & 15) 
        EXCEPTION_GP(0);
    int shift = cpu.tlb_shift_write;
    uint8_t tag = cpu.tlb_tags[linaddr >> 12] >> shift;
    if (tag & 2) {
        if (cpu_mmu_translate(linaddr, shift))
            return 1;
        tag = cpu.tlb_tags[linaddr >> 12] >> shift;
    }
    uint32_t* host_ptr = cpu.tlb[linaddr >> 12] + linaddr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    // Check for MMIO areas
    if ((phys >= 0xA0000 && phys < 0xC0000) || (phys >= cpu.memory_size)) {
        write_back = 1;
        write_linaddr = linaddr;
        result_ptr = temp.d128;
        return 0;
    }
    write_back = 0;
    result_ptr = host_ptr;
    return 0;
}
static inline int commit_write(void)
{
        if (cpu_write128(write_linaddr, temp.d128))
            return 1;
    return 0;
}
#define WRITE_BACK()    \
    if (write_back && commit_write()) \
        EXCEP()
// Actual SSE operations

// XOR
void cpu_sse_xorps(uint32_t* dest, uint32_t* src)
{
    dest[0] ^= src[0];
    dest[1] ^= src[1];
    dest[2] ^= src[2];
    dest[3] ^= src[3];
}

// Interleave bytes, generic function for MMX/SSE
void punpckl(void* dst, void* src, int size, int copysize)
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
void pmullw(uint16_t* dest, uint16_t* src, int wordcount, int shift)
{
    for (int i = 0; i < wordcount; i++) {
        uint32_t result = (uint32_t)(int16_t)dest[i] * (uint32_t)(int16_t)src[i];
        dest[i] = result >> shift;
    }
}
void paddusb(uint8_t* dest, uint8_t* src, int bytecount)
{
    for (int i = 0; i < bytecount; i++) {
        uint8_t result = dest[i] + src[i];
        dest[i] = -(result < dest[i]) | result;
    }
}
void paddusw(uint16_t* dest, uint16_t* src, int wordcount)
{
    for (int i = 0; i < wordcount; i++) {
        uint16_t result = dest[i] + src[i];
        dest[i] = -(result < dest[i]) | result;
    }
}
void paddssb(uint8_t* dest, uint8_t* src, int bytecount)
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
void paddssw(uint16_t* dest, uint16_t* src, int wordcount)
{
    for (int i = 0; i < wordcount; i++) {
        uint16_t x = dest[i], y = src[i], res = x + y;
        x = (x >> 15) + 0x7FFF;
        if ((int16_t)((x ^ y) | ~(y ^ res)) >= 0)
            res = x;
        dest[i] = res;
    }
}

void psubusb(uint8_t* dest, uint8_t* src, int bytecount)
{
    for (int i = 0; i < bytecount; i++) {
        uint8_t result = dest[i] - src[i];
        dest[i] = -(result <= dest[i]) & result;
    }
}
void psubusw(uint16_t* dest, uint16_t* src, int wordcount)
{
    for (int i = 0; i < wordcount; i++) {
        uint16_t result = dest[i] - src[i];
        dest[i] = -(result <= dest[i]) & result;
    }
}
void psubssb(uint8_t* dest, uint8_t* src, int bytecount)
{
    for (int i = 0; i < bytecount; i++) {
        uint8_t x = dest[i], y = src[i], res = x - y;
        x = (x >> 7) + 0x7FFF;
        if ((int8_t)((x ^ y) & (x ^ res)) < 0)
            res = x;
        dest[i] = res;
    }
}
void psubssw(uint16_t* dest, uint16_t* src, int wordcount)
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
void punpckh(void* dst, void* src, int size, int copysize)
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
void cpu_psraw(uint16_t* a, int shift, int mask, int wordcount)
{
    // SAR but with MMX/SSE operands
    for (int i = 0; i < wordcount; i++)
        a[i] = (int16_t)a[i] >> shift & mask;
}
void cpu_psrlw(uint16_t* a, int shift, int mask, int wordcount)
{
    // SHR but with MMX/SSE operands
    for (int i = 0; i < wordcount; i++)
        a[i] = a[i] >> shift & mask;
}
void cpu_psllw(uint16_t* a, int shift, int mask, int wordcount)
{
    // SHL but with MMX/SSE operands
    for (int i = 0; i < wordcount; i++)
        a[i] = a[i] << shift & mask;
}
void cpu_psrad(uint32_t* a, int shift, int mask, int dwordcount)
{
    for (int i = 0; i < dwordcount; i++)
        a[i] = (int32_t)a[i] >> shift & mask;
}
void cpu_psrld(uint32_t* a, int shift, int mask, int dwordcount)
{
    for (int i = 0; i < dwordcount; i++)
        a[i] = a[i] >> shift & mask;
}
void cpu_pslld(uint32_t* a, int shift, int mask, int dwordcount)
{
    for (int i = 0; i < dwordcount; i++)
        a[i] = a[i] << shift & mask;
}
void cpu_psraq(uint64_t* a, int shift, int mask, int qwordcount)
{
    for (int i = 0; i < qwordcount; i++)
        a[i] = (int64_t)a[i] >> shift & mask;
}
void cpu_psrlq(uint64_t* a, int shift, int mask, int qwordcount)
{
    for (int i = 0; i < qwordcount; i++)
        a[i] = a[i] >> shift & mask;
}
void cpu_psllq(uint64_t* a, int shift, int mask, int qwordcount)
{
    for (int i = 0; i < qwordcount; i++)
        a[i] = a[i] << shift & mask;
}
static void cpu_pslldq(uint64_t* a, int shift, int mask)
{
    if (mask == 0) {
        a[0] = 0;
        a[1] = 0;
        return;
    }
    // This is a 128 bit SHL shift for xmm registers only
    a[0] <<= shift; // Bottom bits should be 0
    a[0] |= a[1] >> (64L - shift);
    a[1] <<= shift;
}
static void cpu_psrldq(uint64_t* a, int shift, int mask)
{
    if (mask == 0) {
        a[0] = 0;
        a[1] = 0;
        return;
    }
    // This is a 128 bit SHR shift for xmm registers only
    a[1] >>= shift;
    a[1] |= a[0] << (64L - shift);
    a[0] >>= shift;
}
static inline uint8_t pack_i16_to_u8(uint16_t x)
{
    if (x >= 0x100) {
        if (x & 0x8000)
            return 0;
        return 0xFF;
    }
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
void packuswb(void* dest, void* src, int wordcount)
{
    uint8_t res[16];
    uint16_t *dest16 = dest, *src16 = src;
    for (int i = 0; i < wordcount; i++) {
        res[i] = pack_i16_to_u8(dest16[i]);
        res[i | wordcount] = pack_i16_to_u8(src16[i]);
    }
    memcpy(dest, res, wordcount << 1);
}
void packsswb(void* dest, void* src, int wordcount)
{
    uint8_t res[16];
    uint16_t *dest16 = dest, *src16 = src;
    for (int i = 0; i < wordcount; i++) {
        res[i] = pack_i16_to_i8(dest16[i]);
        res[i | wordcount] = pack_i16_to_i8(src16[i]);
    }
    memcpy(dest, res, wordcount << 1);
}
void packssdw(void* dest, void* src, int dwordcount)
{
    uint16_t res[8];
    uint32_t *dest32 = dest, *src32 = src;
    for (int i = 0; i < dwordcount; i++) {
        res[i] = pack_i32_to_i16(dest32[i]);
        res[i | dwordcount] = pack_i32_to_i16(src32[i]);
    }
    memcpy(dest, res, dwordcount << 2);
}
void pshuf(void* dest, void* src, int imm, int shift)
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
void pmaddwd(void* dest, void* src, int dwordcount)
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
void paddb(uint8_t* dest, uint8_t* src, int bytecount)
{
    if (dest == src) // Faster alternative -- requires just a RMW SHL instead of a MOV from src[i] and a RMW add to dst[i]
        for (int i = 0; i < bytecount; i++)
            dest[i] <<= 1;
    else
        for (int i = 0; i < bytecount; i++)
            dest[i] += src[i];
}
void paddw(uint16_t* dest, uint16_t* src, int wordcount)
{
    if (dest == src)
        for (int i = 0; i < wordcount; i++)
            dest[i] <<= 1;
    else
        for (int i = 0; i < wordcount; i++)
            dest[i] += src[i];
}
void paddd(uint32_t* dest, uint32_t* src, int dwordcount)
{
    if (dest == src)
        for (int i = 0; i < dwordcount; i++)
            dest[i] <<= 1;
    else
        for (int i = 0; i < dwordcount; i++)
            dest[i] += src[i];
}
void paddq(uint64_t* dest, uint64_t* src, int qwordcount)
{
    if (dest == src)
        for (int i = 0; i < qwordcount; i++)
            dest[i] <<= 1;
    else
        for (int i = 0; i < qwordcount; i++)
            dest[i] += src[i];
}

void psubb(uint8_t* dest, uint8_t* src, int bytecount)
{
    if (dest == src)
        for (int i = 0; i < bytecount; i++)
            dest[i] >>= 1;
    else
        for (int i = 0; i < bytecount; i++)
            dest[i] -= src[i];
}
void psubw(uint16_t* dest, uint16_t* src, int wordcount)
{
    if (dest == src)
        for (int i = 0; i < wordcount; i++)
            dest[i] >>= 1;
    else
        for (int i = 0; i < wordcount; i++)
            dest[i] -= src[i];
}
void psubd(uint32_t* dest, uint32_t* src, int dwordcount)
{
    if (dest == src)
        for (int i = 0; i < dwordcount; i++)
            dest[i] >>= 1;
    else
        for (int i = 0; i < dwordcount; i++)
            dest[i] -= src[i];
}
void psubq(uint64_t* dest, uint64_t* src, int qwordcount)
{
    if (dest == src)
        for (int i = 0; i < qwordcount; i++)
            dest[i] >>= 1;
    else
        for (int i = 0; i < qwordcount; i++)
            dest[i] -= src[i];
}
void pandn(uint32_t* dest, uint32_t* src, int dwordcount)
{
    for (int i = 0; i < dwordcount; i++)
        dest[i] = ~dest[i] & src[i];
}
int cvt_f_to_i32(void* dest, void* src, int dwordcount, int truncate){
#ifdef FAST_FLOAT
    uint32_t* dest32 = dest;
    float* src32 = src;
    for (int i = 0; i < dwordcount; i++) {
        if(truncate)
            dest32[i] = (int32_t)trunc(src32[i]);
        else
            dest32[i] = (int32_t)src32[i];
    }
#else
    uint32_t* dest32 = dest;
    float32* src32 = src;
    for (int i = 0; i < dwordcount; i++) {
        if(truncate)
        dest32[i] = float32_to_int32(src32[i], &status);
        else
        dest32[i] = float32_to_int32_round_to_zero(src32[i], &status);
    }
    return cpu_sse_handle_exceptions();
#endif
}
int cvt_d_to_i32(void* dest, void* src, int qwordcount, int truncate){
#ifdef FAST_FLOAT
    uint32_t* dest32 = dest;
    double* src32 = src;
    for (int i = 0; i < qwordcount; i++) {
        if(truncate)
            dest32[i] = (int32_t)trunc(src32[i]);
        else
            dest32[i] = (int32_t)src32[i];
    }
#else
    uint32_t* dest32 = dest;
    float64* src64 = src;
    for (int i = 0; i < qwordcount; i++){
        if(truncate)
            dest32[i] = float64_to_int32(src64[i], &status);
        else
            dest32[i] = float64_to_int32_round_to_zero(src64[i], &status);
    }
    return cpu_sse_handle_exceptions();
#endif
}
int cvt_i32_to_f(void* dest, void* src, int dwordcount){
#ifdef FAST_FLOAT
    float* dest32 = dest;
    uint32_t* src32 = src;
    for (int i = 0; i < dwordcount; i++)
        dest32[i] = (float)src32[i];
#else
    float32* dest32 = dest;
    int32_t* src32 = src;
    for (int i = 0; i < dwordcount; i++)
        dest32[i] = int32_to_float32(src32[i], &status);
    return cpu_sse_handle_exceptions();
#endif
}
int cvt_i32_to_d(void* dest, void* src, int dwordcount){
#ifdef FAST_FLOAT
    double* dest32 = dest;
    uint32_t* src32 = src;
    for (int i = 0; i < dwordcount; i++)
        dest32[i] = (float)src32[i];
#else
    float64* dest64 = dest;
    int32_t* src32 = src;
    for (int i = 0; i < dwordcount; i++)
        dest64[i] = int32_to_float32(src32[i], &status);
    return cpu_sse_handle_exceptions();
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Actual opcodes
///////////////////////////////////////////////////////////////////////////////

#undef EXCEPTION_HANDLER
#define EXCEPTION_HANDLER EXCEP()

#ifdef INSTRUMENT
#define INSTRUMENT_INSN() cpu_instrument_execute()
#else
#define INSTRUMENT_INSN() NOP()
#endif

#define NEXT(flags)                 \
    do {                            \
        cpu.phys_eip += flags & 15; \
        INSTRUMENT_INSN();          \
        return i + 1;               \
    } while (1)
#define NEXT2(flags)           \
    do {                       \
        cpu.phys_eip += flags; \
        INSTRUMENT_INSN();     \
        return i + 1;          \
    } while (1)
// Stops the trace and moves onto next one
#define STOP()                  \
    do {                        \
        INSTRUMENT_INSN();      \
        return cpu_get_trace(); \
    } while (0)
#define EXCEP()                 \
    do {                        \
        cpu.cycles_to_run++;    \
        return cpu_get_trace(); \
    } while (0)

#define read128(linaddr, data)      \
    if (cpu_read128(linaddr, data)) \
    EXCEP()
#define write128(linaddr, data)      \
    if (cpu_write128(linaddr, data)) \
    EXCEP()

OPTYPE op_ldmxcsr(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i), mxcsr;
    cpu_read32(linaddr, mxcsr, cpu.tlb_shift_read);
    if (mxcsr & ~MXCSR_MASK)
        EXCEPTION_GP(0);
    cpu.mxcsr = mxcsr;
    cpu_update_mxcsr();
    NEXT(flags);
}
OPTYPE op_stmxcsr(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    cpu_write32(linaddr, cpu.mxcsr, cpu.tlb_shift_read);
    NEXT(flags);
}

OPTYPE op_mfence(struct decoded_instruction* i)
{
    // Does nothing at the moment
    NEXT(i->flags);
}
OPTYPE op_fxsave(struct decoded_instruction* i)
{
    // Does nothing at the moment
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    if (fpu_fxsave(linaddr))
        EXCEP();
    NEXT(flags);
}
OPTYPE op_fxrstor(struct decoded_instruction* i)
{
    // Does nothing at the moment
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    if (fpu_fxrstor(linaddr))
        EXCEP();
    NEXT(flags);
}

// Load/store opcodes
// Note that MOVAPS, MOVDQU, and MOVDQA do the exact same thing if their operands are registers.
// MOVAPS and MOVDQA do the same thing

// Used for MOVAPS, MOVDQU, and MOVDQA
OPTYPE op_mov_v128x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if(get_ptr128_write(flags, i)) EXCEP();
    cpu_mov128(result_ptr, &XMM32(I_REG(flags)));
    WRITE_BACK();
    NEXT(flags);
}
OPTYPE op_mov_x128m128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    if (linaddr & 15)
        EXCEPTION_GP(0);
    if (cpu_read128(linaddr, &XMM32(I_REG(flags))))
        EXCEP();
    NEXT(flags);
}
OPTYPE op_movu_m128x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    if (cpu_write128u(linaddr, &XMM32(I_REG(flags))))
        EXCEP();
    NEXT(flags);
}
OPTYPE op_movu_x128m128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    if (cpu_read128u(linaddr, &XMM32(I_REG(flags))))
        EXCEP();
    NEXT(flags);
}

OPTYPE op_mov_x128v32(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, *xmm = &XMM32(I_REG(flags)), data;
    if(I_OP2(flags)) data = cpu.reg32[I_RM(flags)];
    else cpu_read32(cpu_get_linaddr(flags, i), data, cpu.tlb_shift_read);
    xmm[0] = data;
    xmm[1] = 0;
    xmm[2] = 0;
    xmm[3] = 0;
    NEXT(flags);
}
OPTYPE op_mov_v32x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, data = XMM32(I_REG(flags));
    if(I_OP2(flags)) cpu.reg32[I_RM(flags)] = data;
    else cpu_write32(cpu_get_linaddr(flags, i), data, cpu.tlb_shift_read);
    NEXT(flags);
}

// SSE memory operations
OPTYPE op_xor_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if(get_ptr128_read(flags, i)) EXCEP();
    cpu_sse_xorps(&XMM32(I_REG(flags)), result_ptr);
    NEXT(flags);
}

static inline int getmask(uint32_t* a, unsigned int n)
{
    // Same as a >= n ? 0 : -1
    return -!(a[1] | a[2] | a[3] | (a[0] > (n - 1)));
}
static inline int getmask2(uint32_t a, unsigned int n)
{
    // Same as a >= n ? 0 : -1
    return -(a < n);
}

OPTYPE op_sse_pshift_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, shift;
    if(get_ptr128_read(flags, i)) EXCEP();
    void *x = &XMM32(I_REG(flags)), *maskptr = result_ptr;
    shift = *(uint32_t*)result_ptr;
    switch (i->imm16 >> 8 & 15) {
    case 0: // PSRLW
        cpu_psrlw(x, shift & 15, getmask(maskptr, 16), 8);
        break;
    case 1: // PSRAW
        cpu_psraw(x, shift & 15, getmask(maskptr, 16), 8);
        break;
    case 2: // PSLLW
        cpu_psllw(x, shift & 15, getmask(maskptr, 16), 8);
        break;
    case 3: // PSRLD
        cpu_psrld(x, shift & 31, getmask(maskptr, 32), 4);
        break;
    case 4: // PSRAD
        cpu_psrad(x, shift & 31, getmask(maskptr, 32), 4);
        break;
    case 5: // PSLLD
        cpu_pslld(x, shift & 31, getmask(maskptr, 32), 4);
        break;
    case 6: // PSRLQ
        cpu_psrlq(x, shift & 63, getmask(maskptr, 64), 2);
        break;
    case 7: // PSRAQ
        cpu_psraq(x, shift & 63, getmask(maskptr, 64), 2);
        break;
    case 8: // PSLLQ
        cpu_psllq(x, shift & 63, getmask(maskptr, 64), 2);
        break;
    }
    NEXT(flags);
}

OPTYPE op_sse_pshift_x128i8(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, shift = i->imm8;
    void* x = &XMM32(I_RM(flags));
    switch (i->imm16 >> 8 & 15) {
    case 0: // PSRLW
        cpu_psrlw(x, shift & 15, getmask2(shift, 16), 8);
        break;
    case 1: // PSRAW
        cpu_psraw(x, shift & 15, getmask2(shift, 16), 8);
        break;
    case 2: // PSLLW
        cpu_psllw(x, shift & 15, getmask2(shift, 16), 8);
        break;
    case 3: // PSRLD
        cpu_psrld(x, shift & 31, getmask2(shift, 32), 4);
        break;
    case 4: // PSRAD
        cpu_psrad(x, shift & 31, getmask2(shift, 32), 4);
        break;
    case 5: // PSLLD
        cpu_pslld(x, shift & 31, getmask2(shift, 32), 4);
        break;
    case 6: // PSRLQ
        cpu_psrlq(x, shift & 63, getmask2(shift, 64), 2);
        break;
    case 7: // PSRAQ
        cpu_psraq(x, shift & 63, getmask2(shift, 64), 2);
        break;
    case 8: // PSLLQ
        cpu_psllq(x, shift & 63, getmask2(shift, 64), 2);
        break;
    }
    NEXT(flags);
}

OPTYPE op_sse_pshift128_x128i8(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, shift = i->imm8;
    void* x = &XMM32(I_REG(flags));
    if (i->imm16 >> 8 & 15)
        cpu_pslldq(x, (shift & 15) << 3, shift < 16);
    else
        cpu_psrldq(x, (shift & 15) << 3, shift < 16);
    NEXT(flags);
}

OPTYPE op_sse_punpckl_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if (get_ptr128_read(flags, i))
        EXCEP();
    punpckl(&XMM32(I_REG(flags)), result_ptr, 16, i->imm8);
    NEXT(flags);
}
OPTYPE op_sse_pmullw_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if (get_ptr128_read(flags, i))
        EXCEP();
    pmullw(&XMM16(I_REG(flags)), result_ptr, 8, 0);
    NEXT(flags);
}
OPTYPE op_sse_pmulhw_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if (get_ptr128_read(flags, i))
        EXCEP();
    pmullw(&XMM16(I_REG(flags)), result_ptr, 8, 16);
    NEXT(flags);
}
OPTYPE op_sse_paddsubs_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if(get_ptr128_read(flags, i))
        EXCEP();
    void* dest = &XMM8(I_REG(flags)), *src = result_ptr;
    switch(i->imm8 & 7){
        case 0:
            paddusb(dest, src, 16);
            break;
        case 1: 
            paddusw(dest, src, 8);
            break;
        case 2: 
            paddssb(dest, src, 16);
            break;
        case 3:
            paddusb(dest, src, 8);
            break;
        case 4:
            psubusb(dest, src, 16);
            break;
        case 5: 
            psubusw(dest, src, 8);
            break;
        case 6: 
            psubssb(dest, src, 16);
            break;
        case 7:
            psubusb(dest, src, 8);
            break;
    }
    NEXT(flags);
}

OPTYPE op_sse_punpckh_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if (get_ptr128_read(flags, i))
        EXCEP();
    punpckh(&XMM32(I_REG(flags)), result_ptr, 16, i->imm8);
    NEXT(flags);
}
OPTYPE op_sse_pack_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if (get_ptr128_read(flags, i))
        EXCEP();
    switch(i->imm8 & 3){
        case 0:
            packuswb(&XMM32(I_REG(flags)), result_ptr, 8);
            break;
        case 2:
            packsswb(&XMM32(I_REG(flags)), &XMM32(I_RM(flags)), 8);
            break;
        case 3:
            packssdw(&XMM32(I_REG(flags)), &XMM32(I_RM(flags)), 4);
            break;
    }
    NEXT(flags);
}
OPTYPE op_sse_pshufw_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    // Format of PSHUFW immediate operand:
    //  0...7: Immediate
    //  8...9: Opcode
    if (get_ptr128_read(flags, i))
        EXCEP();
    uint32_t *dest = &XMM32(I_REG(flags)), *src = result_ptr;
    switch (i->imm16 >> 8 & 3) {
    case 2: // PSHUFLW
        pshuf(dest, src, i->imm8, 1);
        dest[2] = src[2];
        dest[3] = src[3];
        break;
    case 3: // PSHUFHW
        dest[0] = src[0];
        dest[1] = src[1];
        pshuf(dest + 2, src + 2, i->imm8, 1);
        break;
    case 1: // PSHUFD
        pshuf(dest, src, i->imm8, 2);
        break;
    }
    NEXT(flags);
}
OPTYPE op_sse_pmaddwd_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if (get_ptr128_read(flags, i))
        EXCEP();
    pmaddwd(&XMM32(I_REG(flags)), result_ptr, 4);
    NEXT(flags);
}
OPTYPE op_sse_padd_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if (get_ptr128_read(flags, i))
        EXCEP();
    void *dest = &XMM32(I_REG(flags)), *src = result_ptr;
    switch (i->imm8 & 3) {
    case 0: // paddb
        paddb(dest, src, 16);
        break;
    case 1: // paddw
        paddw(dest, src, 8);
        break;
    case 2: // paddd
        paddd(dest, src, 4);
        break;
    case 3: // paddq
        paddq(dest, src, 2);
        break;
    }
    NEXT(flags);
}
OPTYPE op_sse_psub_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if (get_ptr128_read(flags, i))
        EXCEP();
    void *dest = &XMM32(I_REG(flags)), *src = result_ptr;
    switch (i->imm8 & 3) {
    case 0: // psubb
        psubb(dest, src, 16);
        break;
    case 1: // psubw
        psubw(dest, src, 8);
        break;
    case 2: // psubd
        psubd(dest, src, 4);
        break;
    case 3: // psubq
        psubq(dest, src, 2);
        break;
    }
    NEXT(flags);
}
// TODO: combine four below ops
OPTYPE op_sse_mov_m64x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    void* src = &XMM32(I_REG(flags));
    if (cpu_write64(cpu_get_linaddr(flags, i), src + i->imm8))
        EXCEP();
    NEXT(flags);
}
OPTYPE op_sse_mov_x64x64(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, *dest = &XMM32(I_REG(flags)), *src = &XMM32(I_RM(flags));
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = 0;
    dest[3] = 0;
    NEXT(flags);
}
OPTYPE op_mov_x128m64(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, *dest = &XMM32(I_REG(flags));
    if (cpu_read64(cpu_get_linaddr(flags, i), dest))
        EXCEP();
    dest[2] = 0;
    dest[3] = 0;
    NEXT(flags);
}
OPTYPE op_mov_x128x64(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, *dest = &XMM32(I_REG(flags)), *src = &XMM32(I_RM(flags));
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = 0;
    dest[3] = 0;
    NEXT(flags);
}
OPTYPE op_sse_pandn_x128v128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if (get_ptr128_read(flags, i))
        EXCEP();
    pandn(&XMM32(I_REG(flags)), result_ptr, 4);
    NEXT(flags);
}