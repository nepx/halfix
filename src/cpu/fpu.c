// x87 FPU operations built on top of the Bochs/SoftFloat floating point emulator.

// Quick note:
//         | ST0 | ST1 | | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
//  fld A  |  A  |  -  | |   |   |   |   |   |   |   | A | ftop: 7
//  fld B  |  B  |  A  | |   |   |   |   |   |   | B | A | ftop: 6
//  fadd C | B+A |  A  | |   |   |   |   |   |   |B+A| A | ftop: 6

// List of instructions that do NOT check for exceptions:
//  - FNSTENV, FNINIT, FNCLEX, FNSAVE, FNSTSW, FNSTSW, FNSTCW

// List of instructions that do not update FPU eip and friends:
//  - FNCLEX, FLDCW, FNSTCW, FNSTSW, FNSTENV, FLDENV, and FWAIT

// FLD mem order of operations:
//  1. Read source memory, but don't do anything with it yet
//  2. Save FPU CS:IP and data pointers
//  3. Convert source memory to 80-bit FP register
//  4. Check destination

// For basic arithmetic instructions, these are the possible operands:
/*
FADD:
 ST(0) = ST(0) + float32 <D8>
 ST(0) = ST(0) + int32 <DA>
 ST(0) = ST(0) + float64 <DC>
 ST(0) = ST(0) + int16 <DE>
 ST(0) = ST(0) + ST(i) <D8 mod=3>
 ST(i) = ST(i) + ST(0) <DC mod=3>
 ST(i) = ST(i) + ST(0) & pop <DE mod=3>
*/
// There are no read-modify-write operations, thank god.

#ifndef LIBCPU
#define FPU_DEBUG
#ifdef FPU_DEBUG
#include <math.h>
#endif
#endif

#include "cpu/cpu.h"
#include "cpu/instrument.h"
#include "devices.h"
#define EXCEPTION_HANDLER return 1

#define FLOATX80
#include "softfloat/softfloat.h"
#include "softfloat/softfloatx80.h"

#define NEED_STRUCT
#include "cpu/fpu.h"
#undef NEED_STRUCT

#if 0
// For savestate generator

    // <<< BEGIN STRUCT "struct" >>>
    int ftop;
    uint16_t control_word, status_word, tag_word;
    uint32_t fpu_eip, fpu_data_ptr;
    uint16_t fpu_cs, fpu_opcode, fpu_data_seg;
    // <<< END STRUCT "struct" >>>
#endif

enum {
    FPU_TAG_VALID = 0,
    FPU_TAG_ZERO = 1,
    FPU_TAG_SPECIAL = 2,
    FPU_TAG_EMPTY = 3
};

#define FPU_ROUND_SHIFT 10
#define FPU_PRECISION_SHIFT 8
enum {
    FPU_ROUND_NEAREST = 0,
    FPU_ROUND_DOWN = 1,
    FPU_ROUND_UP = 2,
    FPU_ROUND_TRUNCATE = 3
};
enum {
    FPU_PRECISION_24 = 0, // 32-bit float
    FPU_PRECISION_53 = 2, // 64-bit double
    FPU_PRECISION_64 = 3 // 80-bit st80
};
#define FPU_EXCEPTION_STACK_FAULT (1 << 6)
#define FPU_EXCEPTION_PRECISION (1 << 5)
#define FPU_EXCEPTION_UNDERFLOW (1 << 4)
#define FPU_EXCEPTION_OVERFLOW (1 << 3)
#define FPU_EXCEPTION_ZERO_DIVIDE (1 << 2)
#define FPU_EXCEPTION_DENORMALIZED (1 << 1)
#define FPU_EXCEPTION_INVALID_OPERATION (1 << 0)

// Status word
#define GET_C0 (fpu.status_word & (1 << 8)) != 0
#define GET_C1 (fpu.status_word & (1 << 9)) != 0
#define GET_C2 (fpu.status_word & (1 << 10)) != 0
#define GET_C3 (fpu.status_word & (1 << 14)) != 0
#define FPU_FTOP_SHIFT 11

#define SET_C0(n) fpu.status_word = (fpu.status_word & ~(1 << 8)) | (n) << 8
#define SET_C1(n) fpu.status_word = (fpu.status_word & ~(1 << 9)) | (n) << 9
#define SET_C2(n) fpu.status_word = (fpu.status_word & ~(1 << 10)) | (n) << 10
#define SET_C3(n) fpu.status_word = (fpu.status_word & ~(1 << 14)) | (n) << 14

#define MASKED(a) (fpu.control_word & a)

static const floatx80 Zero = BUILD_FLOAT(0, 0);
static const floatx80 IndefiniteNaN = BUILD_FLOAT(0xFFFF, 0xC000000000000000);
//static const floatx80 PositiveInfinity = BUILD_FLOAT(0x7FFF, 0x8000000000000000);
//static const extFloat80_t NegativeInfinity = BUILD_FLOAT(0xFFFF, 0x8000000000000000);
static const floatx80 Constant_1 = BUILD_FLOAT(0x3fff, 0x8000000000000000);
static const floatx80 Constant_L2T = BUILD_FLOAT(0x4000, 0xd49a784bcd1b8afe);
static const floatx80 Constant_L2E = BUILD_FLOAT(0x3fff, 0xb8aa3b295c17f0bc);
static const floatx80 Constant_PI = BUILD_FLOAT(0x4000, 0xc90fdaa22168c235);
static const floatx80 Constant_LG2 = BUILD_FLOAT(0x3ffd, 0x9a209a84fbcff799);
static const floatx80 Constant_LN2 = BUILD_FLOAT(0x3ffe, 0xb17217f7d1cf79ac);

static const floatx80* Constants[8] = {
    // Technically, there are only 7 constants according to the x87 spec, but to make this array nice and round, I'm going to assume that were there to be an eighth value, it would be an indefinite NaN.
    &Constant_1, &Constant_L2T, &Constant_L2E, &Constant_PI, &Constant_LG2, &Constant_LN2, &Zero, &IndefiniteNaN
};

struct fpu fpu;

// FLDCW
static void fpu_set_control_word(uint16_t control_word)
{
    control_word |= 0x40; // Experiments with real hardware indicate that bit 6 is always set.
    fpu.control_word = control_word;

    int rounding = fpu.control_word >> FPU_ROUND_SHIFT & 3;
    switch (rounding) {
    case FPU_ROUND_NEAREST: // aka round to even
        fpu.status.float_rounding_mode = float_round_nearest_even;
        break;
    case FPU_ROUND_DOWN:
        fpu.status.float_rounding_mode = float_round_down;
        break;
    case FPU_ROUND_UP:
        fpu.status.float_rounding_mode = float_round_up;
        break;
    case FPU_ROUND_TRUNCATE: // aka towards zero
        fpu.status.float_rounding_mode = float_round_to_zero;
        break;
    }
    int precision = fpu.control_word >> FPU_PRECISION_SHIFT & 3;
    switch (precision) {
    case FPU_PRECISION_24: // aka float
        fpu.status.float_rounding_precision = 32;
        break;
    case FPU_PRECISION_53: // aka double
        fpu.status.float_rounding_precision = 64;
        break;
    case FPU_PRECISION_64: // This is the default
        fpu.status.float_rounding_precision = 80;
        break;
    }

    // Are these right?
    fpu.status.float_exception_flags = 0; // clear exceptions before execution
    fpu.status.float_nan_handling_mode = float_first_operand_nan;
    fpu.status.flush_underflow_to_zero = 0;
    fpu.status.float_suppress_exception = 0;
    fpu.status.float_exception_masks = control_word & 0x3F;
    fpu.status.denormals_are_zeros = 0;
}

static void fpu_state(void)
{
#ifndef LIBCPU
    // <<< BEGIN AUTOGENERATE "state" >>>
    struct bjson_object* obj = state_obj("fpu", 9 + 16);
    state_field(obj, 4, "fpu.ftop", &fpu.ftop);
    state_field(obj, 2, "fpu.control_word", &fpu.control_word);
    state_field(obj, 2, "fpu.status_word", &fpu.status_word);
    state_field(obj, 2, "fpu.tag_word", &fpu.tag_word);
    state_field(obj, 4, "fpu.fpu_eip", &fpu.fpu_eip);
    state_field(obj, 4, "fpu.fpu_data_ptr", &fpu.fpu_data_ptr);
    state_field(obj, 2, "fpu.fpu_cs", &fpu.fpu_cs);
    state_field(obj, 2, "fpu.fpu_opcode", &fpu.fpu_opcode);
    state_field(obj, 2, "fpu.fpu_data_seg", &fpu.fpu_data_seg);
    // <<< END AUTOGENERATE "state" >>>
    char name[32];
    for (int i = 0; i < 8; i++) {
        sprintf(name, "fpu.st[%d].mantissa", i);
        state_field(obj, 8, name, &fpu.st[i].fraction);
        sprintf(name, "fpu.st[%d].exponent", i);
        state_field(obj, 2, name, &fpu.st[i].exp);
    }
    if (state_is_reading())
        fpu_set_control_word(fpu.control_word);
#endif
}

static uint16_t fpu_get_status_word(void)
{
    return fpu.status_word | (fpu.ftop << 11);
}

// Helper functions to determine type of floating point number.
static int is_denormal(uint16_t exponent, uint64_t mantissa)
{
    return !(exponent & 0x7FFF) && mantissa;
}
static int is_pseudo_denormal(uint16_t exponent, uint64_t mantissa)
{
    return is_denormal(exponent, mantissa) && !(mantissa & 0x8000000000000000ULL);
}

