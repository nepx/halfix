// MMX instructions

#include "cpu/cpu.h"
#include "cpu/instruction.h"
#include "cpu/instrument.h"
#include "cpu/opcodes.h"
#include "cpu/sse.h" // Has some useful memory load functions
#include "io.h"

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

static inline void mmx_set_exp(int n)
{
    fpu.mm[n].dummy = 0xFFFF;
}

// This should be called after memory has been read
static inline void mmx_reset_fpu(void)
{
    // MMX transitions clear the tag word and reset the stack
    fpu.ftop = 0;
    fpu.tag_word = 0;
}

#define CHECK_MMX        \
    INSTRUMENT_MMX;      \
    if (cpu_mmx_check()) \
    EXCEP()
#ifdef INSTRUMENT
#define INSTRUMENT_MMX cpu_instrument_pre_fpu()
#else
#define INSTRUMENT_MMX NOP()
#endif

static inline void cpu_mmx_xor(void* a, void* b)
{
    uint32_t *a32 = a, *b32 = b;
    a32[0] ^= b32[0];
    a32[1] ^= b32[1];
}

#define FAST_BRANCHLESS_MASK(addr, i) (addr & ((i << 12 & 65536) - 1))
static inline uint32_t cpu_get_linaddr(uint32_t i, struct decoded_instruction* j)
{
    uint32_t addr = cpu.reg32[I_BASE(i)];
    addr += cpu.reg32[I_INDEX(i)] << (I_SCALE(i));
    addr += j->disp32;
    return FAST_BRANCHLESS_MASK(addr, i) + cpu.seg_base[I_SEG_BASE(i)];
}

static union {
    uint8_t d8;
    uint16_t d16;
    uint32_t d32;
    uint64_t d64;
    uint32_t d128[4];

    uint16_t d16a[4];
    uint32_t d32a[2];
} temp __attribute__((aligned(16)));
// This is the actual pointer we can read/write.
static void* result_ptr;

// This is the linear address to the memory just in case a write was not aligned.
static uint32_t write_linaddr,
    // Indicate if we need a separate write-back procedure
    write_back;

static int get_ptr64_read(uint32_t flags, struct decoded_instruction* i)
{
    uint32_t linaddr;
    if (I_OP2(flags)) {
        result_ptr = &MM(I_RM(flags));
        return 0;
    }
    linaddr = cpu_get_linaddr(flags, i);
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

static int get_ptr64_write(uint32_t flags, struct decoded_instruction* i)
{
    uint32_t linaddr;
    if (I_OP2(flags)) {
        result_ptr = &MM(I_RM(flags));
        mmx_set_exp(I_RM(flags));
        write_back = 0;
        return 0;
    }
    linaddr = cpu_get_linaddr(flags, i);
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
    if (cpu_write64(write_linaddr, temp.d32a))
        return 1;
    return 0;
}
#define WRITE_BACK()                  \
    if (write_back && commit_write()) \
    return EXCEP()

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

#define write64(addr, src)      \
    if (cpu_write64(addr, src)) \
    EXCEP()
#define read64(addr, src)      \
    if (cpu_read64(addr, src)) \
    EXCEP()

OPTYPE op_mov_v64r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    mmx_reset_fpu();
    uint32_t flags = i->flags;
    if (get_ptr64_write(flags, i))
        EXCEP();
    cpu_mov64(result_ptr, &MM(I_REG(flags)));
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

OPTYPE op_mov_r64v32(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, *mm = MM(I_REG(flags)).r32, data;
    if (I_OP2(flags))
        data = cpu.reg32[I_RM(flags)];
    else
        cpu_read32(cpu_get_linaddr(flags, i), data, cpu.tlb_shift_read);
    mm[0] = data;
    mm[1] = 0;
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mov_v32r64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, data = MM(I_REG(flags)).r32[0];
    if (I_OP2(flags))
        cpu.reg32[I_RM(flags)] = data;
    else
        cpu_write32(cpu_get_linaddr(flags, i), data, cpu.tlb_shift_read);
    mmx_reset_fpu();
    NEXT(flags);
}

OPTYPE op_xor_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    cpu_mmx_xor(&MM(I_REG(flags)), result_ptr);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}

