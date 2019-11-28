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
        cpu_read32(linaddr, data32[0], cpu.tlb_shift_write);
        cpu_read32(linaddr + 4, data32[1], cpu.tlb_shift_write);
        cpu_read32(linaddr + 8, data32[2], cpu.tlb_shift_write);
        cpu_read32(linaddr + 12, data32[3], cpu.tlb_shift_write);
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
        cpu_write32(linaddr, data32[1], shift);
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
        cpu_read32(linaddr, data32[1], shift);
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
static union {
    uint32_t d32;
    uint32_t d128[4];
} temp;
// This is the actual pointer we can read/write.
static void* result_ptr;

#if 0
// This is the linear address to the memory just in case a write was not aligned.
static uint32_t write_linaddr,
    // Indicate if we need a separate write-back procedure
    write_back;
#endif

static int get_ptr128_read(uint32_t linaddr)
{
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
        temp.d128[0] = io_handle_mmio_read(phys, 2);
        temp.d128[1] = io_handle_mmio_read(phys + 4, 2);
        temp.d128[2] = io_handle_mmio_read(phys + 8, 2);
        temp.d128[3] = io_handle_mmio_read(phys + 12, 2);
        return 0;
    }
    result_ptr = host_ptr;
    return 0;
}

#if 0
static int get_ptr128_write(uint32_t linaddr)
{
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
    if (write_back) {
        if (cpu_write128(write_linaddr, temp.d128))
            return 1;
    }
    return 0;
}
#define WRITE_BACK()    \
    if (commit_write()) \
    return 1
#endif
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
    int idx = 0, nidx = 0;
    while (idx < size) {
        for (int i = 0; i < copysize; i++)
            tmp[idx++] = dst8[nidx + i]; // Copy destination bytes
        for (int i = 0; i < copysize; i++)
            tmp[idx++] = src8[nidx + i]; // Copy source bytes
        nidx += copysize;
    }
    memcpy(dst, tmp, size);
}
void pmullw(uint16_t* dest, uint16_t* src, int wordcount){
    for(int i=0;i<wordcount;i++) {
        uint32_t result = (uint32_t)(int16_t)dest[i] * (uint32_t)(int16_t)src[i];
        dest[i] = result;
    }
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

#define FAST_BRANCHLESS_MASK(addr, i) (addr & ((i << 12 & 65536) - 1))
static inline uint32_t cpu_get_linaddr(uint32_t i, struct decoded_instruction* j)
{
    uint32_t addr = cpu.reg32[I_BASE(i)];
    addr += cpu.reg32[I_INDEX(i)] << (I_SCALE(i));
    addr += j->disp32;
    return FAST_BRANCHLESS_MASK(addr, i) + cpu.seg_base[I_SEG_BASE(i)];
}

#define read128(linaddr, data)      \
    if (cpu_read128(linaddr, data)) \
    EXCEP()
#define write128(linaddr, data)      \
    if (cpu_write128(linaddr, data)) \
    EXCEP()

OPTYPE op_ldmxcsr(struct decoded_instruction* i)
{
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i), mxcsr;
    cpu_read32(linaddr, mxcsr, cpu.tlb_shift_read);
    if (mxcsr & ~MXCSR_MASK)
        EXCEPTION_GP(0);
    cpu.mxcsr = mxcsr;
    NEXT(flags);
}
OPTYPE op_stmxcsr(struct decoded_instruction* i)
{
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
OPTYPE op_mov_x128x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    cpu_mov128(&XMM32(I_RM(flags)), &XMM32(I_REG(flags)));
    NEXT(flags);
}
OPTYPE op_mov_m128x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    if (linaddr & 15)
        EXCEPTION_GP(0);
    if (cpu_write128(linaddr, &XMM32(I_REG(flags))))
        EXCEP();
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

OPTYPE op_mov_x128r32(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, *xmm = &XMM32(I_REG(flags));
    xmm[0] = cpu.reg32[I_RM(flags)];
    xmm[1] = 0;
    xmm[2] = 0;
    xmm[3] = 0;
    NEXT(flags);
}
OPTYPE op_mov_x128m32(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i), *xmm = &XMM32(I_REG(flags));
    cpu_read32(linaddr, xmm[0], cpu.tlb_shift_read);
    xmm[1] = 0;
    xmm[2] = 0;
    xmm[3] = 0;
    NEXT(flags);
}
OPTYPE op_mov_r32x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    cpu.reg32[I_RM(flags)] = XMM32(I_REG(flags));
    NEXT(flags);
}
OPTYPE op_mov_m32x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    cpu_write32(linaddr, XMM32(I_REG(flags)), cpu.tlb_shift_write);
    NEXT(flags);
}

// SSE memory operations
OPTYPE op_xor_x128x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    cpu_sse_xorps(&XMM32(I_REG(flags)), &XMM32(I_RM(flags)));
    NEXT(flags);
}
OPTYPE op_xor_x128m128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    if (linaddr & 15)
        EXCEPTION_GP(0);
    if (cpu_read128(linaddr, temp.d128))
        EXCEP();
    cpu_sse_xorps(&XMM32(I_REG(flags)), temp.d128);
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
    return -(a < (n - 1));
}

OPTYPE op_sse_pshift_x128x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, shift = XMM32(I_RM(flags));
    void *x = &XMM32(I_REG(flags)), *maskptr = &XMM32(I_RM(flags));
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
    void* x = &XMM32(I_REG(flags));
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

OPTYPE op_sse_pshift_x128m128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags, shift, linaddr = cpu_get_linaddr(flags, i);
    void *x = &XMM32(I_REG(flags)), *maskptr;
    read128(linaddr, temp.d128);
    shift = temp.d32;
    maskptr = temp.d128;
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

OPTYPE op_sse_punpckl_x128x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    punpckl(&XMM32(I_REG(flags)), &XMM32(I_RM(flags)), 16, i->imm8);
    NEXT(flags);
}
OPTYPE op_sse_punpckl_x128m128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if (get_ptr128_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    punpckl(&XMM32(I_REG(flags)), result_ptr, 16, i->imm8);
    NEXT(flags);
}
OPTYPE op_sse_pmullw_x128x128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    pmullw(&XMM16(I_REG(flags)), &XMM16(I_RM(flags)), 8);
    NEXT(flags);
}
OPTYPE op_sse_pmullw_x128m128(struct decoded_instruction* i)
{
    CHECK_SSE;
    uint32_t flags = i->flags;
    if (get_ptr128_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    pmullw(&XMM16(I_REG(flags)), result_ptr, 8);
    NEXT(flags);
}