static int is_zero(uint16_t exponent, uint64_t mantissa)
{
    return ((exponent & 0x7FFF) | mantissa) == 0;
}

// Check if a floating point number is a zero of any sign.
// Returns 1 if it's a positive zero
// Returns -1 if it's a negative zero
// Returns 0 if it's neither.
static int is_zero_any_sign(uint16_t exponent, uint64_t mantissa)
{
    if (is_zero(exponent, mantissa)) {
        if (exponent & 0x8000)
            return -1;
        else
            return 1;
    } else
        return 0;
}

int is_negative(uint16_t exponent, uint64_t mantissa)
{
    return !is_zero_any_sign(exponent, mantissa) && (exponent & 0x8000) != 0;
}

static int is_invalid(uint16_t exponent, uint64_t mantissa)
{
    // Check for pseudo NaN, pseudo Infinity, or Unnormal
    uint16_t exponent_without_sign = exponent & 0x7FFF;
    if (exponent_without_sign != 0)
        return (mantissa & 0x8000000000000000ULL) == 0; // Pseudo-NaN, Pseudo-Infinity, or Unnormal
    return 0;
}

// Returns:
//   -1 for -Infinity
//    1 for Infinity
//    0 for non-infinity
static int is_infinity(uint16_t exponent, uint64_t mantissa)
{
    if (((exponent & 0x7FFF) == 0x7FFF) && (mantissa == 0x8000000000000000ULL))
        return mantissa >> 15 ? -1 : 1;
    return 0;
}

// Returns 1 if quiet, 2 if signalling, 0 if neither
static int is_nan(uint16_t exponent, uint64_t mantissa)
{
    if (((exponent & 0x7FFF) == 0x7FFF) && (mantissa != 0x8000000000000000ULL))
        return 1 + ((mantissa & 0x4000000000000000ULL) != 0);
    return 0;
}

static int fpu_get_tag_from_value(floatx80* f)
{
    uint16_t exponent;
    uint64_t mantissa;
    floatx80_unpack(f, exponent, mantissa);
    if ((exponent | mantissa) == 0) // Both exponent and mantissa are zero
        return FPU_TAG_ZERO;

    int x = 0;
    x |= is_infinity(exponent, mantissa);
    x |= is_denormal(exponent, mantissa);
    x |= is_pseudo_denormal(exponent, mantissa);
    x |= is_invalid(exponent, mantissa);
    x |= is_nan(exponent, mantissa);

    if (x)
        return FPU_TAG_SPECIAL;
    return FPU_TAG_VALID;
}

static int fpu_get_tag(int st)
{
    return fpu.tag_word >> (((st + fpu.ftop) & 7) << 1) & 3;
}
static void fpu_set_tag(int st, int v)
{
    int shift = ((st + fpu.ftop) & 7) << 1;
    fpu.tag_word &= ~(3 << shift);
    fpu.tag_word |= v << shift;
}

static int fpu_exception_raised(int flags)
{
    return (fpu.status.float_exception_flags & ~fpu.status.float_exception_masks) & flags;
}

// Note that stack faults must be handled before any arith, softfloat.c may clear them.
static void fpu_stack_fault(void)
{
    //__asm__("int3");
    fpu.status.float_exception_flags = FPU_EXCEPTION_INVALID_OPERATION | FPU_EXCEPTION_STACK_FAULT;
    //if(fpu.status.float_exception_masks & FPU_EXCEPTION_INVALID_OPERATION) return 1;
    //return 0;
}

static uint32_t partial_sw, bits_to_clear;

static void fpu_commit_sw(void)
{
    // XXX this is a really, really bad kludge
    fpu.status_word |= partial_sw;
    fpu.status_word &= ~bits_to_clear | partial_sw;
    bits_to_clear = 0;
    partial_sw = 0;
}

static int fpu_check_exceptions2(int commit_sw)
{
    int flags = fpu.status.float_exception_flags;
    int unmasked_exceptions = (flags & ~fpu.status.float_exception_masks) & 0x3F;

    // Note: #P is ignored if #U or #O is set.
    if (flags & FPU_EXCEPTION_PRECISION && (flags & (FPU_EXCEPTION_UNDERFLOW | FPU_EXCEPTION_OVERFLOW))) {
        flags &= ~FPU_EXCEPTION_PRECISION;
        unmasked_exceptions &= ~FPU_EXCEPTION_PRECISION;
    }

    // Note: C1 is set if the result was rounded up, but cleared if a stack underflow occurred
    if (flags & 0x10000) {
        // Stack underflow occurred
        flags &= ~(1 << 9);
    }

    // If #I, #D, or #Z, then ignore others.
    if (flags & (FPU_EXCEPTION_INVALID_OPERATION | FPU_EXCEPTION_ZERO_DIVIDE | FPU_EXCEPTION_DENORMALIZED)) {
        unmasked_exceptions &= FPU_EXCEPTION_INVALID_OPERATION | FPU_EXCEPTION_ZERO_DIVIDE | FPU_EXCEPTION_DENORMALIZED;
        flags &= FPU_EXCEPTION_INVALID_OPERATION | FPU_EXCEPTION_ZERO_DIVIDE | FPU_EXCEPTION_DENORMALIZED | FPU_EXCEPTION_STACK_FAULT;
    }

    if (commit_sw)
        fpu.status_word |= flags;
    else
        partial_sw |= flags;

    if (unmasked_exceptions) {
        fpu.status_word |= 0x8080;
        if (unmasked_exceptions & ~FPU_EXCEPTION_PRECISION)
            return 1;
        return 0; // Only #P is raised but we can ignore that
    }

    return 0;
}

static int fpu_check_exceptions(void)
{
    return fpu_check_exceptions2(1);
}

static void fninit(void)
{
    // https://www.felixcloutier.com/x86/finit:fninit
    fpu_set_control_word(0x37F);
    fpu.status_word = 0;
    fpu.tag_word = 0xFFFF;
    fpu.ftop = 0;
    fpu.fpu_data_ptr = 0;
    fpu.fpu_data_seg = 0;
    fpu.fpu_eip = 0;
    fpu.fpu_cs = 0; // Not in the docs, but assuming that it's the case
    fpu.fpu_opcode = 0;
}

static inline int fpu_nm_check(void)
{
    if (cpu.cr[0] & (CR0_EM | CR0_TS))
        EXCEPTION_NM();
    return 0;
}

static inline floatx80* fpu_get_st_ptr(int st)
{
    return &fpu.st[(fpu.ftop + st) & 7];
}
static inline floatx80 fpu_get_st(int st)
{
    return fpu.st[(fpu.ftop + st) & 7];
}
static inline void fpu_set_st(int st, floatx80 data)
{
    fpu_set_tag(st, fpu_get_tag_from_value(&data));
    fpu.st[(fpu.ftop + st) & 7] = data;
}

// Fault if ST register is not empty.
static int fpu_check_stack_overflow(int st)
{
    int tag = fpu_get_tag(st);
    if (tag != FPU_TAG_EMPTY) {
        SET_C1(1);
        fpu_stack_fault();
        return 1;
    }
    SET_C1(0);
    return 0;
}

// Fault if ST register is empty.
static int fpu_check_stack_underflow(int st, int commit_sw)
{
    int tag = fpu_get_tag(st);
    if (tag == FPU_TAG_EMPTY) {
        fpu_stack_fault();
        if (commit_sw)
            SET_C1(1);
        else
            partial_sw = 1 << 9;
        return 1;
    }
    if (commit_sw)
        SET_C1(0);
    else
        bits_to_clear = 1 << 9;
    return 0;
}

static int fpu_exception_masked(int excep)
{
    if (excep == FPU_EXCEPTION_STACK_FAULT)
        excep = FPU_EXCEPTION_INVALID_OPERATION;
    return (fpu.control_word & excep);
}

static int fpu_push(floatx80 data)
{
    fpu.ftop = (fpu.ftop - 1) & 7;
    fpu_set_st(0, data);
    return 0;
}
static void fpu_pop()
{
    fpu_set_tag(0, FPU_TAG_EMPTY);
    fpu.ftop = (fpu.ftop + 1) & 7;
}

static void fpu_update_pointers(uint32_t opcode)
{
    //if (VIRT_EIP() == 0x759783bb)
    //    __asm__("int3");
    fpu.fpu_cs = cpu.seg[CS];
    fpu.fpu_eip = VIRT_EIP();
    fpu.fpu_opcode = opcode;
}
static void fpu_update_pointers2(uint32_t opcode, uint32_t virtaddr, uint32_t seg)
{
    //if (VIRT_EIP() == 0x759783bb)
    //    __asm__("int3");
    fpu.fpu_cs = cpu.seg[CS];
    fpu.fpu_eip = VIRT_EIP();
    fpu.fpu_opcode = opcode;
    fpu.fpu_data_ptr = virtaddr;
    fpu.fpu_data_seg = cpu.seg[seg];
}

#if 0
static int read_float32(uint32_t linaddr, float32* dest)
{
    uint32_t low;
    cpu_read32(linaddr, low, cpu.tlb_shift_read);
    *dest = low;
    return 0;
}
#endif
static int write_float32(uint32_t linaddr, float32 src)
{
    cpu_write32(linaddr, src, cpu.tlb_shift_write);
    return 0;
}
#if 0
static int read_float64(uint32_t linaddr, float64* dest)
{
    uint32_t low, hi;
    cpu_read32(linaddr, low, cpu.tlb_shift_read);
    cpu_read32(linaddr + 4, hi, cpu.tlb_shift_read);
    *dest = (uint64_t)low | (uint64_t)hi << 32;
    return 0;
}
#endif
static int write_float64(uint32_t linaddr, float64 dest)
{
    uint64_t x = dest;
    cpu_write32(linaddr, (uint32_t)x, cpu.tlb_shift_write);
    cpu_write32(linaddr + 4, (uint32_t)(x >> 32), cpu.tlb_shift_write);
    return 0;
}

