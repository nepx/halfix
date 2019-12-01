// MMX instructions

#include "cpu/cpu.h"
#include "cpu/instruction.h"
#include "cpu/instrument.h"
#include "cpu/opcodes.h"
#include "io.h"
#include "cpu/sse.h" // Has some useful memory load functions

#define NEED_STRUCT
#include "cpu/fpu.h"
#undef NEED_STRUCT

#define EXCEPTION_HANDLER return 1

#define MM(n) fpu.mm[n].reg

// This must be called before the start of any MMX instruction 
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

static inline void mmx_set_exp(int n){
    fpu.mm[n].dummy = 0xFFFF;
}

// This should be called after memory has been read
static inline void mmx_reset_fpu(void){
    // MMX transitions clear the tag word and reset the stack
    fpu.ftop = 0;
    fpu.tag_word = 0;
}

#define CHECK_MMX INSTRUMENT_MMX; \
            if(cpu_mmx_check()) EXCEP()
#ifdef INSTRUMENT
#define INSTRUMENT_MMX cpu_instrument_pre_fpu()
#else
#define INSTRUMENT_MMX NOP()
#endif

static inline void cpu_mmx_xor(void* a, void* b){
    uint32_t* a32 = a, * b32 = b;
    a32[0] ^= b32[0];
    a32[1] ^= b32[1];
}

static union {
    uint8_t d8;
    uint16_t d16;
    uint32_t d32;
    uint64_t d64;

    uint16_t d16a[4];
    uint32_t d32a[2];
} temp;
// This is the actual pointer we can read/write.
static void* result_ptr;

#if 0
// This is the linear address to the memory just in case a write was not aligned.
static uint32_t write_linaddr,
    // Indicate if we need a separate write-back procedure
    write_back;
#endif

static int get_ptr64_read(uint32_t linaddr)
{
    if (linaddr & 7) {
        if (cpu_read64(linaddr, &temp.d64))
            return 1;
        result_ptr = temp.d32a;
        return 0;
    }
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
        temp.d32a[0] = io_handle_mmio_read(phys, 2);
        temp.d32a[1] = io_handle_mmio_read(phys + 4, 2);
        return 0;
    }
    result_ptr = host_ptr;
    return 0;
}

#if 0
static int get_ptr64_write(uint32_t linaddr)
{
    // We just need to provide 16 valid bytes to the caller.
    if (linaddr & 7) {
        write_back = 1;
        write_linaddr = linaddr;
        result_ptr = temp.d32a;
        return 0;
    }
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
        result_ptr = temp.d32a;
        return 0;
    }
    write_back = 0;
    result_ptr = host_ptr;
    return 0;
}
static inline int commit_write(void)
{
    if (write_back) {
        if (cpu_write64(write_linaddr, temp.d32a))
            return 1;
    }
    return 0;
}
#define WRITE_BACK()    \
    if (commit_write()) \
    return 1
#endif

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

OPTYPE op_mov_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    mmx_reset_fpu();
    uint32_t flags = i->flags;
    cpu_mov64(&MM(I_RM(flags)), &MM(I_REG(flags)));
    mmx_set_exp(I_RM(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mov_m64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    write64(linaddr, &MM(I_REG(flags)));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mov_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    read64(linaddr, &temp.d64);
    MM(I_REG(flags))
        .r64
        = temp.d64;
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}

OPTYPE op_mov_r64r32(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, *mm = MM(I_REG(flags)).r32;
    mm[0] = cpu.reg32[I_RM(flags)];
    mm[1] = 0;
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mov_r64m32(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i), *mm = MM(I_REG(flags)).r32;
    cpu_read32(linaddr, mm[0], cpu.tlb_shift_read);
    mm[1] = 0;
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mov_r32r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    cpu.reg32[I_RM(flags)] = MM(I_REG(flags)).r32[0];
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mov_m32r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    cpu_write32(linaddr, MM(I_REG(flags)).r32[0], cpu.tlb_shift_write);
    mmx_reset_fpu();
    NEXT(flags);
}

OPTYPE op_xor_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    read64(linaddr, &temp.d64);
    cpu_mmx_xor(&MM(I_REG(flags)), &temp.d64);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_xor_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    cpu_mmx_xor(&MM(I_REG(flags)), &MM(I_RM(flags)));
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}

