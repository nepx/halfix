// SSE operations
#include "cpu/cpu.h"
#include "cpu/fpu.h"
#include "cpu/instrument.h"
#include "cpu/opcodes.h"
#include "cpu/ops.h"
#include "io.h"
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
#define CHECK_SSE if(cpu_sse_exception()) EXCEP()

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

// Actual SSE operations

// XOR
void cpu_sse_xorps(uint32_t* dest, uint32_t* src)
{
    dest[0] ^= src[0];
    dest[1] ^= src[1];
    dest[2] ^= src[2];
    dest[3] ^= src[3];
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

static union {
    uint32_t d32;
    uint32_t d128[4];
} temp;

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

#if 0
static inline int getmask(uint32_t* a, int n){
    // Same as a >= n ? 0 : -1
    return -!(a[1] | a[2] | a[3] | (a[0] > (n - 1)));
}
OPTYPE op_psrlw_r64r64(struct decoded_instruction* i){
    CHECK_SSE;
    uint32_t flags = i->flags, shift = MM(I_REG(flags)).r32[0] & 15, mask = getmask(MM(I_REG(flags)).r32, 16);
    uint16_t* x = MM(I_REG(flags)).r16;
    x[0] = ((uint32_t)x[0] >> shift) & mask;
    x[1] = ((uint32_t)x[1] >> shift) & mask;
    x[2] = ((uint32_t)x[2] >> shift) & mask;
    x[3] = ((uint32_t)x[3] >> shift) & mask;
    NEXT(flags);
}
OPTYPE op_psrlw_r64m64(struct decoded_instruction* i){
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i), shift, mask;
    read64(linaddr, temp.d64);
    shift = temp.d64 & 15;
    mask = -(shift <= 15) & 0xFFFF;
    uint16_t* x = MM(I_REG(flags)).r16;
    x[0] = ((uint32_t)x[0] >> shift) & mask;
    x[1] = ((uint32_t)x[1] >> shift) & mask;
    x[2] = ((uint32_t)x[2] >> shift) & mask;
    x[3] = ((uint32_t)x[3] >> shift) & mask;
    NEXT(flags);
}
#endif