static int fpu_check_push(void)
{
    if (fpu_check_stack_overflow(-1)) {
        fpu_check_exceptions();
        if (fpu.control_word & FPU_EXCEPTION_INVALID_OPERATION) {
            // masked response
            fpu_push(IndefiniteNaN);
        } else
            fpu.status_word |= 0x80; // ?
        return 1;
    }
    return 0;
}

static int fpu_store_f80(uint32_t linaddr, floatx80* data)
{
    uint16_t exponent;
    uint64_t mantissa;
    floatx80_unpack(data, exponent, mantissa);
    int shift = cpu.tlb_shift_write;
    cpu_write32(linaddr, (uint32_t)mantissa, shift);
    cpu_write32(linaddr + 4, (uint32_t)(mantissa >> 32), shift);
    cpu_write16(linaddr + 8, exponent, shift);
    return 0;
}
static int fpu_read_f80(uint32_t linaddr, floatx80* data)
{
    uint16_t exponent;
    uint32_t low, hi;
    int shift = cpu.tlb_shift_read;
    cpu_read32(linaddr, low, shift);
    cpu_read32(linaddr + 4, hi, shift);
    cpu_read16(linaddr + 8, exponent, shift);
    floatx80_repack(data, exponent, (uint64_t)low | (uint64_t)hi << 32);
    return 0;
}

// Actual FPU operations
static int fpu_fcom(floatx80 op1, floatx80 op2, int unordered)
{
    int relation = floatx80_compare_internal(op1, op2, unordered, &fpu.status);
    if (fpu_check_exceptions())
        return 1;
    int bad = relation == float_relation_unordered;
    SET_C0(bad | (relation == float_relation_less));
    SET_C2(bad);
    SET_C3(bad | (relation == float_relation_equal));

    return 0;
}
static int fpu_fcomi(floatx80 op1, floatx80 op2, int unordered)
{
    int relation = floatx80_compare_internal(op1, op2, unordered, &fpu.status);
    if (fpu_check_exceptions())
        return 1;
    int bad = relation == float_relation_unordered;

    int cf = bad | (relation == float_relation_less);
    int pf = bad;
    int zf = bad | (relation == float_relation_equal);
    cpu_set_cf(cf);
    cpu_set_pf(pf);
    cpu_set_zf(zf);

    return 0;
}

//void fpu_debug(void);
static int fstenv(uint32_t linaddr, int code16)
{
    //fpu_debug();
    for (int i = 0; i < 8; i++) {
        if (fpu_get_tag(i) != FPU_TAG_EMPTY)
            fpu_set_tag(i, fpu_get_tag_from_value(&fpu.st[(fpu.ftop + i) & 7]));
    }
    // https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-1-manual.pdf
    // page 203
    int x = cpu.tlb_shift_write;
    //fpu_debug();
    //__asm__("int3");
    if (!code16) {
        cpu_write32(linaddr, 0xFFFF0000 | fpu.control_word, x);
        cpu_write32(linaddr + 4, 0xFFFF0000 | fpu_get_status_word(), x);
        cpu_write32(linaddr + 8, 0xFFFF0000 | fpu.tag_word, x);
        if (cpu.cr[0] & CR0_PE) {
            cpu_write32(linaddr + 12, fpu.fpu_eip, x);
            cpu_write32(linaddr + 16, fpu.fpu_cs | (fpu.fpu_opcode << 16), x);
            cpu_write32(linaddr + 20, fpu.fpu_data_ptr, x);
            cpu_write32(linaddr + 24, 0xFFFF0000 | fpu.fpu_data_seg, x);
        } else {
            uint32_t linear_fpu_eip = fpu.fpu_eip + (fpu.fpu_cs << 4);
            uint32_t linear_fpu_data = fpu.fpu_data_ptr + (fpu.fpu_data_seg << 4);
            cpu_write32(linaddr + 12, linear_fpu_eip | 0xFFFF0000, x);
            cpu_write32(linaddr + 16, (fpu.fpu_opcode & 0x7FF) | (linear_fpu_eip >> 4 & 0x0FFFF000), x);
            cpu_write32(linaddr + 20, linear_fpu_data | 0xFFFF0000, x);
            cpu_write32(linaddr + 24, linear_fpu_data >> 4 & 0x0FFFF000, x);
        }
    } else {
        cpu_write16(linaddr, fpu.control_word, x);
        cpu_write16(linaddr + 2, fpu_get_status_word(), x);
        cpu_write16(linaddr + 4, fpu.tag_word, x);
        if (cpu.cr[0] & CR0_PE) {
            cpu_write16(linaddr + 6, fpu.fpu_eip, x);
            cpu_write16(linaddr + 8, fpu.fpu_cs, x);
            cpu_write16(linaddr + 10, fpu.fpu_data_ptr, x);
            cpu_write16(linaddr + 12, fpu.fpu_data_seg, x);
        } else {
            uint32_t linear_fpu_eip = fpu.fpu_eip + (fpu.fpu_cs << 4);
            uint32_t linear_fpu_data = fpu.fpu_data_ptr + (fpu.fpu_data_seg << 4);
            cpu_write16(linaddr + 6, linear_fpu_eip, x);
            cpu_write16(linaddr + 8, (fpu.fpu_opcode & 0x7FF) | (linear_fpu_eip >> 4 & 0xF000), x);
            cpu_write16(linaddr + 10, linear_fpu_data, x);
            cpu_write16(linaddr + 12, linear_fpu_data >> 4 & 0xF000, x);
        }
    }
    return 0;
}
static int fldenv(uint32_t linaddr, int code16)
{
    uint32_t temp32;
    if (!code16) {
        cpu_read32(linaddr, temp32, cpu.tlb_shift_read);
        fpu_set_control_word(temp32);

        cpu_read16(linaddr + 4, fpu.status_word, cpu.tlb_shift_read);
        fpu.ftop = fpu.status_word >> 11 & 7;
        fpu.status_word &= ~(7 << 11); // Clear FTOP.

        cpu_read16(linaddr + 8, fpu.tag_word, cpu.tlb_shift_read);
        if (cpu.cr[0] & CR0_PE) {
            cpu_read32(linaddr + 12, fpu.fpu_eip, cpu.tlb_shift_read);

            cpu_read32(linaddr + 16, temp32, cpu.tlb_shift_read);
            fpu.fpu_cs = temp32 & 0xFFFF;
            fpu.fpu_opcode = temp32 >> 16 & 0x7FF;

            cpu_read32(linaddr + 20, fpu.fpu_data_ptr, cpu.tlb_shift_read);
            cpu_read32(linaddr + 24, fpu.fpu_data_seg, cpu.tlb_shift_read);
        } else {
            fpu.fpu_cs = 0;
            fpu.fpu_eip = 0;
            cpu_read16(linaddr + 12, fpu.fpu_eip, cpu.tlb_shift_read);

            cpu_read32(linaddr + 16, temp32, cpu.tlb_shift_read);
            fpu.fpu_opcode = temp32 & 0x7FF;
            fpu.fpu_eip |= temp32 << 4 & 0xFFFF0000;

            cpu_read32(linaddr + 20, temp32, cpu.tlb_shift_read);
            fpu.fpu_data_ptr = temp32 & 0xFFFF;

            cpu_read32(linaddr + 24, temp32, cpu.tlb_shift_read);
            fpu.fpu_eip |= temp32 << 4 & 0xFFFF0000;
        }
    } else {
        cpu_read16(linaddr, temp32, cpu.tlb_shift_read);
        fpu_set_control_word(temp32);

        cpu_read16(linaddr + 2, fpu.status_word, cpu.tlb_shift_read);
        fpu.ftop = fpu.status_word >> 11 & 7;
        fpu.status_word &= ~(7 << 11); // Clear FTOP.

        cpu_read16(linaddr + 4, fpu.tag_word, cpu.tlb_shift_read);
        if (cpu.cr[0] & CR0_PE) {
            cpu_read16(linaddr + 6, fpu.fpu_eip, cpu.tlb_shift_read);
            cpu_read16(linaddr + 8, fpu.fpu_cs, cpu.tlb_shift_read);
            cpu_read16(linaddr + 10, fpu.fpu_data_ptr, cpu.tlb_shift_read);
            cpu_read16(linaddr + 12, fpu.fpu_data_seg, cpu.tlb_shift_read);
        } else {
            fpu.fpu_cs = 0;
            fpu.fpu_eip = 0;
            cpu_read16(linaddr + 6, fpu.fpu_eip, cpu.tlb_shift_read);

            cpu_read16(linaddr + 8, temp32, cpu.tlb_shift_read);
            fpu.fpu_opcode = temp32 & 0x7FF;
            fpu.fpu_eip |= temp32 << 4 & 0xF0000;

            cpu_read16(linaddr + 10, temp32, cpu.tlb_shift_read);
            fpu.fpu_data_ptr = temp32 & 0xFFFF;

            cpu_read32(linaddr + 12, temp32, cpu.tlb_shift_read);
            fpu.fpu_eip |= temp32 << 4 & 0xF0000;
        }
    }
    if (fpu.status_word & ~fpu.control_word & 0x3F)
        fpu.status_word |= 0x8080;
    else
        fpu.status_word &= ~0x8080;
    return 0;
}