static inline int getmask(uint32_t* a, unsigned int n){
    // Same as a >= n ? 0 : -1
    // TODO: for 64-bit platfoms, we can use a 64-bit compare and still have high speed
    return -!(a[1] | (a[0] > (n - 1)));
}
static inline int getmask2(uint32_t a, unsigned int n){
    // Same as a >= n ? 0 : -1
    return -(a < (n - 1));
}

OPTYPE op_mmx_pshift_r64r64(struct decoded_instruction* i){
    CHECK_MMX;
    uint32_t flags = i->flags, shift = MM(I_RM(flags)).r32[0];
    void* x = &MM(I_REG(flags)), *maskptr = &MM(I_RM(flags));
    switch(i->imm16 >> 8 & 15){
        case 0: // PSRLW
            cpu_psrlw(x, shift & 15, getmask(maskptr, 16), 4);
            break;
        case 1: // PSRAW
            cpu_psraw(x, shift & 15, getmask(maskptr, 16), 4);
            break;
        case 2: // PSLLW
            cpu_psllw(x, shift & 15, getmask(maskptr, 16), 4);
            break;
        case 3: // PSRLD
            cpu_psrld(x, shift & 31, getmask(maskptr, 32), 2);
            break;
        case 4: // PSRAD
            cpu_psrad(x, shift & 31, getmask(maskptr, 32), 2);
            break;
        case 5: // PSLLD
            cpu_pslld(x, shift & 31, getmask(maskptr, 32), 2);
            break;
        case 6: // PSRLQ
            cpu_psrlq(x, shift & 63, getmask(maskptr, 64), 1);
            break;
        case 7: // PSRAQ
            cpu_psraq(x, shift & 63, getmask(maskptr, 64), 1);
            break;
        case 8: // PSLLQ
            cpu_psllq(x, shift & 63, getmask(maskptr, 64), 1);
            break;
    }
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}

OPTYPE op_mmx_pshift_r64i8(struct decoded_instruction* i){
    CHECK_MMX;
    uint32_t flags = i->flags, shift = i->imm8;
    void* x = &MM(I_RM(flags));
    switch(i->imm16 >> 8 & 15){
        case 0: // PSRLW
            cpu_psrlw(x, shift & 15, getmask2(shift, 16), 4);
            break;
        case 1: // PSRAW
            cpu_psraw(x, shift & 15, getmask2(shift, 16), 4);
            break;
        case 2: // PSLLW
            cpu_psllw(x, shift & 15, getmask2(shift, 16), 4);
            break;
        case 3: // PSRLD
            cpu_psrld(x, shift & 31, getmask2(shift, 32), 2);
            break;
        case 4: // PSRAD
            cpu_psrad(x, shift & 31, getmask2(shift, 32), 2);
            break;
        case 5: // PSLLD
            cpu_pslld(x, shift & 31, getmask2(shift, 32), 2);
            break;
        case 6: // PSRLQ
            cpu_psrlq(x, shift & 63, getmask2(shift, 64), 1);
            break;
        case 7: // PSRAQ
            cpu_psraq(x, shift & 63, getmask2(shift, 64), 1);
            break;
        case 8: // PSLLQ
            cpu_psllq(x, shift & 63, getmask2(shift, 64), 1);
            break;
    }
    mmx_set_exp(I_RM(flags));
    mmx_reset_fpu();
    NEXT(flags);
}

