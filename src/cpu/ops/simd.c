// Single instruction, multiple data instructions.

#define NEED_STRUCT

#include "cpu/simd.h"
#include "cpu/cpu.h"
#include "cpu/fpu.h"
#include "io.h"
#define EXCEPTION_HANDLER return 1

///////////////////////////////////////////////////////////////////////////////
// Floating point routines
///////////////////////////////////////////////////////////////////////////////
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

    return 0;
}
#define CHECK_SSE            \
    if (cpu_sse_exception()) \
    return 1
#define CHECK_MMX           \
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
#if 0
static int get_mmx_write_ptr(uint32_t flags, struct decoded_instruction* i, int dwords)
{
    if (I_OP2(flags)) {
        result_ptr = &MM32(I_RM(flags));
        write_back = 0;
        return 0;
    } else
        return get_write_ptr(flags, i, dwords, 0);
}
#endif
static int get_reg_read_ptr(uint32_t flags, struct decoded_instruction* i)
{
    if (I_OP2(flags)) {
        result_ptr = &cpu.reg32[I_RM(flags)];
        return 0;
    } else
        return get_read_ptr(flags, i, 1, 0);
}
static void* get_mmx_reg_dest(int x)
{
    fpu.mm[x].dummy = 0xFFFF; // STn.exponent is set to all ones
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
    switch (i->imm8 & 15) {
    case MOVUPS_XGoXEo:
        // xmm128 <<== r/m128
        EX(get_sse_read_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        dest32[0] = *(uint32_t*)(result_ptr);
        dest32[1] = *(uint32_t*)(result_ptr + 4);
        dest32[2] = *(uint32_t*)(result_ptr + 8);
        dest32[3] = *(uint32_t*)(result_ptr + 12);
        break;
    case MOVSS_XGdXEd:
        // xmm32 <<== r/m32
        // Clear top 96 bits if source is memory
        EX(get_sse_read_ptr(flags, i, 1, 1));
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
        EX(get_sse_read_ptr(flags, i, 2, 1));
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
        EX(get_sse_write_ptr(flags, i, 4, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)(result_ptr) = dest32[0];
        *(uint32_t*)(result_ptr + 4) = dest32[1];
        *(uint32_t*)(result_ptr + 8) = dest32[2];
        *(uint32_t*)(result_ptr + 12) = dest32[3];
        WRITE_BACK();
        break;
    case MOVSS_XEdXGd:
        // r/m32 <<== xmm32
        EX(get_sse_write_ptr(flags, i, 1, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)(result_ptr) = dest32[0];
        WRITE_BACK();
        break;
    case MOVSD_XEqXGq:
        // r/m64 <<== xmm64
        EX(get_sse_write_ptr(flags, i, 1, 1));
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
        EX(get_sse_read_ptr(flags, i, 2, 1));
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
    case MOVHPS_XEqXGq:
        EX(get_sse_write_ptr(flags, i, 2, 1));
        src32 = get_sse_reg_dest(I_REG(flags));
        *(uint32_t*)(result_ptr + 8) = src32[0];
        *(uint32_t*)(result_ptr + 12) = src32[1];
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
            result = float32_compare(*(uint32_t*)result_ptr, dest32[0], &status);
        else
            result = float32_compare_quiet(*(uint32_t*)result_ptr, dest32[0], &status);
        int eflags = 0;
        switch(result){
            case float_relation_unordered:
                eflags = EFLAGS_ZF | EFLAGS_PF | EFLAGS_CF;
                break;
            case float_relation_less:
                eflags = EFLAGS_CF;
                break;
            case float_relation_equal:
                eflags = EFLAGS_ZF;
                break;
        }
        cpu_set_eflags(eflags | (cpu.eflags & ~arith_flag_mask));
        fp_exception = cpu_sse_handle_exceptions();
    }
    case UCOMISD_XGqXEq: {
        EX(get_sse_read_ptr(flags, i, 2, 1));
        dest32 = get_sse_reg_dest(I_REG(flags));
        int result;
        if (i->imm8 & 16) // UCOMISD
            result = float64_compare(*(uint64_t*)result_ptr, *(uint64_t*)&dest32[0], &status);
        else
            result = float64_compare_quiet(*(uint64_t*)result_ptr, *(uint64_t*)&dest32[0], &status);
        int eflags = 0;
        switch(result){
            case float_relation_unordered:
                eflags = EFLAGS_ZF | EFLAGS_PF | EFLAGS_CF;
                break;
            case float_relation_less:
                eflags = EFLAGS_CF;
                break;
            case float_relation_equal:
                eflags = EFLAGS_ZF;
                break;
        }
        cpu_set_eflags(eflags | (cpu.eflags & ~arith_flag_mask));
        fp_exception = cpu_sse_handle_exceptions();
    }
    }
    return fp_exception;
}
int cpu_emms(void){
    CHECK_MMX;
    fpu.tag_word = 0xFFFF;
    return 0;
}