static void fpu_watchpoint(void)
{
    // For debugging purposes
    //if(VIRT_EIP() == 0x71961cad) __asm__("int3");
    //if(fpu.st[5].fraction == 0x00000006e8b877f6) __asm__("int3");
}
static void fpu_watchpoint2(void)
{
    // For debugging purposes
    //if(fpu.fpu_opcode == 0x77F8) __asm__("int3");
}

#define FPU_EXCEP() return 1
#define FPU_ABORT()        \
    do {                   \
        fpu_watchpoint2(); \
        return 0;          \
    } while (0) // Not an exception, so keep on goings

// Run a FPU operation that does not require memory
#define OP(op, reg) (op & 7) << 3 | reg
int fpu_reg_op(struct decoded_instruction* i, uint32_t flags)
{
    UNUSED(flags);
    UNUSED(fpu_exception_raised);
    UNUSED(fpu_get_st_ptr);
    uint32_t opcode = i->imm32;
    floatx80 temp80;
    if (fpu_nm_check())
        return 1;
#ifdef INSTRUMENT
    cpu_instrument_pre_fpu();
#endif
    fpu_watchpoint();

    fpu.status.float_exception_flags = 0;
    int smaller_opcode = (opcode >> 5 & 0x38) | (opcode >> 3 & 7);

    switch (smaller_opcode) {
    case OP(0xD8, 0):
    case OP(0xD8, 1):
    case OP(0xD8, 4):
    case OP(0xD8, 5):
    case OP(0xD8, 6):
    case OP(0xD8, 7):
    case OP(0xDC, 0):
    case OP(0xDC, 1):
    case OP(0xDC, 4):
    case OP(0xDC, 5):
    case OP(0xDC, 6):
    case OP(0xDC, 7):
    case OP(0xDE, 0):
    case OP(0xDE, 1):
    case OP(0xDE, 4):
    case OP(0xDE, 5):
    case OP(0xDE, 6):
    case OP(0xDE, 7): {
        int st_index = opcode & 7;
        floatx80 dst;
        if (fpu_fwait())
            return 1;
        fpu_update_pointers(opcode);
        if (fpu_check_stack_underflow(0, 1) || fpu_check_stack_underflow(st_index, 1))
            FPU_ABORT();

        switch (smaller_opcode & 7) {
        case 0: // FADD - Floating point add
            dst = floatx80_add(fpu_get_st(0), fpu_get_st(st_index), &fpu.status);
            break;
        case 1: // FMUL - Floating point multiply
            dst = floatx80_mul(fpu_get_st(0), fpu_get_st(st_index), &fpu.status);
            break;
        case 4: // FSUB - Floating point subtract
            dst = floatx80_sub(fpu_get_st(0), fpu_get_st(st_index), &fpu.status);
            break;
        case 5: // FSUBR - Floating point subtract reverse
            dst = floatx80_sub(fpu_get_st(st_index), fpu_get_st(0), &fpu.status);
            break;
        case 6: // FDIV - Floating point divide
            dst = floatx80_div(fpu_get_st(0), fpu_get_st(st_index), &fpu.status);
            break;
        case 7: // FDIVR - Floating point divide reverse
            dst = floatx80_div(fpu_get_st(st_index), fpu_get_st(0), &fpu.status);
            break;
        }
        if (!fpu_check_exceptions()) {
            if (smaller_opcode & 32) {
                fpu_set_st(st_index, dst);
                if (smaller_opcode & 16)
                    fpu_pop();
            } else
                fpu_set_st(0, dst);
        }
        break;
    }
    case OP(0xD8, 2): // FCOM - Floating point compare
    case OP(0xD8, 3): // FCOMP - Floating point compare and pop
    case OP(0xDC, 2):
    case OP(0xDC, 3):
    case OP(0xDE, 2): { // Aliases of the DB opcodes
        if (fpu_fwait())
            FPU_ABORT();
        if (fpu_check_stack_underflow(0, 1) || fpu_check_stack_underflow(opcode & 7, 1)) {
            SET_C0(1);
            SET_C2(1);
            SET_C3(1);
        }
        fpu_update_pointers(opcode);
        if (!fpu_fcom(fpu_get_st(0), fpu_get_st(opcode & 7), 0)) {
            if (smaller_opcode & 1 || smaller_opcode == (OP(0xDE, 2)))
                fpu_pop();
        }
        break;
    }
    case OP(0xD9, 0): // FLD - Load floating point value
        if (fpu_fwait())
            FPU_ABORT();
        fpu_update_pointers(opcode);
        if (fpu_check_stack_underflow(opcode & 7, 1) || fpu_check_push())
            FPU_ABORT();
        temp80 = fpu_get_st(opcode & 7);
        fpu_push(temp80);
        break;
    case OP(0xD9, 1): // FXCH - Floating point exchange
    case OP(0xDD, 1):
    case OP(0xDF, 1): // alias of FXCH
        if (fpu_fwait())
            FPU_ABORT();
        fpu_update_pointers(opcode);
        if (fpu_check_stack_underflow(0, 1) || fpu_check_stack_underflow(1, 1))
            FPU_ABORT();
        temp80 = fpu_get_st(0);
        fpu_set_st(0, fpu_get_st(opcode & 7));
        fpu_set_st(opcode & 7, temp80);
        break;
    case OP(0xD9, 2): // FNOP
        if (fpu_fwait())
            FPU_ABORT();
        fpu_update_pointers(opcode);
        break;
    case OP(0xD9, 4):
        if (fpu_fwait())
            FPU_ABORT();
        fpu_update_pointers(opcode);
        if ((opcode & 7) != 5)
            if (fpu_check_stack_underflow(0, 1))
                FPU_ABORT();
        temp80 = fpu_get_st(0);
        switch (opcode & 7) {
        case 0: // FCHS - Flip sign of floating point number
            floatx80_chs(&temp80);
            break;
        case 1: // FABS - Find absolute value of floating point number
            floatx80_abs(&temp80);
            break;
        case 4: // FTST - Compare floating point register to 0
            if (fpu_fcom(temp80, Zero, 0))
                FPU_ABORT();
            return 0;
        case 5: { // FXAM - Examine floating point number
            int unordered = 0;
            uint16_t exponent;
            uint64_t mantissa;
            floatx80_unpack(&temp80, exponent, mantissa);
            if (fpu_get_tag(0) == FPU_TAG_EMPTY)
                unordered = 5;
            else {
                if (is_invalid(exponent, mantissa))
                    unordered = 0;
                else if (is_nan(exponent, mantissa))
                    unordered = 1;
                else if (is_infinity(exponent, mantissa))
                    unordered = 3;
                else if (is_zero_any_sign(exponent, mantissa))
                    unordered = 4;
                else if (is_denormal(exponent, mantissa))
                    unordered = 6;
                else
                    unordered = 2;
            }
            SET_C0(unordered & 1);
            SET_C1(exponent >> 15 & 1); // Get sign
            SET_C2(unordered >> 1 & 1);
            SET_C3(unordered >> 2 & 1);
            return 0;
        }
        }
        fpu_set_st(0, temp80);
        break;
    case OP(0xD9, 5): // FLD - Load floating point constants
        if (fpu_fwait())
            FPU_ABORT();
        fpu_update_pointers(opcode);

        if (fpu_check_push())
            FPU_ABORT();
        fpu_push(*Constants[opcode & 7]);
        break;
    case OP(0xD9, 6): { // Various complex FPU operations
        if (fpu_fwait())
            FPU_ABORT();
        fpu_update_pointers(opcode);
        floatx80 res, temp;
        int temp2, old_rounding;
        switch (opcode & 7) {
        case 0: // D9 F0: F2XM1 - Compute 2^ST(0) - 1
            if (fpu_check_stack_underflow(0, 1))
                FPU_ABORT();
            res = f2xm1(fpu_get_st(0), &fpu.status);
            if (!fpu_check_exceptions())
                fpu_set_st(0, res);
            break;
        case 1: // D9 F1: FYL2X - Compute ST(1) * log2(ST(0)) and then pop
            if (fpu_check_stack_underflow(0, 1) || fpu_check_stack_underflow(1, 1))
                FPU_ABORT();

            old_rounding = fpu.status.float_rounding_precision;
            fpu.status.float_rounding_precision = 80;
            res = fyl2x(fpu_get_st(0), fpu_get_st(1), &fpu.status);
            fpu.status.float_rounding_precision = old_rounding;

            if (!fpu_check_exceptions()) {
                fpu_set_st(1, res);
                fpu_pop();
            }
            break;
        case 2: // D9 F2: FPTAN - Compute tan(ST(0)) partially
            if (fpu_check_stack_underflow(0, 1))
                FPU_ABORT();
            res = fpu_get_st(0);
            if (!ftan(&res, &fpu.status))
                fpu_set_st(0, res);
            break;
        case 3: // D9 F3: FPATAN - Compute tan-1(ST(0)) partially
            if (fpu_check_stack_underflow(0, 1) || fpu_check_stack_underflow(1, 1))
                FPU_ABORT();
            res = fpatan(fpu_get_st(0), fpu_get_st(1), &fpu.status);
            if (!fpu_check_exceptions()) {
                fpu_pop();
                fpu_set_st(0, res);
            }
            break;
        case 4: // D9 F4: FXTRACT - Extract Exponent and mantissa of ST0
            if (fpu_check_stack_underflow(0, 1))
                FPU_ABORT();
            if (fpu_check_stack_overflow(-1))
                FPU_ABORT();
            temp = fpu_get_st(0);
            res = floatx80_extract(&temp, &fpu.status);
            if (!fpu_check_exceptions()) {
                fpu_set_st(0, res);
                fpu_push(temp);
            }
            break;
        case 5: { // D9 F5: FPREM1 - Partial floating point remainder
            floatx80 st0 = fpu_get_st(0), st1 = fpu_get_st(1);
            uint64_t quo;
            temp2 = floatx80_ieee754_remainder(st0, st1, &temp, &quo, &fpu.status);
            if (!fpu_check_exceptions()) {
                if (!(temp2 < 0)) {
                    SET_C0(0);
                    SET_C2(0);
                    SET_C3(0);
                    if (temp2 > 0) {
                        SET_C2(1);
                    } else {
                        // 1 2 4 - 1 3 0
                        if (quo & 1)
                            SET_C1(1);
                        if (quo & 2)
                            SET_C3(1);
                        if (quo & 4)
                            SET_C0(1);
                    }
                }
                fpu_set_st(0, temp);
            }
            break;
        case 6: // D9 F6: FDECSTP - Decrement stack pointer
            SET_C1(0);
            fpu.ftop = (fpu.ftop - 1) & 7;
            break;
        case 7: // D9 F7: FINCSTP - Increment stack pointer
            SET_C1(0);
            fpu.ftop = (fpu.ftop + 1) & 7;
            break;
        }
        }
        break;
    }
    case OP(0xD9, 7): {
        if (fpu_fwait())
            return 1;
        fpu_update_pointers(opcode);

        // Check for FPU registers
        if (fpu_check_stack_underflow(0, 1))
            FPU_ABORT();

        int flags, pop = 0;
        floatx80 dest;
        uint64_t quotient;
        switch (opcode & 7) {
        case 0: // FPREM - Floating point partial remainder (8087/80287 compatible)
            if (fpu_check_stack_underflow(1, 1))
                FPU_ABORT();
            flags = floatx80_remainder(fpu_get_st(0), fpu_get_st(1), &dest, &quotient, &fpu.status);
            if (!fpu_check_exceptions()) {
                if (flags < 0) {
                    SET_C0(0);
                    SET_C1(0);
                    SET_C2(0);
                    SET_C3(0);
                } else {
                    if (flags != 0) {
                        SET_C0(0);
                        SET_C1(0);
                        SET_C2(1);
                        SET_C3(0);
                    } else {
                        SET_C0(quotient >> 2 & 1);
                        SET_C1(quotient & 1);
                        SET_C2(0);
                        SET_C3(quotient >> 1 & 1);
                    }
                }
                fpu_set_st(0, dest);
            }
            break;
        case 1: // FYL2XP1 - Compute ST1 * log2(ST0 + 1) and pop
            if (fpu_check_stack_underflow(1, 1))
                FPU_ABORT();
            dest = fyl2xp1(fpu_get_st(0), fpu_get_st(1), &fpu.status);
            if (!fpu_check_exceptions()) {
                fpu_pop();
                fpu_set_st(0, dest);
            }
            return 0;
        case 2: // FSQRT - Compute sqrt(ST0)
            dest = floatx80_sqrt(fpu_get_st(0), &fpu.status);
            break;
        case 3: { // FSINCOS - Compute sin(ST0) and sin(ST1)
            // TODO: What if exceptions are masked?
            if (fpu_check_stack_overflow(-1))
                FPU_ABORT();
            floatx80 sinres, cosres;
            if (fsincos(fpu_get_st(0), &sinres, &cosres, &fpu.status) == -1) {
                SET_C2(1);
            } else if (!fpu_check_exceptions()) {
                fpu_set_st(0, sinres);
                fpu_push(cosres);
            }
            return 0;
        }
        case 4: // FRNDINT - Round ST0
            dest = floatx80_round_to_int(fpu_get_st(0), &fpu.status);
            break;
        case 5: // FSCALE - Scale ST0
            if (fpu_check_stack_underflow(0, 1) || fpu_check_stack_underflow(1, 1))
                FPU_ABORT();
            dest = floatx80_scale(fpu_get_st(0), fpu_get_st(1), &fpu.status);
            break;
        case 6: // FSIN - Find sine of ST0
            dest = fpu_get_st(0);
            if (fsin(&dest, &fpu.status) == -1) {
                SET_C2(1);
                FPU_ABORT();
            }
            break;
        case 7: // FCOS - Find cosine of ST0
            dest = fpu_get_st(0);
            if (fcos(&dest, &fpu.status) == -1) {
                SET_C2(1);
                FPU_ABORT();
            }
            break;
        }
        if (!fpu_check_exceptions()) {
            fpu_set_st(0, dest);
            if (pop)
                fpu_pop();
        }
        break;
    }
    case OP(0xDA, 0):
    case OP(0xDB, 0): // FCMOV(N)B - Move floating point to register ST0 if condition code is set
        if (fpu_fwait())
            return 1;
        fpu_update_pointers(opcode);
        if (fpu_check_stack_underflow(0, 1) && fpu_check_stack_underflow(opcode & 7, 1))
            FPU_ABORT();
        if (cpu_get_cf() ^ (smaller_opcode >> 3 & 1))
            fpu_set_st(0, fpu_get_st(opcode & 7));
        break;
    case OP(0xDB, 1):
    case OP(0xDA, 1): // FCMOV(N)E - Move floating point to register ST0 if condition code is set
        if (fpu_fwait())
            return 1;
        fpu_update_pointers(opcode);
        if (fpu_check_stack_underflow(0, 1) && fpu_check_stack_underflow(opcode & 7, 1))
            FPU_ABORT();
        if (cpu_get_zf() ^ (smaller_opcode >> 3 & 1))
            fpu_set_st(0, fpu_get_st(opcode & 7));
        break;
    case OP(0xDB, 2):
    case OP(0xDA, 2): // FCMOV(N)BE - Move floating point to register ST0 if condition code is set
        if (fpu_fwait())
            return 1;
        fpu_update_pointers(opcode);
        if (fpu_check_stack_underflow(0, 1) && fpu_check_stack_underflow(opcode & 7, 1))
            FPU_ABORT();
        if ((cpu_get_zf() || cpu_get_cf()) ^ (smaller_opcode >> 3 & 1))
            fpu_set_st(0, fpu_get_st(opcode & 7));
        break;
    case OP(0xDB, 3):
    case OP(0xDA, 3): // FCMOV(N)U - Move floating point to register ST0 if condition code is set
        if (fpu_fwait())
            return 1;
        fpu_update_pointers(opcode);
        if (fpu_check_stack_underflow(0, 1) && fpu_check_stack_underflow(opcode & 7, 1))
            FPU_ABORT();
        if (cpu_get_pf() ^ (smaller_opcode >> 3 & 1))
            fpu_set_st(0, fpu_get_st(opcode & 7));
        break;
    case OP(0xDA, 5): // FUCOMPP
        if (fpu_fwait())
            return 1;
        fpu_update_pointers(opcode);
        if ((opcode & 7) == 1) {
            if (fpu_check_stack_underflow(0, 1) || fpu_check_stack_underflow(1, 1)) {
                SET_C0(1);
                SET_C2(1);
                SET_C3(1);
            }
            if (fpu_fcom(fpu_get_st(0), fpu_get_st(1), 1))
                FPU_ABORT();

            if (!fpu_check_exceptions()) {
                fpu_pop();
                fpu_pop();
            }
        }
        break;
    case OP(0xDB, 4):
        switch (opcode & 7) {
        case 0 ... 1:
        case 4:
            // All 286 opcodes, nop
            return 0;
        case 2: // DB E2: FNCLEX - Clear FPU exceptions
            fpu.status_word &= ~(0x80FF);
            break;
        case 3: // DB E3: FNINIT - Reset Floating point state
            fninit();
            break;
        default:
            EXCEPTION_UD();
        }
        break;
    case OP(0xDB, 5):
    case OP(0xDB, 6): // F(U)COMI : Do an (un)ordered compare, and set EFLAGS.
        if (fpu_fwait())
            return 1;
        fpu_update_pointers(opcode);

        cpu_set_eflags(cpu_get_eflags() & ~(EFLAGS_OF | EFLAGS_SF | EFLAGS_ZF | EFLAGS_AF | EFLAGS_PF | EFLAGS_CF));
        if (fpu_check_stack_underflow(0, 1) || fpu_check_stack_underflow(opcode & 7, 1)) {
            cpu_set_zf(1);
            cpu_set_pf(1);
            cpu_set_cf(1);
            FPU_ABORT();
        }
        if (fpu_fcomi(fpu_get_st(0), fpu_get_st(opcode & 7), smaller_opcode & 1))
            FPU_ABORT();
        break;
    case OP(0xD9, 3): // FSTP (alias)
    case OP(0xDD, 2): // FST - Store floating point value
    case OP(0xDD, 3):
    case OP(0xDF, 2):
    case OP(0xDF, 3): { // FSTP - Store floating point value and pop
        if (fpu_fwait())
            FPU_ABORT();
        fpu_update_pointers(opcode);
        if (fpu_check_stack_underflow(0, 1)) {
            if (fpu_exception_masked(FPU_EXCEPTION_STACK_FAULT))
                fpu_pop();
            FPU_ABORT();
        }
        fpu_set_st(opcode & 7, fpu_get_st(0));
        if (smaller_opcode & 1 || (smaller_opcode & ~1) == (OP(0xDF, 2)))
            fpu_pop();
        break;
    }
    case OP(0xDD, 0): // FFREE - Free floating point value
    case OP(0xDF, 0): { // FFREEP - Free floaing point value and pop
        if (fpu_fwait())
            FPU_ABORT();
        fpu_update_pointers(opcode);
        fpu_set_tag(opcode & 7, FPU_TAG_EMPTY);
        if (smaller_opcode == (OP(0xDF, 0)))
            fpu_pop();
        break;
    }
    case OP(0xDD, 4): // FUCOM - Unordered compare
    case OP(0xDD, 5): // FUCOMP
        if (fpu_fwait())
            return 1;
        fpu_update_pointers(opcode);
        if (fpu_check_stack_underflow(0, 1) || fpu_check_stack_underflow(opcode & 7, 1)) {
            SET_C0(1);
            SET_C2(1);
            SET_C3(1);
        }
        if (fpu_fcom(fpu_get_st(0), fpu_get_st(opcode & 7), 1))
            FPU_ABORT();

        if (!fpu_check_exceptions()) {
            if (smaller_opcode & 1)
                fpu_pop();
        }
        break;
    case OP(0xDE, 3): // FCOMPP - Floating point compare and pop twice
        if (fpu_fwait())
            FPU_ABORT();
        fpu_update_pointers(opcode);
        if (fpu_check_stack_underflow(0, 1) || fpu_check_stack_underflow(opcode & 7, 1)) {
            if (!fpu_check_exceptions()) {
                // Masked response
                SET_C0(1);
                SET_C2(1);
                SET_C3(1);
            }
            FPU_ABORT();
        }
        if (fpu_fcom(fpu_get_st(0), fpu_get_st(1), 0))
            FPU_ABORT();
        fpu_pop();
        fpu_pop();
        break;
    case OP(0xDF, 4): // FSTSW - Store status word
        if ((opcode & 7) != AX)
            EXCEPTION_UD();
        cpu.reg16[AX] = fpu_get_status_word();
        break;

    case OP(0xDF, 5): // FUCOMIP - Unordered compare and pop
    case OP(0xDF, 6): { // FCOMIP - Ordered compare and pop
        if (fpu_fwait())
            return 1;
        fpu_update_pointers(opcode);
        // Clear all flags
        cpu_set_eflags(cpu_get_eflags() & ~(EFLAGS_OF | EFLAGS_SF | EFLAGS_ZF | EFLAGS_AF | EFLAGS_PF | EFLAGS_CF));

        if (fpu_check_stack_underflow(0, 1) || fpu_check_stack_underflow(opcode & 7, 1)) {
            cpu_set_zf(1);
            cpu_set_pf(1);
            cpu_set_cf(1);
            FPU_ABORT();
        }
        if (fpu_fcomi(fpu_get_st(0), fpu_get_st(opcode & 7), smaller_opcode & 1))
            FPU_ABORT();
        fpu_pop();
        break;
    }
    case OP(0xDA, 4):
    case OP(0xDA, 6):
    case OP(0xDA, 7):
    case OP(0xDD, 6):
    case OP(0xDD, 7):
    case OP(0xDF, 7): { // Invalid
        int major_opcode = opcode >> 8 | 0xD8;
        UNUSED(major_opcode); // In case log is disabled
        CPU_LOG("Unknown FPU register operation: %02x %02x [%02x /%d] internal=%d\n", major_opcode, opcode & 0xFF, major_opcode, opcode >> 3 & 7, (opcode >> 5 & 0x38) | (opcode >> 3 & 7));
        EXCEPTION_UD();
        break;
    }
    default: {
        int major_opcode = opcode >> 8 | 0xD8;
        CPU_FATAL("Unknown FPU register operation: %02x %02x [%02x /%d] internal=%d\n", major_opcode, opcode & 0xFF, major_opcode, opcode >> 3 & 7, (opcode >> 5 & 0x38) | (opcode >> 3 & 7));
    }
    }
    fpu_watchpoint2();
    return 0;
}