OPTYPE op_mmx_pshift_r64m64(struct decoded_instruction* i){
    CHECK_MMX;
    uint32_t flags = i->flags, shift, linaddr = cpu_get_linaddr(flags, i);
    void* x = &MM(I_REG(flags)), *maskptr;
    read64(linaddr, &temp.d64);
    shift = temp.d32;
    maskptr = &temp.d64;
    switch(i->imm16 >> 8 & 15){
        case 0: // PSRLW
            cpu_psrlw(x, shift & 15, getmask(maskptr, 16), 4);
            break;
        case 1: // PSRAW
            cpu_psraw(x, shift & 15, getmask(maskptr, 16), 4);
            break;
        case 2: // PSLLW
            cpu_psllw(x, shift & 15, getmask(maskptr, 16), 4);
            break;
        case 3: // PSRLD
            cpu_psrld(x, shift & 31, getmask(maskptr, 32), 2);
            break;
        case 4: // PSRAD
            cpu_psrad(x, shift & 31, getmask(maskptr, 32), 2);
            break;
        case 5: // PSLLD
            cpu_pslld(x, shift & 31, getmask(maskptr, 32), 2);
            break;
        case 6: // PSRLQ
            cpu_psrlq(x, shift & 63, getmask(maskptr, 64), 1);
            break;
        case 7: // PSRAQ
            cpu_psraq(x, shift & 63, getmask(maskptr, 64), 1);
            break;
        case 8: // PSLLQ
            cpu_psllq(x, shift & 63, getmask(maskptr, 64), 1);
            break;
    }
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}

