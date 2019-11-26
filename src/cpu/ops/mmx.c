// MMX instructions

#include "cpu/cpu.h"
#include "cpu/instruction.h"
#include "cpu/instrument.h"
#include "cpu/opcodes.h"
#include "cpu/sse.h" // Has some useful memory load functions

#define NEED_STRUCT
#include "cpu/fpu.h"
#undef NEED_STRUCT

#define EXCEPTION_HANDLER return 1

#define MM(n) fpu.mm[n].reg

int cpu_mmx_check(void)
{
    if (cpu.cr[0] & CR0_EM)
        EXCEPTION_UD();
    if (cpu.cr[0] & CR0_TS)
        EXCEPTION_NM();

    if (fpu_fwait())
        EXCEPTION_HANDLER;

    return 0;
}

#define CHECK_MMX if(cpu_mmx_check()) EXCEP()

static inline void cpu_mmx_xor(void* a, void* b){
    uint32_t* a32 = a, * b32 = b;
    a32[0] ^= b32[0];
    a32[1] ^= b32[1];
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

#define write64(addr, src)      \
    if (cpu_write64(addr, src)) \
    EXCEP()
#define read64(addr, src)      \
    if (cpu_read64(addr, src)) \
    EXCEP()

static union {
    uint8_t d8;
    uint16_t d16;
    uint32_t d32;
    uint64_t d64;

    uint16_t d16a[4];
} temp;

OPTYPE op_movq_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    cpu_mov64(&MM(I_RM(flags)), &MM(I_REG(flags)));
    NEXT(flags);
}
OPTYPE op_movq_m64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    write64(linaddr, &MM(I_REG(flags)));
    NEXT(flags);
}
OPTYPE op_movq_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    read64(linaddr, &temp.d64);
    MM(I_REG(flags))
        .r64
        = temp.d64;
    NEXT(flags);
}

OPTYPE op_mov_r64r32(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, *mm = MM(I_REG(flags)).r32;
    mm[0] = cpu.reg32[I_RM(flags)];
    mm[1] = 0;
    NEXT(flags);
}
OPTYPE op_mov_r64m32(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i), *mm = MM(I_REG(flags)).r32;
    cpu_read32(linaddr, mm[0], cpu.tlb_shift_read);
    mm[1] = 0;
    NEXT(flags);
}
OPTYPE op_mov_r32r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    cpu.reg32[I_RM(flags)] = MM(I_REG(flags)).r32[0];
    NEXT(flags);
}
OPTYPE op_mov_m32r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    cpu_write32(linaddr, MM(I_REG(flags)).r32[0], cpu.tlb_shift_write);
    NEXT(flags);
}

OPTYPE op_xor_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    read64(linaddr, &temp.d64);
    cpu_mmx_xor(&MM(I_REG(flags)), &temp.d64);
    NEXT(flags);
}
OPTYPE op_xor_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    cpu_mmx_xor(&MM(I_REG(flags)), &MM(I_RM(flags)));
    NEXT(flags);
}

static inline int getmask(uint32_t* a, int n){
    // Same as a >= n ? 0 : -1
    // TODO: for 64-bit platfoms, we can use a 64-bit compare and still have high speed
    return -!(a[1] | (a[0] > (n - 1)));
}
OPTYPE op_psrlw_r64r64(struct decoded_instruction* i){
    CHECK_MMX;
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
    shift = temp.d32 & 15;
    mask = getmask(MM(I_REG(flags)).r32, 16);
    uint16_t* x = MM(I_REG(flags)).r16;
    x[0] = ((uint32_t)x[0] >> shift) & mask;
    x[1] = ((uint32_t)x[1] >> shift) & mask;
    x[2] = ((uint32_t)x[2] >> shift) & mask;
    x[3] = ((uint32_t)x[3] >> shift) & mask;
    NEXT(flags);
}