int fpu_mem_op(struct decoded_instruction* i, uint32_t virtaddr, uint32_t seg)
{
    uint32_t opcode = i->imm32, linaddr = virtaddr + cpu.seg_base[seg];
    floatx80 temp80;
    float64 temp64;
    float32 temp32;

    if (fpu_nm_check())
        return 1;

#ifdef INSTRUMENT
    cpu_instrument_pre_fpu();
#endif

    fpu_watchpoint();

    fpu.status.float_exception_flags = 0;
    int smaller_opcode = (opcode >> 5 & 0x38) | (opcode >> 3 & 7);
    switch (smaller_opcode) {
    // Basic arithmetic

    case OP(0xD8, 0):
    case OP(0xD8, 1):
    case OP(0xD8, 2):
    case OP(0xD8, 3):
    case OP(0xD8, 4):
    case OP(0xD8, 5):
    case OP(0xD8, 6):
    case OP(0xD8, 7):
    case OP(0xD9, 0):
    case OP(0xDA, 0):
    case OP(0xDA, 1):
    case OP(0xDA, 2):
    case OP(0xDA, 3):
    case OP(0xDA, 4):
    case OP(0xDA, 5):
    case OP(0xDA, 6):
    case OP(0xDA, 7):
    case OP(0xDB, 0):
    case OP(0xDC, 0):
    case OP(0xDC, 1):
    case OP(0xDC, 2):
    case OP(0xDC, 3):
    case OP(0xDC, 4):
    case OP(0xDC, 5):
    case OP(0xDC, 6):
    case OP(0xDC, 7):
    case OP(0xDD, 0):
    case OP(0xDE, 0):
    case OP(0xDE, 1):
    case OP(0xDE, 2):
    case OP(0xDE, 3):
    case OP(0xDE, 4):
    case OP(0xDE, 5):
    case OP(0xDE, 6):
    case OP(0xDE, 7):
    case OP(0xDF, 0): {
        //printf("%08x %02x /%d\n", cpu.phys_eip, (smaller_opcode >> 3) | 0xD8, opcode & 7);
        //if(cpu.phys_eip == 0x0018b3f0) __asm__("int3");
        if (fpu_fwait())
            return 1;
        switch (opcode >> 9 & 3) {
        case 0:
            cpu_read32(linaddr, temp32, cpu.tlb_shift_read);
            temp80 = float32_to_floatx80(temp32, &fpu.status);
            break;
        case 1:
            cpu_read32(linaddr, temp32, cpu.tlb_shift_read);
            temp80 = int32_to_floatx80(temp32);
            break;
        case 2: {
            uint32_t low, hi;
            uint64_t res;
            cpu_read32(linaddr, low, cpu.tlb_shift_read);
            cpu_read32(linaddr + 4, hi, cpu.tlb_shift_read);
            res = (uint64_t)low | (uint64_t)hi << 32;
            temp80 = float64_to_floatx80(res, &fpu.status);
            break;
        }
        case 3: {
            cpu_read16(linaddr, temp32, cpu.tlb_shift_read);
            temp80 = int32_to_floatx80((int16_t)temp32);
            break;
        }
        }
        fpu_update_pointers2(opcode, virtaddr, seg);

        // Make sure we won't stack fault
        int op = smaller_opcode & 15;
        if ((op & 8) == 0) { // Don't do this for ST0
            if (fpu_check_stack_underflow(0, 1)) {
                if ((smaller_opcode & 14) == 2) {
                    // For FCOM/FCOMP, set condition codes to 1
                    SET_C0(1);
                    SET_C2(1);
                    SET_C3(1);
                }
                return 0;
            }
        } else {
            if (fpu_check_push())
                FPU_ABORT();
        }
        floatx80 st0 = fpu_get_st(0);
        switch (op) {
        case 0: // FADD - Floating point add
            st0 = floatx80_add(st0, temp80, &fpu.status);
            break;
        case 1: // FMUL - Floating point multiply
            st0 = floatx80_mul(st0, temp80, &fpu.status);
            break;
        case 2: // FCOM - Floating point compare
        case 3: // FCOMP - Floating point compare and pop
            if (!fpu_fcom(st0, temp80, 0)) {
                if (smaller_opcode & 1)
                    fpu_pop();
            }
            return 0;
        case 4: // FSUB - Floating point subtract
            st0 = floatx80_sub(st0, temp80, &fpu.status);
            break;
        case 5: // FSUBR - Floating point subtract with reversed operands
            st0 = floatx80_sub(temp80, st0, &fpu.status);
            break;
        case 6: // FDIV - Floating point divide
            st0 = floatx80_div(st0, temp80, &fpu.status);
            break;
        case 7: // FDIVR - Floating point divide with reversed operands
            st0 = floatx80_div(temp80, st0, &fpu.status);
            break;
        default: // FLD
            if (!fpu_check_exceptions())
                fpu_push(temp80);
            return 0;
        }
        if (!fpu_check_exceptions())
            fpu_set_st(0, st0);
        break;
    }

    case OP(0xD9, 2): // FST - Store floating point register
    case OP(0xD9, 3): { // FSTP m64 - Store floating point register and pop
        if (fpu_fwait())
            return 1;
        fpu_update_pointers2(opcode, virtaddr, seg);

        if (fpu_check_stack_underflow(0, 0))
            FPU_ABORT();
        temp32 = floatx80_to_float32(fpu_get_st(0), &fpu.status);
        if (!fpu_check_exceptions2(0)) {
            if (write_float32(linaddr, temp32))
                FPU_EXCEP();
            fpu_commit_sw();
            if (smaller_opcode & 1)
                fpu_pop();
        }
        break;
    }
    case OP(0xD9, 5): { // FLDCW
        uint16_t cw;
        cpu_read16(linaddr, cw, cpu.tlb_shift_read);
        fpu_set_control_word(cw);
        break;
    }
    case OP(0xD9, 6): { // FSTENV
        int is16 = I_OP2(i->flags);
        if (fstenv(linaddr, is16))
            FPU_ABORT();
        break;
    }
    case OP(0xD9, 7): // FSTCW - Store control word to memory
        cpu_write16(linaddr, fpu.control_word, cpu.tlb_shift_write);
        break;
    case OP(0xDB, 1): // FISTTP - Store floating point register (truncate to 0) to memory and pop
    case OP(0xDB, 2): // FIST - Store floating point register (converted to integer) to memory
    case OP(0xDB, 3): // FISTP - Store floating point register (converted to integer) to memory and pop
    case OP(0xDD, 1): // FISTTP
    case OP(0xDF, 1):
    case OP(0xDF, 2):
    case OP(0xDF, 3): {
        if (fpu_fwait())
            return 1;
        //fpu_debug();
        fpu_update_pointers2(opcode, virtaddr, seg);
        if (fpu_check_stack_underflow(0, 0))
            FPU_ABORT();
        switch (smaller_opcode >> 4 & 3) { // Extract the upper 2 bits of the small opcode
        case 1: { // DB
            uint32_t res;
            if (smaller_opcode & 2)
                res = floatx80_to_int32(fpu_get_st(0), &fpu.status);
            else
                res = floatx80_to_int32_round_to_zero(fpu_get_st(0), &fpu.status);
            if (!fpu_check_exceptions2(0))
                cpu_write32(linaddr, res, cpu.tlb_shift_write);
            break;
        }
        case 2: { // DD
            uint64_t res;
            if (smaller_opcode & 2)
                res = floatx80_to_int64(fpu_get_st(0), &fpu.status);
            else
                res = floatx80_to_int64_round_to_zero(fpu_get_st(0), &fpu.status);
            if (!fpu_check_exceptions2(0)) {
                cpu_write32(linaddr, res, cpu.tlb_shift_write);
                cpu_write32(linaddr + 4, res >> 32, cpu.tlb_shift_write);
            }
            break;
        }
        case 3: { // DF
            uint16_t res;
            if (smaller_opcode & 2)
                res = floatx80_to_int16(fpu_get_st(0), &fpu.status);
            else
                res = floatx80_to_int16_round_to_zero(fpu_get_st(0), &fpu.status);
            if (!fpu_check_exceptions2(0))
                cpu_write16(linaddr, res, cpu.tlb_shift_write);
            break;
        }
        }
        if (!fpu_check_exceptions2(0)) { // XXX eliminate this
            if (smaller_opcode & 1)
                fpu_pop();
        }
        fpu_commit_sw();
        break;
    }
    case OP(0xD9, 4): { // FLDENV - Load floating point environment from memory
        int is16 = I_OP2(i->flags);
        if (fldenv(linaddr, is16))
            FPU_ABORT();
        break;
    }
    case OP(0xDB, 5): { // FLD - Load floating point register from memory
        if (fpu_fwait())
            return 1;
        if (fpu_read_f80(linaddr, &temp80))
            return 1;
        fpu_update_pointers2(opcode, virtaddr, seg);
        if (fpu_check_stack_overflow(-1))
            FPU_ABORT();
        fpu_push(temp80);
        break;
    }
    case OP(0xDB, 7): { // FSTP - Store floating point register to memory and pop
        if (fpu_fwait())
            return 1;
        fpu_update_pointers2(opcode, virtaddr, seg);
        if (fpu_check_stack_underflow(0, 1))
            FPU_ABORT();
        if (fpu_store_f80(linaddr, fpu_get_st_ptr(0)))
            return 1;
        fpu_pop();
        break;
    }
    case OP(0xDD, 2): // FST - Store floating point register to memory
    case OP(0xDD, 3): { // FSTP - Store floating point register to memory and pop
        if (fpu_fwait())
            return 1;
        //fpu_debug();
        fpu_update_pointers2(opcode, virtaddr, seg);

        if (fpu_check_stack_underflow(0, 0))
            FPU_ABORT();
        temp64 = floatx80_to_float64(fpu_get_st(0), &fpu.status);
        if (!fpu_check_exceptions2(0)) {
            if (write_float64(linaddr, temp64))
                FPU_EXCEP();
            fpu_commit_sw();
            if (smaller_opcode & 1)
                fpu_pop();
        }
        break;
    }
    case OP(0xDD, 4): { // FRSTOR -- Load FPU context
        int is16 = I_OP2(i->flags);
        if (fldenv(linaddr, is16))
            FPU_ABORT();
        int offset = 14 << !is16;
        for (int i = 0; i < 8; i++) {
            if (fpu_read_f80(offset + linaddr, &fpu.st[(fpu.ftop + i) & 7]))
                FPU_EXCEP();
            offset += 10;
        }
        break;
    }
    case OP(0xDD, 6): { // FSAVE - Save FPU environment to memory
        int is16 = I_OP2(i->flags);
        if (fstenv(linaddr, is16))
            FPU_ABORT();
        int offset = 14 << !is16;
        for (int i = 0; i < 8; i++) {
            if (fpu_store_f80(offset + linaddr, &fpu.st[(fpu.ftop + i) & 7]))
                FPU_EXCEP();
            offset += 10;
        }
        fninit();
        break;
    }
    case OP(0xDD, 7): // FSTSW - Store status word to memory
        cpu_write16(linaddr, fpu_get_status_word(), cpu.tlb_shift_write);
        break;
    case OP(0xDF, 4): { // FBLD - The infamous "load BCD" instruction. Loads BCD integer and converts to floatx80
        uint32_t low, high, higher;
        if (fpu_fwait())
            return 1;
        cpu_read32(linaddr, low, cpu.tlb_shift_read);
        cpu_read32(linaddr + 4, high, cpu.tlb_shift_read);
        cpu_read16(linaddr + 8, higher, cpu.tlb_shift_read);
        fpu_update_pointers2(opcode, virtaddr, seg);

        uint64_t result = 0;
        int sign = higher & 0x8000;
        higher &= 0x7FFF;
        // XXX - use only one loop
        for (int i = 0; i < 4; i++) {
            result *= 100;
            uint8_t temp = low & 0xFF;
            result += temp - (6 * (temp >> 4));
            low >>= 8;
        }
        for (int i = 0; i < 4; i++) {
            result *= 100;
            uint8_t temp = high & 0xFF;
            result += temp - (6 * (temp >> 4));
            high >>= 8;
        }
        for (int i = 0; i < 2; i++) {
            result *= 100;
            uint8_t temp = higher & 0xFF;
            result += temp - (6 * (temp >> 4));
            higher >>= 8;
        }
        temp80 = int64_to_floatx80((uint64_t)sign << (64LL - 16LL) | result);
        if (fpu_check_push())
            FPU_ABORT();
        fpu_push(temp80);
        break;
    }
    case OP(0xDF, 5): { // FILD - Load floating point register.
        uint32_t hi, low;
        if (fpu_fwait())
            return 1;
        fpu_update_pointers2(opcode, virtaddr, seg);

        cpu_read32(linaddr, low, cpu.tlb_shift_read);
        cpu_read32(linaddr + 4, hi, cpu.tlb_shift_read);
        temp80 = int64_to_floatx80((uint64_t)low | (uint64_t)hi << 32);
        if (fpu_check_push())
            FPU_ABORT();
        fpu_push(temp80);
        break;
    }
    case OP(0xDF, 6): { // FBSTP - Store BCD to memory
        if (fpu_fwait())
            return 1;
        fpu_update_pointers2(opcode, virtaddr, seg);
        floatx80 st0 = fpu_get_st(0);
        uint64_t bcd = floatx80_to_int64(st0, &fpu.status);

        if (fpu_check_exceptions())
            FPU_ABORT();
        // Make sure we didn't cause exception

        for (int i = 0; i < 9; i++) {
            int result = bcd % 10;
            bcd /= 10;
            result |= (bcd % 10) << 4;
            bcd /= 10;
            cpu_write8(linaddr + i, result, cpu.tlb_shift_write);
        }

        int result = bcd % 10;
        bcd /= 10;
        result |= (bcd % 10) << 4;
        cpu_write8(linaddr + 9, result | (st0.exp >> 8 & 0x80), cpu.tlb_shift_write);

        fpu_pop();
        break;
    }
    case OP(0xDF, 7): { // FISTP - Store floating point register to integer and pop
        if (fpu_fwait())
            return 1;
        fpu_update_pointers2(opcode, virtaddr, seg);
        if (fpu_check_stack_underflow(0, 0))
            FPU_ABORT();
        uint64_t i64 = floatx80_to_int64(fpu_get_st(0), &fpu.status);
        if (fpu_check_exceptions2(0))
            FPU_ABORT();
        cpu_write32(linaddr, (uint32_t)i64, cpu.tlb_shift_write);
        cpu_write32(linaddr + 4, (uint32_t)(i64 >> 32), cpu.tlb_shift_write);
        fpu_commit_sw();
        fpu_pop();
        break;
    }
    case OP(0xD9, 1):
    case OP(0xDB, 4):
    case OP(0xDB, 6):
    case OP(0xDD, 5): {
        int major_opcode = opcode >> 8 | 0xD8;
        UNUSED(major_opcode);
        CPU_LOG("Unknown FPU memory operation: %02x %02x [%02x /%d] internal=%d\n", major_opcode, opcode & 0xFF, major_opcode, opcode >> 3 & 7, (opcode >> 5 & 0x38) | (opcode >> 3 & 7));
        break;
    }
    default: {
        int major_opcode = opcode >> 8 | 0xD8;
        CPU_FATAL("Unknown FPU memory operation: %02x %02x [%02x /%d] internal=%d\n", major_opcode, opcode & 0xFF, major_opcode, opcode >> 3 & 7, (opcode >> 5 & 0x38) | (opcode >> 3 & 7));
    }
    }
    fpu_watchpoint2();
    return 0;
}