OPTYPE op_mmx_punpckl_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    punpckl(&MM(I_REG(flags)), result_ptr, 8, i->imm8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_punpckl_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    punpckl(&MM(I_REG(flags)), &MM(I_RM(flags)), 8, i->imm8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pmullw_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    pmullw(MM(I_REG(flags)).r16, MM(I_RM(flags)).r16, 8, 0);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pmullw_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    pmullw(MM(I_REG(flags)).r16, result_ptr, 8, 0);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pmulhw_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    pmullw(MM(I_REG(flags)).r16, MM(I_RM(flags)).r16, 8, 16);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pmulhw_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    pmullw(MM(I_REG(flags)).r16, result_ptr, 8, 16);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}

OPTYPE op_mmx_paddusb_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    paddusb(MM(I_REG(flags)).r8, MM(I_RM(flags)).r8, 16);
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_paddusb_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    paddusb(MM(I_REG(flags)).r8, result_ptr, 16);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_paddusw_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    paddusw(MM(I_REG(flags)).r16, MM(I_RM(flags)).r16, 8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_paddusw_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    paddusw(MM(I_REG(flags)).r16, result_ptr, 8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_paddssb_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    paddssb(MM(I_REG(flags)).r8, MM(I_RM(flags)).r8, 16);
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_paddssb_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    paddssb(MM(I_REG(flags)).r8, result_ptr, 16);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_paddssw_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    paddssw(MM(I_REG(flags)).r16, MM(I_RM(flags)).r16, 8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_paddssw_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    paddssw(MM(I_REG(flags)).r16, result_ptr, 8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}

OPTYPE op_mmx_psubssb_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    psubssb(MM(I_REG(flags)).r8, MM(I_RM(flags)).r8, 16);
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_psubssb_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    psubssb(MM(I_REG(flags)).r8, result_ptr, 16);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_psubssw_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    psubssw(MM(I_REG(flags)).r16, MM(I_RM(flags)).r16, 8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_psubssw_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    psubssw(MM(I_REG(flags)).r16, result_ptr, 8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_punpckh_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    punpckh(&MM(I_REG(flags)), result_ptr, 8, i->imm8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_punpckh_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    punpckh(&MM(I_REG(flags)), &MM(I_RM(flags)), 8, i->imm8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_packuswb_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    packuswb(MM(I_REG(flags)).r32, MM(I_RM(flags)).r32, 4);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_packuswb_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    packuswb(MM(I_REG(flags)).r32, result_ptr, 4);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_packsswb_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    packsswb(MM(I_REG(flags)).r32, MM(I_RM(flags)).r32, 4);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_packsswb_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    packsswb(MM(I_REG(flags)).r32, result_ptr, 4);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_packssdw_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    packssdw(MM(I_REG(flags)).r32, MM(I_RM(flags)).r32, 2);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_packssdw_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    packssdw(MM(I_REG(flags)).r32, result_ptr, 2);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}

OPTYPE op_emms(struct decoded_instruction* i)
{
    CHECK_MMX;
    fpu.tag_word = 0xFFFF;
    NEXT2(i->flags);
}

OPTYPE op_mmx_pshufw_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    printf("pshuf mmx: phys=%08x imm16=%04x\n", cpu.phys_eip, i->imm8);
    pshuf(MM(I_REG(flags)).r32, MM(I_RM(flags)).r32, i->imm8, 1); // Only 16-bit shifts
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pshufw_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    printf("pshuf mmx: phys=%08x imm16=%04x\n", cpu.phys_eip, i->imm8);
    pshuf(MM(I_REG(flags)).r32, result_ptr, i->imm8, 1);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pmaddwd_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    pmaddwd(MM(I_REG(flags)).r32, MM(I_RM(flags)).r32, 2);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pmaddwd_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    pmaddwd(MM(I_REG(flags)).r32, result_ptr, 2);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_padd_r64r64(struct decoded_instruction* i){
    CHECK_MMX;
    uint32_t flags = i->flags;
    void* dest = &MM(I_REG(flags)), *src = &MM(I_RM(flags));
    switch(i->imm8 & 3){
        case 0: // paddb
            paddb(dest, src, 8);
            break;
        case 1: // paddw
            paddw(dest, src, 4);
            break;
        case 2: // paddd
            paddd(dest, src, 2);
            break;
        case 3: // paddq
            paddq(dest, src, 1);
            break;
    }
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_padd_r64m64(struct decoded_instruction* i){
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    void* dest = &MM(I_REG(flags)), *src = result_ptr;
    switch(i->imm8 & 3){
        case 0: // paddb
            paddb(dest, src, 8);
            break;
        case 1: // paddw
            paddw(dest, src, 4);
            break;
        case 2: // paddd
            paddd(dest, src, 2);
            break;
        case 3: // paddq
            paddq(dest, src, 1);
            break;
    }
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}OPTYPE op_mmx_psub_r64r64(struct decoded_instruction* i){
    CHECK_MMX;
    uint32_t flags = i->flags;
    void* dest = &MM(I_REG(flags)), *src = &MM(I_RM(flags));
    switch(i->imm8 & 3){
        case 0: // psubb
            psubb(dest, src, 8);
            break;
        case 1: // psubw
            psubw(dest, src, 4);
            break;
        case 2: // psubd
            psubd(dest, src, 2);
            break;
        case 3: // psubq
            psubq(dest, src, 1);
            break;
    }
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_psub_r64m64(struct decoded_instruction* i){
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    void* dest = &MM(I_REG(flags)), *src = result_ptr;
    switch(i->imm8 & 3){
        case 0: // psubb
            psubb(dest, src, 8);
            break;
        case 1: // psubw
            psubw(dest, src, 4);
            break;
        case 2: // psubd
            psubd(dest, src, 2);
            break;
        case 3: // psubq
            psubq(dest, src, 1);
            break;
    }
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pandn_r64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    pandn(MM(I_REG(flags)).r32, MM(I_RM(flags)).r32, 2);
    NEXT(flags);
}
OPTYPE op_mmx_pandn_r64m64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(cpu_get_linaddr(flags, i)))
        EXCEP();
    pandn(MM(I_REG(flags)).r32, result_ptr, 2);
    NEXT(flags);
}
OPTYPE op_mmx_movq2dq(struct decoded_instruction* i){
    CHECK_MMX;
    uint32_t flags = i->flags, *dest = &XMM32(I_REG(flags)), *src = MM(I_RM(flags)).r32;
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = 0;
    dest[3] = 0;
    NEXT(flags);
}
OPTYPE op_mmx_movdq2q(struct decoded_instruction* i){
    CHECK_MMX;
    uint32_t flags = i->flags, *src = &XMM32(I_REG(flags)), *dest = MM(I_RM(flags)).r32;
    dest[0] = src[0];
    dest[1] = src[1];
    NEXT(flags);
}