static inline int getmask(uint32_t* a, unsigned int n)
{
    // Same as a >= n ? 0 : -1
    // TODO: for 64-bit platfoms, we can use a 64-bit compare and still have high speed
    return -!(a[1] | (a[0] > (n - 1)));
}
static inline int getmask2(uint32_t a, unsigned int n)
{
    // Same as a >= n ? 0 : -1
    return -(a < (n - 1));
}

OPTYPE op_mmx_pshift_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, shift;
    if (get_ptr64_read(flags, i))
        EXCEP();
    void *x = &MM(I_REG(flags)), *maskptr = result_ptr;
    shift = *(uint32_t*)result_ptr;
    switch (i->imm16 >> 8 & 15) {
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

OPTYPE op_mmx_pshift_r64i8(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, shift = i->imm8;
    void* x = &MM(I_RM(flags));
    switch (i->imm16 >> 8 & 15) {
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
OPTYPE op_mmx_punpckl_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    punpckl(&MM(I_REG(flags)), result_ptr, 8, i->imm8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pmullw_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    pmullw(MM(I_REG(flags)).r16, result_ptr, 8, 0);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pmulhw_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    pmullw(MM(I_REG(flags)).r16, result_ptr, 8, 16);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_paddsubs_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    void *dest = MM(I_REG(flags)).r8, *src = result_ptr;
    switch (i->imm8 & 7) {
    case 0:
        paddusb(dest, src, 8);
        break;
    case 1:
        paddusw(dest, src, 4);
        break;
    case 2:
        paddssb(dest, src, 8);
        break;
    case 3:
        paddssw(dest, src, 4);
        break;
    case 4:
        psubusb(dest, src, 8);
        break;
    case 5:
        psubusw(dest, src, 4);
        break;
    case 6:
        psubssb(dest, src, 8);
        break;
    case 7:
        psubssw(dest, src, 4);
        break;
    }
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_punpckh_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    punpckh(&MM(I_REG(flags)), result_ptr, 8, i->imm8);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pack_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    void* dest = MM(I_REG(flags)).r32;
    switch (i->imm8 & 3) {
    case 0:
        packuswb(dest, result_ptr, 4);
        break;
    case 2:
        packsswb(dest, result_ptr, 4);
        break;
    case 3:
        packssdw(dest, result_ptr, 2);
        break;
    }
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

OPTYPE op_mmx_pshufw_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    pshuf(MM(I_REG(flags)).r32, result_ptr, i->imm8, 1);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_pmaddwd_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    pmaddwd(MM(I_REG(flags)).r32, result_ptr, 2);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_padd_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    void *dest = &MM(I_REG(flags)), *src = result_ptr;
    switch (i->imm8 & 3) {
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
OPTYPE op_mmx_psub_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    void *dest = &MM(I_REG(flags)), *src = result_ptr;
    switch (i->imm8 & 3) {
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
OPTYPE op_mmx_pandn_r64v64(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    pandn(MM(I_REG(flags)).r32, result_ptr, 2);
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}
OPTYPE op_mmx_movq2dq(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, *dest = &XMM32(I_REG(flags)), *src = MM(I_RM(flags)).r32;
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = 0;
    dest[3] = 0;
    NEXT(flags);
}
OPTYPE op_mmx_movdq2q(struct decoded_instruction* i)
{
    CHECK_MMX;
    uint32_t flags = i->flags, *src = &XMM32(I_RM(flags)), *dest = MM(I_REG(flags)).r32;
    dest[0] = src[0];
    dest[1] = src[1];
    mmx_set_exp(I_REG(flags));
    mmx_reset_fpu();
    NEXT(flags);
}

OPTYPE op_cvttpi2pf_x128v64(struct decoded_instruction* i)
{
    // Convert packed dword integer to packed float
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    if (i->imm8 == 0) {
        if (cvt_i32_to_f(&XMM32(I_REG(flags)), result_ptr, 2))
            EXCEP();
    } else {
        if (cvt_i32_to_d(&XMM32(I_REG(flags)), result_ptr, 2))
            EXCEP();
    }
    NEXT(flags);
}
OPTYPE op_cvttsi2sf_x128v32(struct decoded_instruction* i)
{
    // Convert scalar dword integer to scalar float
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (I_OP2(flags))
        temp.d32 = cpu.reg32[I_REG(flags)];
    else
        cpu_read32(cpu_get_linaddr(flags, i), temp.d32, cpu.tlb_shift_write);
    if (i->imm8 == 0) {
        if (cvt_i32_to_f(&XMM32(I_REG(flags)), &temp.d32, 1))
            EXCEP();
    } else {
        if (cvt_i32_to_d(&XMM32(I_REG(flags)), &temp.d32, 1))
            EXCEP();
    }
    NEXT(flags);
}
OPTYPE op_cvttps2pi_x128v64(struct decoded_instruction* i)
{
    // Convert packed dword integer to packed float
    CHECK_MMX;
    uint32_t flags = i->flags;
    if (get_ptr64_read(flags, i))
        EXCEP();
    if (i->imm8 == 0) {
        if (cvt_i32_to_f(&XMM32(I_REG(flags)), result_ptr, 2))
            EXCEP();
    } else {
        if (cvt_i32_to_d(&XMM32(I_REG(flags)), result_ptr, 2))
            EXCEP();
    }
    NEXT(flags);
}
OPTYPE op_cvttf2i(struct decoded_instruction* i)
{
    // Convert scalar/packed dword float to scalar/packed integer
    CHECK_MMX;
    uint32_t flags = i->flags, linaddr = cpu_get_linaddr(flags, i);
    void* res = i->imm8 & 2 ? &cpu.reg32[I_REG(flags)] : MM(I_REG(flags)).r32;
    // 0: NP 0F 2C - mm  single --> int32 trunc
    // 1: 66 0F 2C - mm  double --> int32 trunc
    // 2: F2 0F 2C - r32 single --> int32 trunc
    // 3: F3 0F 2C - r32 double --> int32 trunc
    // 4: NP 0F 2D - mm  single --> int32 no trunc
    // 5: 66 0F 2D - mm  double --> int32 no trunc
    // 6: F2 0F 2D - r32 single --> int32 no trunc
    // 7: F3 0F 2D - r32 double --> int32 no trunc
    int trunc = i->imm8 >> 2 & 1;
    switch (i->imm8 & 3) {
    case 0: // CVTTPS2PI - truncate - 0F 2C
        if (!(I_OP2(flags))) {
            read64(linaddr, &temp.d64);
        }else
            cpu_mov64(&temp.d64, &XMM32(I_RM(flags)));
        if(cvt_f_to_i32(res, &temp.d64, 2, trunc)) EXCEP();
        break;
    case 3: // CVTTSS2SI - truncate - F3 0F 2C
        if (!(I_OP2(flags)))
            cpu_read32(linaddr, temp.d32, cpu.tlb_shift_read);
        else
            temp.d32 = XMM32(I_RM(flags));
        if(cvt_f_to_i32(res, &temp.d32, 1, trunc)) EXCEP();
        break;
    case 1: // CVTTPD2PI - truncate - 66 0F 2C
        if (!(I_OP2(flags))){
            if(cpu_read128(linaddr, temp.d128))EXCEP();
        }else
            cpu_mov128(&temp.d128, &XMM32(I_RM(flags)));
        if(cvt_d_to_i32(res, &temp.d128, 2, trunc)) EXCEP();
        break;
    case 2: // CVTTSD2SI - truncate - F2 0F 2C
        if (!(I_OP2(flags))){
            read64(linaddr, &temp.d64);
        }else
            cpu_mov64(&temp.d64, &XMM32(I_RM(flags)));
        if(cvt_d_to_i32(res, &temp.d64, 1, trunc)) EXCEP();
        break;
    }
    NEXT(flags);
}