// FXSAVE -- save floating point state
int fpu_fxsave(uint32_t linaddr)
{
    if (linaddr & 15)
        EXCEPTION_GP(0);
    if (fpu_nm_check())
        return 1;

    if (cpu_access_verify(linaddr, linaddr + 288 - 1, cpu.tlb_shift_write))
        return 1;
    cpu_write16(linaddr + 0, fpu.control_word, cpu.tlb_shift_write);
    cpu_write16(linaddr + 2, fpu_get_status_word(), cpu.tlb_shift_write);
    // "Abridge" tag word
    uint8_t tag = 0;
    for (int i = 0; i < 8; i++)
        if ((fpu.tag_word >> (i * 2) & 3) != FPU_TAG_EMPTY)
            tag |= 1 << i;

    // Some fields are less than 16 or 32 bits wide, but we write them anyways.
    // They are filled with zeros.
    cpu_write16(linaddr + 4, tag, cpu.tlb_shift_write);
    cpu_write16(linaddr + 6, fpu.fpu_opcode, cpu.tlb_shift_write);
    cpu_write32(linaddr + 8, fpu.fpu_eip, cpu.tlb_shift_write);
    cpu_write32(linaddr + 12, fpu.fpu_cs, cpu.tlb_shift_write);
    cpu_write32(linaddr + 16, fpu.fpu_data_ptr, cpu.tlb_shift_write);
    cpu_write32(linaddr + 20, fpu.fpu_data_seg, cpu.tlb_shift_write);
    cpu_write32(linaddr + 24, cpu.mxcsr, cpu.tlb_shift_write);
    cpu_write32(linaddr + 28, MXCSR_MASK, cpu.tlb_shift_write);
    uint32_t tempaddr = linaddr + 32;
    for (int i = 0; i < 8; i++) {
        fpu_store_f80(tempaddr, fpu_get_st_ptr(i));
        // Fill other bytes with zeros
        cpu_write16(tempaddr + 10, 0, cpu.tlb_shift_write);
        cpu_write32(tempaddr + 12, 0, cpu.tlb_shift_write);
        tempaddr += 16;
    }

    // TODO: Find out what happens on real hardware when OSFXSR isn't set.
    tempaddr = linaddr + 160;
    for (int i = 0; i < 8; i++) {
        cpu_write32(tempaddr, cpu.xmm32[(i * 4)], cpu.tlb_shift_write);
        cpu_write32(tempaddr + 4, cpu.xmm32[(i * 4) + 1], cpu.tlb_shift_write);
        cpu_write32(tempaddr + 8, cpu.xmm32[(i * 4) + 2], cpu.tlb_shift_write);
        cpu_write32(tempaddr + 12, cpu.xmm32[(i * 4) + 3], cpu.tlb_shift_write);
        tempaddr += 16;
    }
    return 0;
}
// FXRSTOR -- restore floating point state
int fpu_fxrstor(uint32_t linaddr)
{
    if (linaddr & 15)
        EXCEPTION_GP(0);
    if (fpu_nm_check())
        return 1;
    if (cpu_access_verify(linaddr, linaddr + 288 - 1, cpu.tlb_shift_read))
        return 1;

    uint32_t mxcsr;
    cpu_read32(linaddr + 24, mxcsr, cpu.tlb_shift_read);
    if (mxcsr & ~MXCSR_MASK)
        EXCEPTION_GP(0);
    cpu.mxcsr = mxcsr;
    cpu_update_mxcsr();

    uint32_t temp = 0;
    cpu_read16(linaddr + 0, temp, cpu.tlb_shift_read);
    fpu_set_control_word(temp);
    cpu_read16(linaddr + 2, temp, cpu.tlb_shift_read);
    fpu.status_word = temp;
    fpu.ftop = fpu.status_word >> 11 & 7;
    fpu.status_word &= ~(7 << 11);

    uint8_t small_tag_word;
    cpu_read8(linaddr + 4, small_tag_word, cpu.tlb_shift_read);

    cpu_read16(linaddr + 6, fpu.fpu_opcode, cpu.tlb_shift_read);
    fpu.fpu_opcode &= 0x7FF;
    cpu_read32(linaddr + 8, fpu.fpu_eip, cpu.tlb_shift_read);
    cpu_read16(linaddr + 12, fpu.fpu_cs, cpu.tlb_shift_read); // Note: 16-bit with 4 byte gap
    cpu_read32(linaddr + 16, fpu.fpu_data_ptr, cpu.tlb_shift_read);
    cpu_read16(linaddr + 20, fpu.fpu_data_seg, cpu.tlb_shift_read); // Note: 16-bit with 4 byte gap
    uint32_t tempaddr = linaddr + 32;
    for (int i = 0; i < 8; i++) {
        if (fpu_read_f80(tempaddr, fpu_get_st_ptr(i)))
            return 1;
        tempaddr += 16;
    }

    // TODO: Find out what happens on real hardware when OSFXSR isn't set.
    tempaddr = linaddr + 160;
    for (int i = 0; i < 8; i++) {
        cpu_read32(tempaddr, cpu.xmm32[(i * 4)], cpu.tlb_shift_read);
        cpu_read32(tempaddr + 4, cpu.xmm32[(i * 4) + 1], cpu.tlb_shift_read);
        cpu_read32(tempaddr + 8, cpu.xmm32[(i * 4) + 2], cpu.tlb_shift_read);
        cpu_read32(tempaddr + 12, cpu.xmm32[(i * 4) + 3], cpu.tlb_shift_read);
        tempaddr += 16;
    }

    // Now find out tag word
    uint16_t tag_word = 0;
    for (int i = 0; i < 8; i++) {
        int tagid = FPU_TAG_EMPTY;
        if (small_tag_word & (1 << i))
            tagid = fpu_get_tag_from_value(&fpu.st[i]);
        tag_word |= tagid << (i * 2);
    }
    //printf("new tag word: %04x small: %02x\n", tag_word, small_tag_word);
    fpu.tag_word = tag_word;
    return 0;
}

int fpu_fwait(void)
{
    // Now is as good of a time as any to call FPU exceptions
    if (fpu.status_word & 0x80) {
        if (cpu.cr[0] & CR0_NE)
            EXCEPTION_MF();
        else {
            // yucky, but works. OS/2 uses this method
            pic_lower_irq(13);
            pic_raise_irq(13);
            //ABORT();
        }
    }
    return 0;
}

void fpu_init(void)
{
    state_register(fpu_state);
}

// Utilities for debugging
#ifdef FPU_DEBUG
double f80_to_double(floatx80* f80)
{
    double scale_factor = pow(2.0, -63.0);
    uint16_t exponent;
    uint64_t fraction;
    floatx80_unpack(f80, exponent, fraction);

    double f = pow(2.0, ((exponent & 0x7FFF) - 0x3FFF));
    if (exponent > 0x8000)
        f = -f;
    f *= fraction * scale_factor;
    return f;
}
void fpu_debug(void)
{
    fprintf(stderr, " === FPU Context Dump ===\n");
    fprintf(stderr, "FPU CS:EIP: %04x:%08x Data Pointer: %04x:%08x\n", fpu.fpu_cs, fpu.fpu_eip, fpu.fpu_data_seg, fpu.fpu_data_ptr);
    int opcode = fpu.fpu_opcode >> 8 | 0xD8;
    fprintf(stderr, "Last FPU opcode: %04x [%02x %02x | %02x /%d]\n", fpu.fpu_opcode, opcode, fpu.fpu_opcode & 0xFF, opcode, fpu.fpu_opcode >> 3 & 7);
    fprintf(stderr, "Status: %04x (top: %d) Control: %04x Tag: %04x\n", fpu.status_word, fpu.ftop, fpu.control_word, fpu.tag_word);
    for (int i = 0; i < 8; i++) {
        int real_index = (i + fpu.ftop) & 7;
        floatx80 st = fpu.st[real_index];
        double f = f80_to_double(&st);

        uint16_t exponent;
        uint64_t fraction;
        floatx80_unpack(&st, exponent, fraction);

        uint32_t high = fraction >> 32;
        fprintf(stderr, "ST%d(%c) [FP%d]: %04x %08x%08x (%.10f)\n", i, "v0se"[fpu_get_tag(i)], real_index, exponent, high, (uint32_t)fraction, f);
    }
}

void printFloat80(floatx80* arg)
{
    uint16_t exponent;
    uint64_t fraction;
    floatx80_unpack(arg, exponent, fraction);
    printf("%04x %08x%08x\n", exponent, (uint32_t)(fraction >> 32), (uint32_t)fraction);
}

void* fpu_get_st_ptr1(void)
{
    return &fpu.st[0];
}
#endif

#ifdef LIBCPU
// Initialize FPU state
void fpu_init_lib(void)
{
    // Disable anything that might cause FPU exceptions
    cpu.cr[0] &= ~(CR0_EM | CR0_TS);

    // Enable FPU exceptions (but they're all masked anyways by fninit)
    cpu.cr[0] |= CR0_NE;

    // Initialize the FPU
    fninit();
}
#endif