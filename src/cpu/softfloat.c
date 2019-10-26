/*============================================================================
This C source file is part of the SoftFloat IEC/IEEE Floating-point Arithmetic
Package, Release 2b.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://www.cs.berkeley.edu/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

#define FLOAT128

#define GET_CONSTANTS

/*============================================================================
 * Adapted for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

/*============================================================================
 * Adapted, with love, for Halfix ;)
 * ==========================================================================*/

// Modifications that let this FPU emulator compile with Halfix:
//  > -Wall -Wextra -Werror compatibility
//  > C99 compatibility
//  > Some extra functions that fpu.c uses but aren't directly exposed in Bochs. 

#include "softfloat/softfloat.h"
#include "softfloat/softfloat-round-pack.h"

/*----------------------------------------------------------------------------
| Primitive arithmetic functions, including multi-word arithmetic, and
| division and square root approximations. (Can be specialized to target
| if desired).
*----------------------------------------------------------------------------*/
#define USE_estimateDiv128To64
#define USE_estimateSqrt32
#include "softfloat/softfloat-macros.h"

/*----------------------------------------------------------------------------
| Functions and definitions to determine:  (1) whether tininess for underflow
| is detected before or after rounding by default, (2) what (if anything)
| happens when exceptions are raised, (3) how signaling NaNs are distinguished
| from quiet NaNs, (4) the default generated quiet NaNs, and (5) how NaNs
| are propagated from function inputs to output.  These details are target-
| specific.
*----------------------------------------------------------------------------*/
#include "softfloat/softfloat-specialize.h"

#include "softfloat/fpu-constants.h"

#include "softfloat/softfloatx80.h"

/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit two's complement integer `a'
| to the single-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 int32_to_float32(int32_t a, float_status_t *status)
{
    if (a == 0) return 0;
    if (a == (int32_t) 0x80000000) return packFloat32(1, 0x9E, 0);
    int zSign = (a < 0);
    return normalizeRoundAndPackFloat32(zSign, 0x9C, zSign ? -a : a, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit two's complement integer `a'
| to the double-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 int32_to_float64(int32_t a)
{
    if (a == 0) return 0;
    int zSign = (a < 0);
    uint32_t absA = zSign ? -a : a;
    int shiftCount = countLeadingZeros32(absA) + 21;
    uint64_t zSig = absA;
    return packFloat64(zSign, 0x432 - shiftCount, zSig<<shiftCount);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit two's complement integer `a'
| to the single-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 int64_to_float32(int64_t a, float_status_t *status)
{
    if (a == 0) return 0;
    int zSign = (a < 0);
    uint64_t absA = zSign ? -a : a;
    int shiftCount = countLeadingZeros64(absA) - 40;
    if (0 <= shiftCount) {
        return packFloat32(zSign, 0x95 - shiftCount, (uint32_t)(absA<<shiftCount));
    }
    else {
        shiftCount += 7;
        if (shiftCount < 0) {
            absA = shift64RightJamming(absA, -shiftCount);
        }
        else {
            absA <<= shiftCount;
        }
        return roundAndPackFloat32(zSign, 0x9C - shiftCount, (uint32_t) absA, status);
    }
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit two's complement integer `a'
| to the double-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 int64_to_float64(int64_t a, float_status_t *status)
{
    if (a == 0) return 0;
    if (a == (int64_t) U64(0x8000000000000000)) {
        return packFloat64(1, 0x43E, 0);
    }
    int zSign = (a < 0);
    return normalizeRoundAndPackFloat64(zSign, 0x43C, zSign ? -a : a, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit unsigned integer `a' to the
| single-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 uint32_to_float32(uint32_t a, float_status_t *status)
{
    if (a == 0) return 0;
    if (a & 0x80000000) return normalizeRoundAndPackFloat32(0, 0x9D, a >> 1, status);
    return normalizeRoundAndPackFloat32(0, 0x9C, a, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit unsigned integer `a' to the
| double-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 uint32_to_float64(uint32_t a)
{
   if (a == 0) return 0;
   int shiftCount = countLeadingZeros32(a) + 21;
   uint64_t zSig = a;
   return packFloat64(0, 0x432 - shiftCount, zSig<<shiftCount);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit unsigned integer integer `a'
| to the single-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 uint64_to_float32(uint64_t a, float_status_t *status)
{
    if (a == 0) return 0;
    int shiftCount = countLeadingZeros64(a) - 40;
    if (0 <= shiftCount) {
        return packFloat32(0, 0x95 - shiftCount, (uint32_t)(a<<shiftCount));
    }
    else {
        shiftCount += 7;
        if (shiftCount < 0) {
            a = shift64RightJamming(a, -shiftCount);
        }
        else {
            a <<= shiftCount;
        }
        return roundAndPackFloat32(0, 0x9C - shiftCount, (uint32_t) a, status);
    }
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit unsigned integer integer `a'
| to the double-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 uint64_to_float64(uint64_t a, float_status_t *status)
{
    if (a == 0) return 0;
    if (a & U64(0x8000000000000000))
        return normalizeRoundAndPackFloat64(0, 0x43D, a >> 1, status);
    return normalizeRoundAndPackFloat64(0, 0x43C, a, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 32-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic - which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN or the
| conversion overflows the integer indefinite value is returned.
*----------------------------------------------------------------------------*/

int32_t float32_to_int32(float32 a, float_status_t *status)
{
    uint32_t aSig = extractFloat32Frac(a);
    int16_t aExp = extractFloat32Exp(a);
    int aSign = extractFloat32Sign(a);
    if ((aExp == 0xFF) && aSig) aSign = 0;
    if (aExp) aSig |= 0x00800000;
    else {
        if (get_denormals_are_zeros(status)) aSig = 0;
    }
    int shiftCount = 0xAF - aExp;
    uint64_t aSig64 = ((uint64_t)(aSig)) << 32;
    if (0 < shiftCount) aSig64 = shift64RightJamming(aSig64, shiftCount);
    return roundAndPackInt32(aSign, aSig64, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 32-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN or the conversion overflows, the integer indefinite
| value is returned.
*----------------------------------------------------------------------------*/

int32_t float32_to_int32_round_to_zero(float32 a, float_status_t *status)
{
    int aSign;
    int16_t aExp;
    uint32_t aSig;
    int32_t z;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);
    int shiftCount = aExp - 0x9E;
    if (0 <= shiftCount) {
        if (a != 0xCF000000) {
            float_raise(status, float_flag_invalid);
        }
        return (int32_t)(int32_indefinite);
    }
    else if (aExp <= 0x7E) {
        if (get_denormals_are_zeros(status) && aExp == 0) aSig = 0;
        if (aExp | aSig) float_raise(status, float_flag_inexact);
        return 0;
    }
    aSig = (aSig | 0x800000)<<8;
    z = aSig>>(-shiftCount);
    if ((uint32_t) (aSig<<(shiftCount & 31))) {
        float_raise(status, float_flag_inexact);
    }
    if (aSign) z = -z;
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 32-bit unsigned integer format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-point Arithmetic,
| except that the conversion is always rounded toward zero.  If `a' is a NaN
| or conversion overflows, the largest positive integer is returned.
*----------------------------------------------------------------------------*/

uint32_t float32_to_uint32_round_to_zero(float32 a, float_status_t *status)
{
    int aSign;
    int16_t aExp;
    uint32_t aSig;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);
    int shiftCount = aExp - 0x9E;

    if (aExp <= 0x7E) {
        if (get_denormals_are_zeros(status) && aExp == 0) aSig = 0;
        if (aExp | aSig) float_raise(status, float_flag_inexact);
        return 0;
    }
    else if (0 < shiftCount || aSign) {
        float_raise(status, float_flag_invalid);
        return uint32_indefinite;
    }

    aSig = (aSig | 0x800000)<<8;
    uint32_t z = aSig >> (-shiftCount);
    if (aSig << (shiftCount & 31)) {
        float_raise(status, float_flag_inexact);
    }
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 64-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic - which means in particular that the conversion is rounded
| according to the current rounding mode. If `a' is a NaN or the
| conversion overflows, the integer indefinite value is returned.
*----------------------------------------------------------------------------*/

int64_t float32_to_int64(float32 a, float_status_t *status)
{
    uint64_t aSig64, aSigExtra;

    uint32_t aSig = extractFloat32Frac(a);
    int16_t aExp = extractFloat32Exp(a);
    int aSign = extractFloat32Sign(a);

    int shiftCount = 0xBE - aExp;
    if (shiftCount < 0) {
        float_raise(status, float_flag_invalid);
        return (int64_t)(int64_indefinite);
    }
    if (aExp) aSig |= 0x00800000;
    else {
        if (get_denormals_are_zeros(status)) aSig = 0;
    }
    aSig64 = aSig;
    aSig64 <<= 40;
    shift64ExtraRightJamming(aSig64, 0, shiftCount, &aSig64, &aSigExtra);
    return roundAndPackInt64(aSign, aSig64, aSigExtra, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 64-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN or the conversion overflows, the integer indefinite
| value is returned.
*----------------------------------------------------------------------------*/

int64_t float32_to_int64_round_to_zero(float32 a, float_status_t *status)
{
    int aSign;
    int16_t aExp;
    uint32_t aSig;
    uint64_t aSig64;
    int64_t z;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);
    int shiftCount = aExp - 0xBE;
    if (0 <= shiftCount) {
        if (a != 0xDF000000) {
            float_raise(status, float_flag_invalid);
        }
        return (int64_t)(int64_indefinite);
    }
    else if (aExp <= 0x7E) {
        if (get_denormals_are_zeros(status) && aExp == 0) aSig = 0;
        if (aExp | aSig) float_raise(status, float_flag_inexact);
        return 0;
    }
    aSig64 = aSig | 0x00800000;
    aSig64 <<= 40;
    z = aSig64>>(-shiftCount);
    if ((uint64_t) (aSig64<<(shiftCount & 63))) {
        float_raise(status, float_flag_inexact);
    }
    if (aSign) z = -z;
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 64-bit unsigned integer format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic,
| except that the conversion is always rounded toward zero. If `a' is a NaN
| or the conversion overflows, the largest unsigned integer is returned.
*----------------------------------------------------------------------------*/

uint64_t float32_to_uint64_round_to_zero(float32 a, float_status_t *status)
{
    int aSign;
    int16_t aExp;
    uint32_t aSig;
    uint64_t aSig64;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);
    int shiftCount = aExp - 0xBE;

    if (aExp <= 0x7E) {
        if (get_denormals_are_zeros(status) && aExp == 0) aSig = 0;
        if (aExp | aSig) float_raise(status, float_flag_inexact);
        return 0;
    }
    else if (0 < shiftCount || aSign) {
        float_raise(status, float_flag_invalid);
        return uint64_indefinite;
    }

    aSig64 = aSig | 0x00800000;
    aSig64 <<= 40;
    uint64_t z = aSig64>>(-shiftCount);
    if ((uint64_t) (aSig64<<(shiftCount & 63))) {
        float_raise(status, float_flag_inexact);
    }
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 64-bit unsigned integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN or the conversion
| overflows, the largest unsigned integer is returned.
*----------------------------------------------------------------------------*/

uint64_t float32_to_uint64(float32 a, float_status_t *status)
{
    int aSign;
    int16_t aExp, shiftCount;
    uint32_t aSig;
    uint64_t aSig64, aSigExtra;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
    }

    if ((aSign) && (aExp > 0x7E)) {
        float_raise(status, float_flag_invalid);
        return uint64_indefinite;
    }

    shiftCount = 0xBE - aExp;
    if (aExp) aSig |= 0x00800000;

    if (shiftCount < 0) {
        float_raise(status, float_flag_invalid);
        return uint64_indefinite;
    }

    aSig64 = aSig;
    aSig64 <<= 40;
    shift64ExtraRightJamming(aSig64, 0, shiftCount, &aSig64, &aSigExtra);
    return roundAndPackUint64(aSign, aSig64, aSigExtra, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 32-bit unsigned integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN or the conversion
| overflows, the largest unsigned integer is returned. 
*----------------------------------------------------------------------------*/

uint32_t float32_to_uint32(float32 a, float_status_t *status)
{
    uint64_t val_64 = float32_to_uint64(a, status);

    if (val_64 > 0xffffffff) {
        status->float_exception_flags = float_flag_invalid; // throw away other flags
        return uint32_indefinite;
    }

    return (uint32_t) val_64;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the double-precision floating-point format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float64 float32_to_float64(float32 a, float_status_t *status)
{
    uint32_t aSig = extractFloat32Frac(a);
    int16_t aExp = extractFloat32Exp(a);
    int  aSign = extractFloat32Sign(a);

    if (aExp == 0xFF) {
        if (aSig) return commonNaNToFloat64(float32ToCommonNaN(a, status));
        return packFloat64(aSign, 0x7FF, 0);
    }
    if (aExp == 0) {
        if (aSig == 0 || get_denormals_are_zeros(status))
            return packFloat64(aSign, 0, 0);

        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(aSig, &aExp, &aSig);
        --aExp;
    }
    return packFloat64(aSign, aExp + 0x380, ((uint64_t) aSig)<<29);
}

/*----------------------------------------------------------------------------
| Rounds the single-precision floating-point value `a' to an integer, and
| returns the result as a single-precision floating-point value.  The
| operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_round_to_int_with_scale(float32 a, uint8_t scale, float_status_t *status)
{
    uint32_t lastBitMask, roundBitsMask;
    int roundingMode = get_float_rounding_mode(status);
    int16_t aExp = extractFloat32Exp(a);
    scale &= 0xf;

    if ((aExp == 0xFF) && extractFloat32Frac(a)) {
        return propagateFloat32NaN(a, status);
    }

    aExp += scale; // scale the exponent

    if (0x96 <= aExp) {
        return a;
    }

    if (get_denormals_are_zeros(status)) {
        a = float32_denormal_to_zero(a);
    }

    if (aExp <= 0x7E) {
        if ((uint32_t) (a<<1) == 0) return a;
        float_raise(status, float_flag_inexact);
        int aSign = extractFloat32Sign(a);
        switch (roundingMode) {
         case float_round_nearest_even:
            if ((aExp == 0x7E) && extractFloat32Frac(a)) {
                return packFloat32(aSign, 0x7F - scale, 0);
            }
            break;
         case float_round_down:
            return aSign ? packFloat32(1, 0x7F - scale, 0) : float32_positive_zero;
         case float_round_up:
            return aSign ? float32_negative_zero : packFloat32(0, 0x7F - scale, 0);
        }
        return packFloat32(aSign, 0, 0);
    }

    lastBitMask = 1;
    lastBitMask <<= 0x96 - aExp;
    roundBitsMask = lastBitMask - 1;
    float32 z = a;
    if (roundingMode == float_round_nearest_even) {
        z += lastBitMask>>1;
        if ((z & roundBitsMask) == 0) z &= ~lastBitMask;
    }
    else if (roundingMode != float_round_to_zero) {
        if (extractFloat32Sign(z) ^ (roundingMode == float_round_up)) {
            z += roundBitsMask;
        }
    }
    z &= ~roundBitsMask;
    if (z != a) float_raise(status, float_flag_inexact);
    return z;
}

/*----------------------------------------------------------------------------
| Extracts the fractional portion of single-precision floating-point value `a',
| and returns the result  as a  single-precision  floating-point value. The
| fractional results are precise. The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_frc(float32 a, float_status_t *status)
{
    int roundingMode = get_float_rounding_mode(status);

    int16_t aExp = extractFloat32Exp(a);
    uint32_t aSig = extractFloat32Frac(a);
    int aSign = extractFloat32Sign(a);

    if (aExp == 0xFF) {
        if (aSig) return propagateFloat32NaN(a, status);
        float_raise(status, float_flag_invalid);
        return float32_default_nan;
    }

    if (aExp >= 0x96) {
        return packFloat32(roundingMode == float_round_down, 0, 0);
    }

    if (aExp < 0x7F) {
        if (aExp == 0) {
            if (aSig == 0 || get_denormals_are_zeros(status))
                return packFloat32(roundingMode == float_round_down, 0, 0);

            float_raise(status, float_flag_denormal);
            if (! float_exception_masked(status, float_flag_underflow))
                float_raise(status, float_flag_underflow);

            if(get_flush_underflow_to_zero(status)) {
                float_raise(status, float_flag_underflow | float_flag_inexact);
                return packFloat32(aSign, 0, 0);
            }
        }
        return a;
    }

    uint32_t lastBitMask = 1 << (0x96 - aExp);
    uint32_t roundBitsMask = lastBitMask - 1;

    aSig &= roundBitsMask;
    aSig <<= 7;
    aExp--;

    if (aSig == 0)
       return packFloat32(roundingMode == float_round_down, 0, 0);

    return normalizeRoundAndPackFloat32(aSign, aExp, aSig, status);
}

/*----------------------------------------------------------------------------
| Extracts the exponent portion of single-precision floating-point value 'a',
| and returns the result as a single-precision floating-point value
| representing unbiased integer exponent. The operation is performed according
| to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_getexp(float32 a, float_status_t *status)
{
    int16_t aExp = extractFloat32Exp(a);
    uint32_t aSig = extractFloat32Frac(a);

    if (aExp == 0xFF) {
        if (aSig) return propagateFloat32NaN(a, status);
        return float32_positive_inf;
    }

    if (aExp == 0) {
        if (aSig == 0 || get_denormals_are_zeros(status))
            return float32_negative_inf;

        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(aSig, &aExp, &aSig);
    }

    return int32_to_float32(aExp - 0x7F, status);
}

/*----------------------------------------------------------------------------
| Extracts the mantissa of single-precision floating-point value 'a' and
| returns the result as a single-precision floating-point after applying
| the mantissa interval normalization and sign control. The operation is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_getmant(float32 a, float_status_t *status, int sign_ctrl, int interv)
{
    int16_t aExp = extractFloat32Exp(a);
    uint32_t aSig = extractFloat32Frac(a);
    int aSign = extractFloat32Sign(a);

    if (aExp == 0xFF) {
        if (aSig) return propagateFloat32NaN(a, status);
        if (aSign) {
            if (sign_ctrl & 0x2) {
                float_raise(status, float_flag_invalid);
                return float32_default_nan;
            }
        }
        return packFloat32(~sign_ctrl & aSign, 0x7F, 0);
    }

    if (aExp == 0 && (aSig == 0 || get_denormals_are_zeros(status))) {
        return packFloat32(~sign_ctrl & aSign, 0x7F, 0);
    }

    if (aSign) {
        if (sign_ctrl & 0x2) {
            float_raise(status, float_flag_invalid);
            return float32_default_nan;
        }
    }

    if (aExp == 0) {
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(aSig, &aExp, &aSig);
//      aExp += 0x7E;
        aSig &= 0x7FFFFF;
    }

    switch(interv) {
    case 0x0: // interval [1,2)
        aExp = 0x7F;
        break;
    case 0x1: // interval [1/2,2)
        aExp -= 0x7F;
        aExp  = 0x7F - (aExp & 0x1);
        break;
    case 0x2: // interval [1/2,1)
        aExp = 0x7E;
        break;
    case 0x3: // interval [3/4,3/2)
        aExp = 0x7F - ((aSig >> 22) & 0x1);
        break;
    }

    return packFloat32(~sign_ctrl & aSign, aExp, aSig);
}

/*----------------------------------------------------------------------------
| Return the result of a floating point scale of the single-precision floating
| point value `a' by multiplying it by 2 power of the single-precision
| floating point value 'b' converted to integral value. If the result cannot
| be represented in single precision, then the proper overflow response (for
| positive scaling operand), or the proper underflow response (for negative
| scaling operand) is issued. The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_scalef(float32 a, float32 b, float_status_t *status)
{
    uint32_t aSig = extractFloat32Frac(a);
    int16_t aExp = extractFloat32Exp(a);
    int aSign = extractFloat32Sign(a);
    uint32_t bSig = extractFloat32Frac(b);
    int16_t bExp = extractFloat32Exp(b);
    int bSign = extractFloat32Sign(b);

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
    }

    if (bExp == 0xFF) {
        if (bSig) return propagateFloat32NaN_two_args(a, b, status);
    }

    if (aExp == 0xFF) {
        if (aSig) {
            int aIsSignalingNaN = (aSig & 0x00400000) == 0;
            if (aIsSignalingNaN || bExp != 0xFF || bSig)
                return propagateFloat32NaN_two_args(a, b, status);

            return bSign ? 0 : float32_positive_inf;
        }

        if (bExp == 0xFF && bSign) {
            float_raise(status, float_flag_invalid);
            return float32_default_nan;
        }
        return a;
    }

    if (aExp == 0) {
        if (aSig == 0) {
            if (bExp == 0xFF && ! bSign) {
                float_raise(status, float_flag_invalid);
                return float32_default_nan;
            }
            return a;
        }
        float_raise(status, float_flag_denormal);
    }

    if ((bExp | bSig) == 0) return a;

    if (bExp == 0xFF) {
        if (bSign) return packFloat32(aSign, 0, 0);
        return packFloat32(aSign, 0xFF, 0);
    }

    if (bExp >= 0x8E) {
        // handle obvious overflow/underflow result
        return roundAndPackFloat32(aSign, bSign ? -0x7F : 0xFF, aSig, status);
    }

    int scale = 0;

    if (bExp <= 0x7E) {
        if (bExp == 0)
            float_raise(status, float_flag_denormal);
        scale = -bSign;
    }
    else {
        int shiftCount = bExp - 0x9E;
        bSig = (bSig | 0x800000)<<8;
        scale = bSig>>(-shiftCount);

        if (bSign) {
            if ((uint32_t) (bSig<<(shiftCount & 31))) scale++;
            scale = -scale;
        }

        if (scale >  0x200) scale =  0x200;
        if (scale < -0x200) scale = -0x200;
    }

    if (aExp != 0) {
        aSig |= 0x00800000;
    } else {
        aExp++;
    }

    aExp += scale - 1;
    aSig <<= 7;
    return normalizeRoundAndPackFloat32(aSign, aExp, aSig, status);
}

/*----------------------------------------------------------------------------
| Returns the result of adding the absolute values of the single-precision
| floating-point values `a' and `b'.  If `zSign' is 1, the sum is negated
| before being returned.  `zSign' is ignored if the result is a NaN.
| The addition is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float32 addFloat32Sigs(float32 a, float32 b, int zSign, float_status_t *status)
{
    int16_t aExp, bExp, zExp;
    uint32_t aSig, bSig, zSig;
    int16_t expDiff;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    bSig = extractFloat32Frac(b);
    bExp = extractFloat32Exp(b);

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
    }

    expDiff = aExp - bExp;
    aSig <<= 6;
    bSig <<= 6;

    if (0 < expDiff) {
        if (aExp == 0xFF) {
            if (aSig) return propagateFloat32NaN_two_args(a, b, status);
            if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
            return a;
        }
        if ((aExp == 0) && aSig)
            float_raise(status, float_flag_denormal);

        if (bExp == 0) {
            if (bSig) float_raise(status, float_flag_denormal);
            --expDiff;
        }
        else bSig |= 0x20000000;

        bSig = shift32RightJamming(bSig, expDiff);
        zExp = aExp;
    }
    else if (expDiff < 0) {
        if (bExp == 0xFF) {
            if (bSig) return propagateFloat32NaN_two_args(a, b, status);
            if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
            return packFloat32(zSign, 0xFF, 0);
        }
        if ((bExp == 0) && bSig)
            float_raise(status, float_flag_denormal);

        if (aExp == 0) {
            if (aSig) float_raise(status, float_flag_denormal);
            ++expDiff;
        }
        else aSig |= 0x20000000;

        aSig = shift32RightJamming(aSig, -expDiff);
        zExp = bExp;
    }
    else {
        if (aExp == 0xFF) {
            if (aSig | bSig) return propagateFloat32NaN_two_args(a, b, status);
            return a;
        }
        if (aExp == 0) {
            zSig = (aSig + bSig) >> 6;
            if (aSig | bSig) {
                float_raise(status, float_flag_denormal);
                if (get_flush_underflow_to_zero(status) && (extractFloat32Frac(zSig) == zSig)) {
                    float_raise(status, float_flag_underflow | float_flag_inexact);
                    return packFloat32(zSign, 0, 0);
                }
                if (! float_exception_masked(status, float_flag_underflow)) {
                    if (extractFloat32Frac(zSig) == zSig)
                        float_raise(status, float_flag_underflow);
                }
            }
            return packFloat32(zSign, 0, zSig);
        }
        zSig = 0x40000000 + aSig + bSig;
        return roundAndPackFloat32(zSign, aExp, zSig, status);
    }
    aSig |= 0x20000000;
    zSig = (aSig + bSig)<<1;
    --zExp;
    if ((int32_t) zSig < 0) {
        zSig = aSig + bSig;
        ++zExp;
    }
    return roundAndPackFloat32(zSign, zExp, zSig, status);
}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the absolute values of the single-
| precision floating-point values `a' and `b'.  If `zSign' is 1, the
| difference is negated before being returned.  `zSign' is ignored if the
| result is a NaN.  The subtraction is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float32 subFloat32Sigs(float32 a, float32 b, int zSign, float_status_t *status)
{
    int16_t aExp, bExp, zExp;
    uint32_t aSig, bSig, zSig;
    int16_t expDiff;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    bSig = extractFloat32Frac(b);
    bExp = extractFloat32Exp(b);

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
    }

    expDiff = aExp - bExp;
    aSig <<= 7;
    bSig <<= 7;
    if (0 < expDiff) goto aExpBigger;
    if (expDiff < 0) goto bExpBigger;
    if (aExp == 0xFF) {
        if (aSig | bSig) return propagateFloat32NaN_two_args(a, b, status);
        float_raise(status, float_flag_invalid);
        return float32_default_nan;
    }
    if (aExp == 0) {
        if (aSig | bSig) float_raise(status, float_flag_denormal);
        aExp = 1;
        bExp = 1;
    }
    if (bSig < aSig) goto aBigger;
    if (aSig < bSig) goto bBigger;
    return packFloat32(get_float_rounding_mode(status) == float_round_down, 0, 0);
 bExpBigger:
    if (bExp == 0xFF) {
        if (bSig) return propagateFloat32NaN_two_args(a, b, status);
        if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
        return packFloat32(zSign ^ 1, 0xFF, 0);
    }
    if ((bExp == 0) && bSig)
        float_raise(status, float_flag_denormal);

    if (aExp == 0) {
        if (aSig) float_raise(status, float_flag_denormal);
        ++expDiff;
    }
    else aSig |= 0x40000000;

    aSig = shift32RightJamming(aSig, -expDiff);
    bSig |= 0x40000000;
 bBigger:
    zSig = bSig - aSig;
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if (aExp == 0xFF) {
        if (aSig) return propagateFloat32NaN_two_args(a, b, status);
        if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
        return a;
    }
    if ((aExp == 0) && aSig)
        float_raise(status, float_flag_denormal);

    if (bExp == 0) {
        if (bSig) float_raise(status, float_flag_denormal);
        --expDiff;
    }
    else bSig |= 0x40000000;

    bSig = shift32RightJamming(bSig, expDiff);
    aSig |= 0x40000000;
 aBigger:
    zSig = aSig - bSig;
    zExp = aExp;
 normalizeRoundAndPack:
    --zExp;
    return normalizeRoundAndPackFloat32(zSign, zExp, zSig, status);
}

/*----------------------------------------------------------------------------
| Returns the result of adding the single-precision floating-point values `a'
| and `b'.  The operation is performed according to the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_add(float32 a, float32 b, float_status_t *status)
{
    int aSign = extractFloat32Sign(a);
    int bSign = extractFloat32Sign(b);

    if (aSign == bSign) {
        return addFloat32Sigs(a, b, aSign, status);
    }
    else {
        return subFloat32Sigs(a, b, aSign, status);
    }
}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the single-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_sub(float32 a, float32 b, float_status_t *status)
{
    int aSign = extractFloat32Sign(a);
    int bSign = extractFloat32Sign(b);

    if (aSign == bSign) {
        return subFloat32Sigs(a, b, aSign, status);
    }
    else {
        return addFloat32Sigs(a, b, aSign, status);
    }
}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the single-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_mul(float32 a, float32 b, float_status_t *status)
{
    int aSign, bSign, zSign;
    int16_t aExp, bExp, zExp;
    uint32_t aSig, bSig;
    uint64_t zSig64;
    uint32_t zSig;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);
    bSig = extractFloat32Frac(b);
    bExp = extractFloat32Exp(b);
    bSign = extractFloat32Sign(b);
    zSign = aSign ^ bSign;

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
    }

    if (aExp == 0xFF) {
        if (aSig || ((bExp == 0xFF) && bSig))
            return propagateFloat32NaN_two_args(a, b, status);

        if ((bExp | bSig) == 0) {
            float_raise(status, float_flag_invalid);
            return float32_default_nan;
        }
        if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
        return packFloat32(zSign, 0xFF, 0);
    }
    if (bExp == 0xFF) {
        if (bSig) return propagateFloat32NaN_two_args(a, b, status);
        if ((aExp | aSig) == 0) {
            float_raise(status, float_flag_invalid);
            return float32_default_nan;
        }
        if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
        return packFloat32(zSign, 0xFF, 0);
    }
    if (aExp == 0) {
        if (aSig == 0) {
            if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
            return packFloat32(zSign, 0, 0);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        if (bSig == 0) return packFloat32(zSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(bSig, &bExp, &bSig);
    }
    zExp = aExp + bExp - 0x7F;
    aSig = (aSig | 0x00800000)<<7;
    bSig = (bSig | 0x00800000)<<8;
    zSig64 = shift64RightJamming(((uint64_t) aSig) * bSig, 32);
    zSig = (uint32_t) zSig64;
    if (0 <= (int32_t) (zSig<<1)) {
        zSig <<= 1;
        --zExp;
    }
    return roundAndPackFloat32(zSign, zExp, zSig, status);
}

/*----------------------------------------------------------------------------
| Returns the result of dividing the single-precision floating-point value `a'
| by the corresponding value `b'.  The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_div(float32 a, float32 b, float_status_t *status)
{
    int aSign, bSign, zSign;
    int16_t aExp, bExp, zExp;
    uint32_t aSig, bSig, zSig;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);
    bSig = extractFloat32Frac(b);
    bExp = extractFloat32Exp(b);
    bSign = extractFloat32Sign(b);
    zSign = aSign ^ bSign;

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
    }

    if (aExp == 0xFF) {
        if (aSig) return propagateFloat32NaN_two_args(a, b, status);
        if (bExp == 0xFF) {
            if (bSig) return propagateFloat32NaN_two_args(a, b, status);
            float_raise(status, float_flag_invalid);
            return float32_default_nan;
        }
        if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
        return packFloat32(zSign, 0xFF, 0);
    }
    if (bExp == 0xFF) {
        if (bSig) return propagateFloat32NaN_two_args(a, b, status);
        if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
        return packFloat32(zSign, 0, 0);
    }
    if (bExp == 0) {
        if (bSig == 0) {
            if ((aExp | aSig) == 0) {
                float_raise(status, float_flag_invalid);
                return float32_default_nan;
            }
            float_raise(status, float_flag_divbyzero);
            return packFloat32(zSign, 0xFF, 0);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(bSig, &bExp, &bSig);
    }
    if (aExp == 0) {
        if (aSig == 0) return packFloat32(zSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(aSig, &aExp, &aSig);
    }
    zExp = aExp - bExp + 0x7D;
    aSig = (aSig | 0x00800000)<<7;
    bSig = (bSig | 0x00800000)<<8;
    if (bSig <= (aSig + aSig)) {
        aSig >>= 1;
        ++zExp;
    }
    zSig = (((uint64_t) aSig)<<32) / bSig;
    if ((zSig & 0x3F) == 0) {
        zSig |= ((uint64_t) bSig * zSig != ((uint64_t) aSig)<<32);
    }
    return roundAndPackFloat32(zSign, zExp, zSig, status);
}

/*----------------------------------------------------------------------------
| Returns the square root of the single-precision floating-point value `a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_sqrt(float32 a, float_status_t *status)
{
    int aSign;
    int16_t aExp, zExp;
    uint32_t aSig, zSig;
    uint64_t rem, term;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);

    if (aExp == 0xFF) {
        if (aSig) return propagateFloat32NaN(a, status);
        if (! aSign) return a;
        float_raise(status, float_flag_invalid);
        return float32_default_nan;
    }

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
    }

    if (aSign) {
        if ((aExp | aSig) == 0) return packFloat32(aSign, 0, 0);
        float_raise(status, float_flag_invalid);
        return float32_default_nan;
    }
    if (aExp == 0) {
        if (aSig == 0) return 0;
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(aSig, &aExp, &aSig);
    }
    zExp = ((aExp - 0x7F)>>1) + 0x7E;
    aSig = (aSig | 0x00800000)<<8;
    zSig = estimateSqrt32(aExp, aSig) + 2;
    if ((zSig & 0x7F) <= 5) {
        if (zSig < 2) {
            zSig = 0x7FFFFFFF;
            goto roundAndPack;
        }
        aSig >>= aExp & 1;
        term = ((uint64_t) zSig) * zSig;
        rem = (((uint64_t) aSig)<<32) - term;
        while ((int64_t) rem < 0) {
            --zSig;
            rem += (((uint64_t) zSig)<<1) | 1;
        }
        zSig |= (rem != 0);
    }
    zSig = shift32RightJamming(zSig, 1);
 roundAndPack:
    return roundAndPackFloat32(0, zExp, zSig, status);
}

/*----------------------------------------------------------------------------
| Determine single-precision floating-point number class.
*----------------------------------------------------------------------------*/

float_class_t float32_class(float32 a)
{
   int16_t aExp = extractFloat32Exp(a);
   uint32_t aSig = extractFloat32Frac(a);
   int  aSign = extractFloat32Sign(a);

   if(aExp == 0xFF) {
       if (aSig == 0)
           return (aSign) ? float_negative_inf : float_positive_inf;

       return (aSig & 0x00400000) ? float_QNaN : float_SNaN;
   }

   if(aExp == 0) {
       if (aSig == 0) return float_zero;
       return float_denormal;
   }

   return float_normalized;
}

/*----------------------------------------------------------------------------
| Compare  between  two  single  precision  floating  point  numbers. Returns
| 'float_relation_equal'  if the operands are equal, 'float_relation_less' if
| the    value    'a'   is   less   than   the   corresponding   value   `b',
| 'float_relation_greater' if the value 'a' is greater than the corresponding
| value `b', or 'float_relation_unordered' otherwise.
*----------------------------------------------------------------------------*/

int float32_compare_internal(float32 a, float32 b, int quiet, float_status_t *status)
{
    if (get_denormals_are_zeros(status)) {
        a = float32_denormal_to_zero(a);
        b = float32_denormal_to_zero(b);
    }

    float_class_t aClass = float32_class(a);
    float_class_t bClass = float32_class(b);

    if (aClass == float_SNaN || bClass == float_SNaN) {
        float_raise(status, float_flag_invalid);
        return float_relation_unordered;
    }

    if (aClass == float_QNaN || bClass == float_QNaN) {
        if (! quiet) float_raise(status, float_flag_invalid);
        return float_relation_unordered;
    }

    if (aClass == float_denormal || bClass == float_denormal) {
        float_raise(status, float_flag_denormal);
    }

    if ((a == b) || ((uint32_t) ((a | b)<<1) == 0)) return float_relation_equal;

    int aSign = extractFloat32Sign(a);
    int bSign = extractFloat32Sign(b);
    if (aSign != bSign)
        return (aSign) ? float_relation_less : float_relation_greater;

    if (aSign ^ (a < b)) return float_relation_less;
    return float_relation_greater;
}

/*----------------------------------------------------------------------------
| Compare between two single precision floating point numbers and return the
| smaller of them.
*----------------------------------------------------------------------------*/

float32 float32_min(float32 a, float32 b, float_status_t *status)
{
  if (get_denormals_are_zeros(status)) {
    a = float32_denormal_to_zero(a);
    b = float32_denormal_to_zero(b);
  }

  return (float32_compare(a, b, status) == float_relation_less) ? a : b;
}

/*----------------------------------------------------------------------------
| Compare between two single precision floating point numbers and return the
| larger of them.
*----------------------------------------------------------------------------*/

float32 float32_max(float32 a, float32 b, float_status_t *status)
{
  if (get_denormals_are_zeros(status)) {
    a = float32_denormal_to_zero(a);
    b = float32_denormal_to_zero(b);
  }

  return (float32_compare(a, b, status) == float_relation_greater) ? a : b;
}

/*----------------------------------------------------------------------------
| Compare between two  single precision  floating point numbers and  return the
| smaller/larger of them. The operation  is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_minmax(float32 a, float32 b, int is_max, int is_abs, float_status_t *status)
{
    if (get_denormals_are_zeros(status)) {
        a = float32_denormal_to_zero(a);
        b = float32_denormal_to_zero(b);
    }

    if (float32_is_nan(a) || float32_is_nan(b)) {
        if (float32_is_signaling_nan(a)) {
            return propagateFloat32NaN(a, status);
        }
        if (float32_is_signaling_nan(b) ) {
            return propagateFloat32NaN(b, status);
        }
        if (! float32_is_nan(b)) {
            if (float32_is_denormal(b))
                float_raise(status, float_flag_denormal);
            return b;
        }
        if (! float32_is_nan(a)) {
            if (float32_is_denormal(a))
                float_raise(status, float_flag_denormal);
            return a;
        }
        return propagateFloat32NaN_two_args(a, b, status);
    }

    float32 tmp_a = a, tmp_b = b;
    if (is_abs) {
        tmp_a &= ~0x80000000; // clear the sign bit
        tmp_b &= ~0x80000000;
    }

    int aSign = extractFloat32Sign(tmp_a);
    int bSign = extractFloat32Sign(tmp_b);

    if (float32_is_denormal(a) || float32_is_denormal(b))
        float_raise(status, float_flag_denormal);

    if (aSign != bSign) {
        if (! is_max) {
            return aSign ? a : b;
        } else {
            return aSign ? b : a;
        }
    } else {
        if (! is_max) {
            return (aSign ^ (tmp_a < tmp_b)) ? a : b;
        } else {
            return (aSign ^ (tmp_a < tmp_b)) ? b : a;
        }
    }
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 32-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic - which means in particular that the conversion is rounded
| according to the current rounding mode. If `a' is a NaN or the
| conversion overflows, the integer indefinite value is returned.
*----------------------------------------------------------------------------*/

int32_t float64_to_int32(float64 a, float_status_t *status)
{
    uint64_t aSig = extractFloat64Frac(a);
    int16_t aExp = extractFloat64Exp(a);
    int aSign = extractFloat64Sign(a);
    if ((aExp == 0x7FF) && aSig) aSign = 0;
    if (aExp) aSig |= U64(0x0010000000000000);
    else {
        if (get_denormals_are_zeros(status)) aSig = 0;
    }
    int shiftCount = 0x42C - aExp;
    if (0 < shiftCount) aSig = shift64RightJamming(aSig, shiftCount);
    return roundAndPackInt32(aSign, aSig, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 32-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN or the conversion overflows, the integer indefinite
| value  is returned.
*----------------------------------------------------------------------------*/

int32_t float64_to_int32_round_to_zero(float64 a, float_status_t *status)
{
    int aSign;
    int16_t aExp;
    uint64_t aSig, savedASig;
    int32_t z;
    int shiftCount;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);
    if (0x41E < aExp) {
        float_raise(status, float_flag_invalid);
        return (int32_t)(int32_indefinite);
    }
    else if (aExp < 0x3FF) {
        if (get_denormals_are_zeros(status) && aExp == 0) aSig = 0;
        if (aExp || aSig) float_raise(status, float_flag_inexact);
        return 0;
    }
    aSig |= U64(0x0010000000000000);
    shiftCount = 0x433 - aExp;
    savedASig = aSig;
    aSig >>= shiftCount;
    z = (int32_t) aSig;
    if (aSign) z = -z;
    if ((z < 0) ^ aSign) {
        float_raise(status, float_flag_invalid);
        return (int32_t)(int32_indefinite);
    }
    if ((aSig<<shiftCount) != savedASig) {
        float_raise(status, float_flag_inexact);
    }
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 32-bit unsigned integer format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-point Arithmetic,
| except that the conversion is always rounded toward zero.  If `a' is a NaN
| or conversion overflows, the largest positive integer is returned.
*----------------------------------------------------------------------------*/

uint32_t float64_to_uint32_round_to_zero(float64 a, float_status_t *status)
{
    uint64_t aSig = extractFloat64Frac(a);
    int16_t aExp = extractFloat64Exp(a);
    int aSign = extractFloat64Sign(a);

    if (aExp < 0x3FF) {
        if (get_denormals_are_zeros(status) && aExp == 0) aSig = 0;
        if (aExp || aSig) float_raise(status, float_flag_inexact);
        return 0;
    }

    if (0x41E < aExp || aSign) {
        float_raise(status, float_flag_invalid);
        return uint32_indefinite;
    }

    aSig |= U64(0x0010000000000000);
    int shiftCount = 0x433 - aExp;
    uint64_t savedASig = aSig;
    aSig >>= shiftCount;
    if ((aSig<<shiftCount) != savedASig) {
        float_raise(status, float_flag_inexact);
    }
    return (uint32_t) aSig;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 64-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic - which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  Otherwise, if the conversion overflows, the
| largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int64_t float64_to_int64(float64 a, float_status_t *status)
{
    int aSign;
    int16_t aExp;
    uint64_t aSig, aSigExtra;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);
    if (aExp) aSig |= U64(0x0010000000000000);
    else {
        if (get_denormals_are_zeros(status)) aSig = 0;
    }
    int shiftCount = 0x433 - aExp;
    if (shiftCount <= 0) {
        if (0x43E < aExp) {
            float_raise(status, float_flag_invalid);
            return (int64_t)(int64_indefinite);
        }
        aSigExtra = 0;
        aSig <<= -shiftCount;
    }
    else {
        shift64ExtraRightJamming(aSig, 0, shiftCount, &aSig, &aSigExtra);
    }
    return roundAndPackInt64(aSign, aSig, aSigExtra, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 64-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN or the conversion overflows, the integer indefinite
| value  is returned.
*----------------------------------------------------------------------------*/

int64_t float64_to_int64_round_to_zero(float64 a, float_status_t *status)
{
    int aSign;
    int16_t aExp;
    uint64_t aSig;
    int64_t z;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);
    if (aExp) aSig |= U64(0x0010000000000000);
    int shiftCount = aExp - 0x433;
    if (0 <= shiftCount) {
        if (0x43E <= aExp) {
            if (a != U64(0xC3E0000000000000)) {
                float_raise(status, float_flag_invalid);
            }
            return (int64_t)(int64_indefinite);
        }
        z = aSig<<shiftCount;
    }
    else {
        if (aExp < 0x3FE) {
            if (get_denormals_are_zeros(status) && aExp == 0) aSig = 0;
            if (aExp | aSig) float_raise(status, float_flag_inexact);
            return 0;
        }
        z = aSig>>(-shiftCount);
        if ((uint64_t) (aSig<<(shiftCount & 63))) {
             float_raise(status, float_flag_inexact);
        }
    }
    if (aSign) z = -z;
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 64-bit unsigned integer format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic,
| except that the conversion is always rounded toward zero. If `a' is a NaN
| or the conversion overflows, the largest unsigned integer is returned.
*----------------------------------------------------------------------------*/

uint64_t float64_to_uint64_round_to_zero(float64 a, float_status_t *status)
{
    int aSign;
    int16_t aExp;
    uint64_t aSig, z;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);

    if (aExp < 0x3FE) {
        if (get_denormals_are_zeros(status) && aExp == 0) aSig = 0;
        if (aExp | aSig) float_raise(status, float_flag_inexact);
        return 0;
    }

    if (0x43E <= aExp || aSign) {
        float_raise(status, float_flag_invalid);
        return uint64_indefinite;
    }

    if (aExp) aSig |= U64(0x0010000000000000);
    int shiftCount = aExp - 0x433;

    if (0 <= shiftCount) {
        z = aSig<<shiftCount;
    }
    else {
        z = aSig>>(-shiftCount);
        if ((uint64_t) (aSig<<(shiftCount & 63))) {
             float_raise(status, float_flag_inexact);
        }
    }
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 32-bit unsigned integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN or the conversion
| overflows, the largest unsigned integer is returned.
*----------------------------------------------------------------------------*/

uint32_t float64_to_uint32(float64 a, float_status_t *status)
{
    uint64_t val_64 = float64_to_uint64(a, status);

    if (val_64 > 0xffffffff) {
        status->float_exception_flags = float_flag_invalid; // throw away other flags
        return uint32_indefinite;
    }

    return (uint32_t) val_64;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 64-bit unsigned integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN or the conversion
| overflows, the largest unsigned integer is returned.
*----------------------------------------------------------------------------*/
 
uint64_t float64_to_uint64(float64 a, float_status_t *status)
{
    int aSign;
    int16_t aExp, shiftCount;
    uint64_t aSig, aSigExtra;
 
    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
    }

    if (aSign && (aExp > 0x3FE)) {
        float_raise(status, float_flag_invalid);
        return uint64_indefinite;
    }

    if (aExp) {
        aSig |= U64(0x0010000000000000);
    }
    shiftCount = 0x433 - aExp;
    if (shiftCount <= 0) {
        if (0x43E < aExp) {
            float_raise(status, float_flag_invalid);
            return uint64_indefinite;
        }
        aSigExtra = 0;
        aSig <<= -shiftCount;
    } else {
        shift64ExtraRightJamming(aSig, 0, shiftCount, &aSig, &aSigExtra);
    }

    return roundAndPackUint64(aSign, aSig, aSigExtra, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the single-precision floating-point format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float32 float64_to_float32(float64 a, float_status_t *status)
{
    int aSign;
    int16_t aExp;
    uint64_t aSig;
    uint32_t zSig;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);
    if (aExp == 0x7FF) {
        if (aSig) return commonNaNToFloat32(float64ToCommonNaN(a, status));
        return packFloat32(aSign, 0xFF, 0);
    }
    if (aExp == 0) {
        if (aSig == 0 || get_denormals_are_zeros(status))
            return packFloat32(aSign, 0, 0);
        float_raise(status, float_flag_denormal);
    }
    aSig = shift64RightJamming(aSig, 22);
    zSig = (uint32_t) aSig;
    if (aExp || zSig) {
        zSig |= 0x40000000;
        aExp -= 0x381;
    }
    return roundAndPackFloat32(aSign, aExp, zSig, status);
}

/*----------------------------------------------------------------------------
| Rounds the double-precision floating-point value `a' to an integer, and
| returns the result as a double-precision floating-point value.  The
| operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_round_to_int_with_scale(float64 a, uint8_t scale, float_status_t *status)
{
    uint64_t lastBitMask, roundBitsMask;
    int roundingMode = get_float_rounding_mode(status);
    int16_t aExp = extractFloat64Exp(a);
    scale &= 0xf;

    if ((aExp == 0x7FF) && extractFloat64Frac(a)) {
        return propagateFloat64NaN(a, status);
    }

    aExp += scale; // scale the exponent

    if (0x433 <= aExp) {
        return a;
    }

    if (get_denormals_are_zeros(status)) {
        a = float64_denormal_to_zero(a);
    }

    if (aExp < 0x3FF) {
        if ((uint64_t) (a<<1) == 0) return a;
        float_raise(status, float_flag_inexact);
        int aSign = extractFloat64Sign(a);
        switch (roundingMode) {
         case float_round_nearest_even:
            if ((aExp == 0x3FE) && extractFloat64Frac(a)) {
              return packFloat64(aSign, 0x3FF - scale, 0);
            }
            break;
         case float_round_down:
            return aSign ? packFloat64(1, 0x3FF - scale, 0) : float64_positive_zero;
         case float_round_up:
            return aSign ? float64_negative_zero : packFloat64(0, 0x3FF - scale, 0);
        }
        return packFloat64(aSign, 0, 0);
    }

    lastBitMask = 1;
    lastBitMask <<= 0x433 - aExp;
    roundBitsMask = lastBitMask - 1;
    float64 z = a;
    if (roundingMode == float_round_nearest_even) {
        z += lastBitMask>>1;
        if ((z & roundBitsMask) == 0) z &= ~lastBitMask;
    }
    else if (roundingMode != float_round_to_zero) {
        if (extractFloat64Sign(z) ^ (roundingMode == float_round_up)) {
            z += roundBitsMask;
        }
    }
    z &= ~roundBitsMask;
    if (z != a) float_raise(status, float_flag_inexact);
    return z;
}

/*----------------------------------------------------------------------------
| Extracts the fractional portion of double-precision floating-point value `a',
| and returns the result  as a  double-precision  floating-point value. The
| fractional results are precise. The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_frc(float64 a, float_status_t *status)
{
    int roundingMode = get_float_rounding_mode(status);

    uint64_t aSig = extractFloat64Frac(a);
    int16_t aExp = extractFloat64Exp(a);
    int aSign = extractFloat64Sign(a);

    if (aExp == 0x7FF) {
        if (aSig) return propagateFloat64NaN(a, status);
        float_raise(status, float_flag_invalid);
        return float64_default_nan;
    }

    if (aExp >= 0x433) {
        return packFloat64(roundingMode == float_round_down, 0, 0);
    }

    if (aExp < 0x3FF) {
        if (aExp == 0) {
            if (aSig == 0 || get_denormals_are_zeros(status))
                return packFloat64(roundingMode == float_round_down, 0, 0);

            float_raise(status, float_flag_denormal);
            if (! float_exception_masked(status, float_flag_underflow))
                float_raise(status, float_flag_underflow);

            if(get_flush_underflow_to_zero(status)) {
                float_raise(status, float_flag_underflow | float_flag_inexact);
                return packFloat64(aSign, 0, 0);
            }
        }
        return a;
    }

    uint64_t lastBitMask = U64(1) << (0x433 - aExp);
    uint64_t roundBitsMask = lastBitMask - 1;

    aSig &= roundBitsMask;
    aSig <<= 10;
    aExp--;

    if (aSig == 0)
       return packFloat64(roundingMode == float_round_down, 0, 0);

    return normalizeRoundAndPackFloat64(aSign, aExp, aSig, status);
}

/*----------------------------------------------------------------------------
| Extracts the exponent portion of double-precision floating-point value 'a',
| and returns the result as a double-precision floating-point value
| representing unbiased integer exponent. The operation is performed according
| to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_getexp(float64 a, float_status_t *status)
{
    int16_t aExp = extractFloat64Exp(a);
    uint64_t aSig = extractFloat64Frac(a);

    if (aExp == 0x7FF) {
        if (aSig) return propagateFloat64NaN(a, status);
        return float64_positive_inf;
    }

    if (aExp == 0) {
        if (aSig == 0 || get_denormals_are_zeros(status))
            return float64_negative_inf;

        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(aSig, &aExp, &aSig);
    }

    return int32_to_float64(aExp - 0x3FF);
}

/*----------------------------------------------------------------------------
| Extracts the mantissa of double-precision floating-point value 'a' and
| returns the result as a double-precision floating-point after applying
| the mantissa interval normalization and sign control. The operation is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_getmant(float64 a, float_status_t *status, int sign_ctrl, int interv)
{
    int16_t aExp = extractFloat64Exp(a);
    uint64_t aSig = extractFloat64Frac(a);
    int aSign = extractFloat64Sign(a);

    if (aExp == 0x7FF) {
        if (aSig) return propagateFloat64NaN(a, status);
        if (aSign) {
            if (sign_ctrl & 0x2) {
                float_raise(status, float_flag_invalid);
                return float64_default_nan;
            }
        }
        return packFloat64(~sign_ctrl & aSign, 0x3FF, 0);
    }

    if (aExp == 0 && (aSig == 0 || get_denormals_are_zeros(status))) {
        return packFloat64(~sign_ctrl & aSign, 0x3FF, 0);
    }

    if (aSign) {
        if (sign_ctrl & 0x2) {
            float_raise(status, float_flag_invalid);
            return float64_default_nan;
        }
    }

    if (aExp == 0) {
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(aSig, &aExp, &aSig);
//      aExp += 0x3FE;
        aSig &= U64(0xFFFFFFFFFFFFFFFF);
    }

    switch(interv) {
    case 0x0: // interval [1,2)
        aExp = 0x3FF;
        break;
    case 0x1: // interval [1/2,2)
        aExp -= 0x3FF;
        aExp  = 0x3FF - (aExp & 0x1);
        break;
    case 0x2: // interval [1/2,1)
        aExp = 0x3FE;
        break;
    case 0x3: // interval [3/4,3/2)
        aExp = 0x3FF - ((aSig >> 51) & 0x1);
        break;
    }

    return packFloat64(~sign_ctrl & aSign, aExp, aSig);
}

/*----------------------------------------------------------------------------
| Return the result of a floating point scale of the double-precision floating
| point value `a' by multiplying it by 2 power of the double-precision
| floating point value 'b' converted to integral value. If the result cannot
| be represented in double precision, then the proper overflow response (for
| positive scaling operand), or the proper underflow response (for negative
| scaling operand) is issued. The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_scalef(float64 a, float64 b, float_status_t *status)
{
    uint64_t aSig = extractFloat64Frac(a);
    int16_t aExp = extractFloat64Exp(a);
    int aSign = extractFloat64Sign(a);
    uint64_t bSig = extractFloat64Frac(b);
    int16_t bExp = extractFloat64Exp(b);
    int bSign = extractFloat64Sign(b);

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
    }

    if (bExp == 0x7FF) {
        if (bSig) return propagateFloat64NaN_two_args(a, b, status);
    }

    if (aExp == 0x7FF) {
        if (aSig) {
            int aIsSignalingNaN = (aSig & U64(0x0008000000000000)) == 0;
            if (aIsSignalingNaN || bExp != 0x7FF || bSig)
                return propagateFloat64NaN_two_args(a, b, status);

            return bSign ? 0 : float64_positive_inf;
        }

        if (bExp == 0x7FF && bSign) {
            float_raise(status, float_flag_invalid);
            return float64_default_nan;
        }
        return a;
    }

    if (aExp == 0) {
        if (aSig == 0) {
            if (bExp == 0x7FF && ! bSign) {
                float_raise(status, float_flag_invalid);
                return float64_default_nan;
            }
            return a;
        }
        float_raise(status, float_flag_denormal);
    }

    if ((bExp | bSig) == 0) return a;

    if (bExp == 0x7FF) {
        if (bSign) return packFloat64(aSign, 0, 0);
        return packFloat64(aSign, 0x7FF, 0);
    }

    if (0x40F <= bExp) {
        // handle obvious overflow/underflow result
        return roundAndPackFloat64(aSign, bSign ? -0x3FF : 0x7FF, aSig, status);
    }

    int scale = 0;

    if (bExp < 0x3FF) {
        if (bExp == 0)
            float_raise(status, float_flag_denormal);
        scale = -bSign;
    }
    else {
        bSig |= U64(0x0010000000000000);
        int shiftCount = 0x433 - bExp;
        uint64_t savedBSig = bSig;
        bSig >>= shiftCount;
        scale = (int32_t) bSig;
        if (bSign) {
            if ((bSig<<shiftCount) != savedBSig) scale++;
            scale = -scale;
        }

        if (scale >  0x1000) scale =  0x1000;
        if (scale < -0x1000) scale = -0x1000;
    }

    if (aExp != 0) {
        aSig |= U64(0x0010000000000000);
    } else {
        aExp++;
    }

    aExp += scale - 1;
    aSig <<= 10;
    return normalizeRoundAndPackFloat64(aSign, aExp, aSig, status);
}

/*----------------------------------------------------------------------------
| Returns the result of adding the absolute values of the double-precision
| floating-point values `a' and `b'.  If `zSign' is 1, the sum is negated
| before being returned.  `zSign' is ignored if the result is a NaN.
| The addition is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float64 addFloat64Sigs(float64 a, float64 b, int zSign, float_status_t *status)
{
    int16_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig;
    int16_t expDiff;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    bSig = extractFloat64Frac(b);
    bExp = extractFloat64Exp(b);

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
    }

    expDiff = aExp - bExp;
    aSig <<= 9;
    bSig <<= 9;
    if (0 < expDiff) {
        if (aExp == 0x7FF) {
            if (aSig) return propagateFloat64NaN_two_args(a, b, status);
            if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
            return a;
        }
        if ((aExp == 0) && aSig)
            float_raise(status, float_flag_denormal);

        if (bExp == 0) {
            if (bSig) float_raise(status, float_flag_denormal);
            --expDiff;
        }
        else bSig |= U64(0x2000000000000000);

        bSig = shift64RightJamming(bSig, expDiff);
        zExp = aExp;
    }
    else if (expDiff < 0) {
        if (bExp == 0x7FF) {
            if (bSig) return propagateFloat64NaN_two_args(a, b, status);
            if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
            return packFloat64(zSign, 0x7FF, 0);
        }
        if ((bExp == 0) && bSig)
            float_raise(status, float_flag_denormal);

        if (aExp == 0) {
            if (aSig) float_raise(status, float_flag_denormal);
            ++expDiff;
        }
        else aSig |= U64(0x2000000000000000);

        aSig = shift64RightJamming(aSig, -expDiff);
        zExp = bExp;
    }
    else {
        if (aExp == 0x7FF) {
            if (aSig | bSig) return propagateFloat64NaN_two_args(a, b, status);
            return a;
        }
        if (aExp == 0) {
            zSig = (aSig + bSig) >> 9;
            if (aSig | bSig) {
                float_raise(status, float_flag_denormal);
                if (get_flush_underflow_to_zero(status) && (extractFloat64Frac(zSig) == zSig)) {
                    float_raise(status, float_flag_underflow | float_flag_inexact);
                    return packFloat64(zSign, 0, 0);
                }
                if (! float_exception_masked(status, float_flag_underflow)) {
                    if (extractFloat64Frac(zSig) == zSig)
                        float_raise(status, float_flag_underflow);
                }
            }
            return packFloat64(zSign, 0, zSig);
        }
        zSig = U64(0x4000000000000000) + aSig + bSig;
        return roundAndPackFloat64(zSign, aExp, zSig, status);
    }
    aSig |= U64(0x2000000000000000);
    zSig = (aSig + bSig)<<1;
    --zExp;
    if ((int64_t) zSig < 0) {
        zSig = aSig + bSig;
        ++zExp;
    }
    return roundAndPackFloat64(zSign, zExp, zSig, status);
}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the absolute values of the double-
| precision floating-point values `a' and `b'.  If `zSign' is 1, the
| difference is negated before being returned.  `zSign' is ignored if the
| result is a NaN.  The subtraction is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float64 subFloat64Sigs(float64 a, float64 b, int zSign, float_status_t *status)
{
    int16_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig;
    int16_t expDiff;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    bSig = extractFloat64Frac(b);
    bExp = extractFloat64Exp(b);

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
    }

    expDiff = aExp - bExp;
    aSig <<= 10;
    bSig <<= 10;
    if (0 < expDiff) goto aExpBigger;
    if (expDiff < 0) goto bExpBigger;
    if (aExp == 0x7FF) {
        if (aSig | bSig) return propagateFloat64NaN_two_args(a, b, status);
        float_raise(status, float_flag_invalid);
        return float64_default_nan;
    }
    if (aExp == 0) {
        if (aSig | bSig) float_raise(status, float_flag_denormal);
        aExp = 1;
        bExp = 1;
    }
    if (bSig < aSig) goto aBigger;
    if (aSig < bSig) goto bBigger;
    return packFloat64(get_float_rounding_mode(status) == float_round_down, 0, 0);
 bExpBigger:
    if (bExp == 0x7FF) {
        if (bSig) return propagateFloat64NaN_two_args(a, b, status);
        if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
        return packFloat64(zSign ^ 1, 0x7FF, 0);
    }
    if ((bExp == 0) && bSig)
        float_raise(status, float_flag_denormal);

    if (aExp == 0) {
        if (aSig) float_raise(status, float_flag_denormal);
        ++expDiff;
    }
    else aSig |= U64(0x4000000000000000);

    aSig = shift64RightJamming(aSig, -expDiff);
    bSig |= U64(0x4000000000000000);
 bBigger:
    zSig = bSig - aSig;
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if (aExp == 0x7FF) {
        if (aSig) return propagateFloat64NaN_two_args(a, b, status);
        if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
        return a;
    }
    if ((aExp == 0) && aSig)
        float_raise(status, float_flag_denormal);

    if (bExp == 0) {
        if (bSig) float_raise(status, float_flag_denormal);
        --expDiff;
    }
    else bSig |= U64(0x4000000000000000);

    bSig = shift64RightJamming(bSig, expDiff);
    aSig |= U64(0x4000000000000000);
 aBigger:
    zSig = aSig - bSig;
    zExp = aExp;
 normalizeRoundAndPack:
    --zExp;
    return normalizeRoundAndPackFloat64(zSign, zExp, zSig, status);
}

/*----------------------------------------------------------------------------
| Returns the result of adding the double-precision floating-point values `a'
| and `b'.  The operation is performed according to the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_add(float64 a, float64 b, float_status_t *status)
{
    int aSign = extractFloat64Sign(a);
    int bSign = extractFloat64Sign(b);

    if (aSign == bSign) {
        return addFloat64Sigs(a, b, aSign, status);
    }
    else {
        return subFloat64Sigs(a, b, aSign, status);
    }
}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the double-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_sub(float64 a, float64 b, float_status_t *status)
{
    int aSign = extractFloat64Sign(a);
    int bSign = extractFloat64Sign(b);

    if (aSign == bSign) {
        return subFloat64Sigs(a, b, aSign, status);
    }
    else {
        return addFloat64Sigs(a, b, aSign, status);
    }
}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the double-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_mul(float64 a, float64 b, float_status_t *status)
{
    int aSign, bSign, zSign;
    int16_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);
    bSig = extractFloat64Frac(b);
    bExp = extractFloat64Exp(b);
    bSign = extractFloat64Sign(b);
    zSign = aSign ^ bSign;

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
    }

    if (aExp == 0x7FF) {
        if (aSig || ((bExp == 0x7FF) && bSig)) {
            return propagateFloat64NaN_two_args(a, b, status);
        }
        if ((bExp | bSig) == 0) {
            float_raise(status, float_flag_invalid);
            return float64_default_nan;
        }
        if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
        return packFloat64(zSign, 0x7FF, 0);
    }
    if (bExp == 0x7FF) {
        if (bSig) return propagateFloat64NaN_two_args(a, b, status);
        if ((aExp | aSig) == 0) {
            float_raise(status, float_flag_invalid);
            return float64_default_nan;
        }
        if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
        return packFloat64(zSign, 0x7FF, 0);
    }
    if (aExp == 0) {
        if (aSig == 0) {
            if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
            return packFloat64(zSign, 0, 0);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        if (bSig == 0) return packFloat64(zSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(bSig, &bExp, &bSig);
    }
    zExp = aExp + bExp - 0x3FF;
    aSig = (aSig | U64(0x0010000000000000))<<10;
    bSig = (bSig | U64(0x0010000000000000))<<11;
    mul64To128(aSig, bSig, &zSig0, &zSig1);
    zSig0 |= (zSig1 != 0);
    if (0 <= (int64_t) (zSig0<<1)) {
        zSig0 <<= 1;
        --zExp;
    }
    return roundAndPackFloat64(zSign, zExp, zSig0, status);
}

/*----------------------------------------------------------------------------
| Returns the result of dividing the double-precision floating-point value `a'
| by the corresponding value `b'.  The operation is performed according to
| the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_div(float64 a, float64 b, float_status_t *status)
{
    int aSign, bSign, zSign;
    int16_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig;
    uint64_t rem0, rem1;
    uint64_t term0, term1;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);
    bSig = extractFloat64Frac(b);
    bExp = extractFloat64Exp(b);
    bSign = extractFloat64Sign(b);
    zSign = aSign ^ bSign;

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
    }

    if (aExp == 0x7FF) {
        if (aSig) return propagateFloat64NaN_two_args(a, b, status);
        if (bExp == 0x7FF) {
            if (bSig) return propagateFloat64NaN_two_args(a, b, status);
            float_raise(status, float_flag_invalid);
            return float64_default_nan;
        }
        if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
        return packFloat64(zSign, 0x7FF, 0);
    }
    if (bExp == 0x7FF) {
        if (bSig) return propagateFloat64NaN_two_args(a, b, status);
        if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
        return packFloat64(zSign, 0, 0);
    }
    if (bExp == 0) {
        if (bSig == 0) {
            if ((aExp | aSig) == 0) {
                float_raise(status, float_flag_invalid);
                return float64_default_nan;
            }
            float_raise(status, float_flag_divbyzero);
            return packFloat64(zSign, 0x7FF, 0);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(bSig, &bExp, &bSig);
    }
    if (aExp == 0) {
        if (aSig == 0) return packFloat64(zSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(aSig, &aExp, &aSig);
    }
    zExp = aExp - bExp + 0x3FD;
    aSig = (aSig | U64(0x0010000000000000))<<10;
    bSig = (bSig | U64(0x0010000000000000))<<11;
    if (bSig <= (aSig + aSig)) {
        aSig >>= 1;
        ++zExp;
    }
    zSig = estimateDiv128To64(aSig, 0, bSig);
    if ((zSig & 0x1FF) <= 2) {
        mul64To128(bSig, zSig, &term0, &term1);
        sub128(aSig, 0, term0, term1, &rem0, &rem1);
        while ((int64_t) rem0 < 0) {
            --zSig;
            add128(rem0, rem1, 0, bSig, &rem0, &rem1);
        }
        zSig |= (rem1 != 0);
    }
    return roundAndPackFloat64(zSign, zExp, zSig, status);
}

/*----------------------------------------------------------------------------
| Returns the square root of the double-precision floating-point value `a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_sqrt(float64 a, float_status_t *status)
{
    int aSign;
    int16_t aExp, zExp;
    uint64_t aSig, zSig, doubleZSig;
    uint64_t rem0, rem1, term0, term1;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);

    if (aExp == 0x7FF) {
        if (aSig) return propagateFloat64NaN(a, status);
        if (! aSign) return a;
        float_raise(status, float_flag_invalid);
        return float64_default_nan;
    }

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
    }

    if (aSign) {
        if ((aExp | aSig) == 0) return packFloat64(aSign, 0, 0);
        float_raise(status, float_flag_invalid);
        return float64_default_nan;
    }
    if (aExp == 0) {
        if (aSig == 0) return 0;
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(aSig, &aExp, &aSig);
    }
    zExp = ((aExp - 0x3FF)>>1) + 0x3FE;
    aSig |= U64(0x0010000000000000);
    zSig = estimateSqrt32(aExp, (uint32_t)(aSig>>21));
    aSig <<= 9 - (aExp & 1);
    zSig = estimateDiv128To64(aSig, 0, zSig<<32) + (zSig<<30);
    if ((zSig & 0x1FF) <= 5) {
        doubleZSig = zSig<<1;
        mul64To128(zSig, zSig, &term0, &term1);
        sub128(aSig, 0, term0, term1, &rem0, &rem1);
        while ((int64_t) rem0 < 0) {
            --zSig;
            doubleZSig -= 2;
            add128(rem0, rem1, zSig>>63, doubleZSig | 1, &rem0, &rem1);
        }
        zSig |= ((rem0 | rem1) != 0);
    }
    return roundAndPackFloat64(0, zExp, zSig, status);
}

/*----------------------------------------------------------------------------
| Determine double-precision floating-point number class
*----------------------------------------------------------------------------*/

float_class_t float64_class(float64 a)
{
   int16_t aExp = extractFloat64Exp(a);
   uint64_t aSig = extractFloat64Frac(a);
   int  aSign = extractFloat64Sign(a);

   if(aExp == 0x7FF) {
       if (aSig == 0)
           return (aSign) ? float_negative_inf : float_positive_inf;

       return (aSig & U64(0x0008000000000000)) ? float_QNaN : float_SNaN;
   }

   if(aExp == 0) {
       if (aSig == 0)
           return float_zero;
       return float_denormal;
   }

   return float_normalized;
}

/*----------------------------------------------------------------------------
| Compare  between  two  double  precision  floating  point  numbers. Returns
| 'float_relation_equal'  if the operands are equal, 'float_relation_less' if
| the    value    'a'   is   less   than   the   corresponding   value   `b',
| 'float_relation_greater' if the value 'a' is greater than the corresponding
| value `b', or 'float_relation_unordered' otherwise.
*----------------------------------------------------------------------------*/

int float64_compare_internal(float64 a, float64 b, int quiet, float_status_t *status)
{
    if (get_denormals_are_zeros(status)) {
        a = float64_denormal_to_zero(a);
        b = float64_denormal_to_zero(b);
    }

    float_class_t aClass = float64_class(a);
    float_class_t bClass = float64_class(b);

    if (aClass == float_SNaN || bClass == float_SNaN) {
        float_raise(status, float_flag_invalid);
        return float_relation_unordered;
    }

    if (aClass == float_QNaN || bClass == float_QNaN) {
        if (! quiet) float_raise(status, float_flag_invalid);
        return float_relation_unordered;
    }

    if (aClass == float_denormal || bClass == float_denormal) {
        float_raise(status, float_flag_denormal);
    }

    if ((a == b) || ((uint64_t) ((a | b)<<1) == 0)) return float_relation_equal;

    int aSign = extractFloat64Sign(a);
    int bSign = extractFloat64Sign(b);
    if (aSign != bSign)
        return (aSign) ? float_relation_less : float_relation_greater;

    if (aSign ^ (a < b)) return float_relation_less;
    return float_relation_greater;
}

/*----------------------------------------------------------------------------
| Compare between two double precision floating point numbers and return the
| smaller of them.
*----------------------------------------------------------------------------*/

float64 float64_min(float64 a, float64 b, float_status_t *status)
{
  if (get_denormals_are_zeros(status)) {
    a = float64_denormal_to_zero(a);
    b = float64_denormal_to_zero(b);
  }

  return (float64_compare(a, b, status) == float_relation_less) ? a : b;
}

/*----------------------------------------------------------------------------
| Compare between two double precision floating point numbers and return the
| larger of them.
*----------------------------------------------------------------------------*/

float64 float64_max(float64 a, float64 b, float_status_t *status)
{
  if (get_denormals_are_zeros(status)) {
    a = float64_denormal_to_zero(a);
    b = float64_denormal_to_zero(b);
  }

  return (float64_compare(a, b, status) == float_relation_greater) ? a : b;
}

/*----------------------------------------------------------------------------
| Compare between two  double precision  floating point numbers and  return the
| smaller/larger of them. The operation  is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_minmax(float64 a, float64 b, int is_max, int is_abs, float_status_t *status)
{
    if (get_denormals_are_zeros(status)) {
        a = float64_denormal_to_zero(a);
        b = float64_denormal_to_zero(b);
    }

    if (float64_is_nan(a) || float64_is_nan(b)) {
        if (float64_is_signaling_nan(a)) {
            return propagateFloat64NaN(a, status);
        }
        if (float64_is_signaling_nan(b)) {
            return propagateFloat64NaN(b, status);
        }
        if (! float64_is_nan(b)) {
            if (float64_is_denormal(b))
                float_raise(status, float_flag_denormal);
            return b;
        }
        if (! float64_is_nan(a)) {
            if (float64_is_denormal(a))
                float_raise(status, float_flag_denormal);
            return a;
        }
        return propagateFloat64NaN_two_args(a, b, status);
    }

    float64 tmp_a = a, tmp_b = b;
    if (is_abs) {
        tmp_a &= ~U64(0x8000000000000000); // clear the sign bit
        tmp_b &= ~U64(0x8000000000000000);
    }

    int aSign = extractFloat64Sign(tmp_a);
    int bSign = extractFloat64Sign(tmp_b);

    if (float64_is_denormal(a) || float64_is_denormal(b))
        float_raise(status, float_flag_denormal);

    if (aSign != bSign) {
        if (! is_max) {
            return aSign ? a : b;
        } else {
            return aSign ? b : a;
        }
    } else {
        if (! is_max) {
            return (aSign ^ (tmp_a < tmp_b)) ? a : b;
        } else {
            return (aSign ^ (tmp_a < tmp_b)) ? b : a;
        }
    }
}

#ifdef FLOATX80

/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit two's complement integer `a'
| to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 int32_to_floatx80(int32_t a)
{
    if (a == 0) return packFloatx80(0, 0, 0);
    int   zSign = (a < 0);
    uint32_t absA = zSign ? -a : a;
    int    shiftCount = countLeadingZeros32(absA) + 32;
    uint64_t zSig = absA;
    return packFloatx80(zSign, 0x403E - shiftCount, zSig<<shiftCount);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit two's complement integer `a'
| to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 int64_to_floatx80(int64_t a)
{
    if (a == 0) return packFloatx80(0, 0, 0);
    int   zSign = (a < 0);
    uint64_t absA = zSign ? -a : a;
    int    shiftCount = countLeadingZeros64(absA);
    return packFloatx80(zSign, 0x403E - shiftCount, absA<<shiftCount);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 float32_to_floatx80(float32 a, float_status_t *status)
{
    uint32_t aSig = extractFloat32Frac(a);
    int16_t aExp = extractFloat32Exp(a);
    int aSign = extractFloat32Sign(a);
    if (aExp == 0xFF) {
        if (aSig) return commonNaNToFloatx80(float32ToCommonNaN(a, status));
        return packFloatx80(aSign, 0x7FFF, U64(0x8000000000000000));
    }
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(aSig, &aExp, &aSig);
    }
    aSig |= 0x00800000;
    return packFloatx80(aSign, aExp + 0x3F80, ((uint64_t) aSig)<<40);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 float64_to_floatx80(float64 a, float_status_t *status)
{
    uint64_t aSig = extractFloat64Frac(a);
    int16_t aExp = extractFloat64Exp(a);
    int aSign = extractFloat64Sign(a);

    if (aExp == 0x7FF) {
        if (aSig) return commonNaNToFloatx80(float64ToCommonNaN(a, status));
        return packFloatx80(aSign, 0x7FFF, U64(0x8000000000000000));
    }
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(aSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(aSig, &aExp, &aSig);
    }
    return
        packFloatx80(
            aSign, aExp + 0x3C00, (aSig | U64(0x0010000000000000))<<11);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 32-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic - which means in particular that the conversion
| is rounded according to the current rounding mode. If `a' is a NaN or the
| conversion overflows, the integer indefinite value is returned.
*----------------------------------------------------------------------------*/

int32_t floatx80_to_int32(floatx80 a, float_status_t *status)
{
    uint64_t aSig = extractFloatx80Frac(a);
    int32_t aExp = extractFloatx80Exp(a);
    int aSign = extractFloatx80Sign(a);

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a))
    {
        float_raise(status, float_flag_invalid);
        return int32_indefinite;
    }

    if ((aExp == 0x7FFF) && (uint64_t) (aSig<<1)) aSign = 0;
    int shiftCount = 0x4037 - aExp;
    if (shiftCount <= 0) shiftCount = 1;
    aSig = shift64RightJamming(aSig, shiftCount);
    return roundAndPackInt32(aSign, aSig, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 32-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic, except that the conversion is always rounded
| toward zero.  If `a' is a NaN or the conversion overflows, the integer
| indefinite value is returned.
*----------------------------------------------------------------------------*/

int32_t floatx80_to_int32_round_to_zero(floatx80 a, float_status_t *status)
{
    int32_t aExp;
    uint64_t aSig, savedASig;
    int32_t z;
    int shiftCount;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a))
    {
        float_raise(status, float_flag_invalid);
        return int32_indefinite;
    }

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    int aSign = extractFloatx80Sign(a);

    if (aExp > 0x401E) {
        float_raise(status, float_flag_invalid);
        return (int32_t)(int32_indefinite);
    }
    if (aExp < 0x3FFF) {
        if (aExp || aSig) float_raise(status, float_flag_inexact);
        return 0;
    }
    shiftCount = 0x403E - aExp;
    savedASig = aSig;
    aSig >>= shiftCount;
    z = (int32_t) aSig;
    if (aSign) z = -z;
    if ((z < 0) ^ aSign) {
        float_raise(status, float_flag_invalid);
        return (int32_t)(int32_indefinite);
    }
    if ((aSig<<shiftCount) != savedASig)
    {
        float_raise(status, float_flag_inexact);
    }
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 64-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic - which means in particular that the conversion
| is rounded according to the current rounding mode. If `a' is a NaN or the
| conversion overflows, the integer indefinite value is returned.
*----------------------------------------------------------------------------*/

int64_t floatx80_to_int64(floatx80 a, float_status_t *status)
{
    int32_t aExp;
    uint64_t aSig, aSigExtra;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a))
    {
        float_raise(status, float_flag_invalid);
        return int64_indefinite;
    }

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    int aSign = extractFloatx80Sign(a);

    int shiftCount = 0x403E - aExp;
    if (shiftCount <= 0)
    {
        if (shiftCount)
        {
            float_raise(status, float_flag_invalid);
            return (int64_t)(int64_indefinite);
        }
        aSigExtra = 0;
    }
    else {
        shift64ExtraRightJamming(aSig, 0, shiftCount, &aSig, &aSigExtra);
    }

    return roundAndPackInt64(aSign, aSig, aSigExtra, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 64-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic, except that the conversion is always rounded
| toward zero.  If `a' is a NaN or the conversion overflows, the integer
| indefinite value is returned.
*----------------------------------------------------------------------------*/

int64_t floatx80_to_int64_round_to_zero(floatx80 a, float_status_t *status)
{
    int aSign;
    int32_t aExp;
    uint64_t aSig;
    int64_t z;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a))
    {
        float_raise(status, float_flag_invalid);
        return int64_indefinite;
    }

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    int shiftCount = aExp - 0x403E;
    if (0 <= shiftCount) {
        aSig &= U64(0x7FFFFFFFFFFFFFFF);
        if ((a.exp != 0xC03E) || aSig) {
            float_raise(status, float_flag_invalid);
        }
        return (int64_t)(int64_indefinite);
    }
    else if (aExp < 0x3FFF) {
        if (aExp | aSig) float_raise(status, float_flag_inexact);
        return 0;
    }
    z = aSig>>(-shiftCount);
    if ((uint64_t) (aSig<<(shiftCount & 63))) {
        float_raise(status, float_flag_inexact);
    }
    if (aSign) z = -z;
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the single-precision floating-point format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 floatx80_to_float32(floatx80 a, float_status_t *status)
{
    uint64_t aSig = extractFloatx80Frac(a);
    int32_t aExp = extractFloatx80Exp(a);
    int aSign = extractFloatx80Sign(a);

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a))
    {
        float_raise(status, float_flag_invalid);
        return float32_default_nan;
    }

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1))
            return commonNaNToFloat32(floatx80ToCommonNaN(a, status));

        return packFloat32(aSign, 0xFF, 0);
    }
    aSig = shift64RightJamming(aSig, 33);
    if (aExp || aSig) aExp -= 0x3F81;
    return roundAndPackFloat32(aSign, aExp, (uint32_t) aSig, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the double-precision floating-point format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 floatx80_to_float64(floatx80 a, float_status_t *status)
{
    int32_t aExp;
    uint64_t aSig, zSig;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a))
    {
        float_raise(status, float_flag_invalid);
        return float64_default_nan;
    }

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    int aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) {
            return commonNaNToFloat64(floatx80ToCommonNaN(a, status));
        }
        return packFloat64(aSign, 0x7FF, 0);
    }
    zSig = shift64RightJamming(aSig, 1);
    if (aExp || aSig) aExp -= 0x3C01;
    return roundAndPackFloat64(aSign, aExp, zSig, status);
}

/*----------------------------------------------------------------------------
| Rounds the extended double-precision floating-point value `a' to an integer,
| and returns the result as an extended double-precision floating-point
| value.  The operation is performed according to the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_round_to_int(floatx80 a, float_status_t *status)
{
    int aSign;
    uint64_t lastBitMask, roundBitsMask;
    int roundingMode = get_float_rounding_mode(status);
    floatx80 z;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a))
    {
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    int32_t aExp = extractFloatx80Exp(a);
    uint64_t aSig = extractFloatx80Frac(a);
    if (0x403E <= aExp) {
        if ((aExp == 0x7FFF) && (uint64_t) (aSig<<1)) {
            return propagateFloatx80NaN(a, status);
        }
        return a;
    }
    if (aExp < 0x3FFF) {
        if (aExp == 0) {
            if ((aSig<<1) == 0) return a;
            float_raise(status, float_flag_denormal);
        }
        float_raise(status, float_flag_inexact);
        aSign = extractFloatx80Sign(a);
        switch (roundingMode) {
         case float_round_nearest_even:
            if ((aExp == 0x3FFE) && (uint64_t) (aSig<<1)) {
                set_float_rounding_up(status);
                return packFloatx80(aSign, 0x3FFF, U64(0x8000000000000000));
            }
            break;
         case float_round_down:
            if (aSign) {
                set_float_rounding_up(status);
                return packFloatx80(1, 0x3FFF, U64(0x8000000000000000));
            }
            else {
                return packFloatx80(0, 0, 0);
            }
         case float_round_up:
            if (aSign) {
                return packFloatx80(1, 0, 0);
            }
            else {
                set_float_rounding_up(status);
                return packFloatx80(0, 0x3FFF, U64(0x8000000000000000));
            }
        }
        return packFloatx80(aSign, 0, 0);
    }
    lastBitMask = 1;
    lastBitMask <<= 0x403E - aExp;
    roundBitsMask = lastBitMask - 1;
    z = a;
    if (roundingMode == float_round_nearest_even) {
        z.fraction += lastBitMask>>1;
        if ((z.fraction & roundBitsMask) == 0) z.fraction &= ~lastBitMask;
    }
    else if (roundingMode != float_round_to_zero) {
        if (extractFloatx80Sign(z) ^ (roundingMode == float_round_up))
            z.fraction += roundBitsMask;
    }
    z.fraction &= ~roundBitsMask;
    if (z.fraction == 0) {
        z.exp++;
        z.fraction = U64(0x8000000000000000);
    }
    if (z.fraction != a.fraction) {
        float_raise(status, float_flag_inexact);
        if (z.fraction > a.fraction || z.exp > a.exp)
            set_float_rounding_up(status);
    }
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of adding the absolute values of the extended double-
| precision floating-point values `a' and `b'.  If `zSign' is 1, the sum is
| negated before being returned.  `zSign' is ignored if the result is a NaN.
| The addition is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static floatx80 addFloatx80Sigs(floatx80 a, floatx80 b, int zSign, float_status_t *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a) || floatx80_is_unsupported(b))
    {
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1) || ((bExp == 0x7FFF) && (uint64_t) (bSig<<1)))
            return propagateFloatx80NaN_two_args(a, b, status);
        if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
        return a;
    }
    if (bExp == 0x7FFF) {
        if ((uint64_t) (bSig<<1)) return propagateFloatx80NaN_two_args(a, b, status);
        if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
        return packFloatx80(zSign, 0x7FFF, U64(0x8000000000000000));
    }
    if (aExp == 0) {
        if (aSig == 0) {
            if ((bExp == 0) && bSig) {
                float_raise(status, float_flag_denormal);
                normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
            }
            return roundAndPackFloatx80(get_float_rounding_precision(status),
                    zSign, bExp, bSig, 0, status);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        if (bSig == 0)
            return roundAndPackFloatx80(get_float_rounding_precision(status),
                    zSign, aExp, aSig, 0, status);

        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }
    int32_t expDiff = aExp - bExp;
    zExp = aExp;
    if (0 < expDiff) {
        shift64ExtraRightJamming(bSig, 0,  expDiff, &bSig, &zSig1);
    }
    else if (expDiff < 0) {
        shift64ExtraRightJamming(aSig, 0, -expDiff, &aSig, &zSig1);
        zExp = bExp;
    }
    else {
        zSig0 = aSig + bSig;
        zSig1 = 0;
        goto shiftRight1;
    }
    zSig0 = aSig + bSig;
    if ((int64_t) zSig0 < 0) goto roundAndPack;
 shiftRight1:
    shift64ExtraRightJamming(zSig0, zSig1, 1, &zSig0, &zSig1);
    zSig0 |= U64(0x8000000000000000);
    zExp++;
 roundAndPack:
    return
        roundAndPackFloatx80(get_float_rounding_precision(status),
            zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the absolute values of the extended
| double-precision floating-point values `a' and `b'.  If `zSign' is 1, the
| difference is negated before being returned.  `zSign' is ignored if the
| result is a NaN.  The subtraction is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static floatx80 subFloatx80Sigs(floatx80 a, floatx80 b, int zSign, float_status_t *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a) || floatx80_is_unsupported(b))
    {
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaN_two_args(a, b, status);
        if (bExp == 0x7FFF) {
            if ((uint64_t) (bSig<<1)) return propagateFloatx80NaN_two_args(a, b, status);
            float_raise(status, float_flag_invalid);
            return floatx80_default_nan;
        }
        if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
        return a;
    }
    if (bExp == 0x7FFF) {
        if ((uint64_t) (bSig<<1)) return propagateFloatx80NaN_two_args(a, b, status);
        if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
        return packFloatx80(zSign ^ 1, 0x7FFF, U64(0x8000000000000000));
    }
    if (aExp == 0) {
        if (aSig == 0) {
            if (bExp == 0) {
                if (bSig) {
                    float_raise(status, float_flag_denormal);
                    normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
                    return roundAndPackFloatx80(get_float_rounding_precision(status),
                        zSign ^ 1, bExp, bSig, 0, status);
                }
                return packFloatx80(get_float_rounding_mode(status) == float_round_down, 0, 0);
            }
            return roundAndPackFloatx80(get_float_rounding_precision(status),
                    zSign ^ 1, bExp, bSig, 0, status);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        if (bSig == 0)
            return roundAndPackFloatx80(get_float_rounding_precision(status),
                    zSign, aExp, aSig, 0, status);

        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }
    int32_t expDiff = aExp - bExp;
    if (0 < expDiff) {
        shift128RightJamming(bSig, 0, expDiff, &bSig, &zSig1);
        goto aBigger;
    }
    if (expDiff < 0) {
        shift128RightJamming(aSig, 0, -expDiff, &aSig, &zSig1);
        goto bBigger;
    }
    zSig1 = 0;
    if (bSig < aSig) goto aBigger;
    if (aSig < bSig) goto bBigger;
    return packFloatx80(get_float_rounding_mode(status) == float_round_down, 0, 0);
 bBigger:
    sub128(bSig, 0, aSig, zSig1, &zSig0, &zSig1);
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aBigger:
    sub128(aSig, 0, bSig, zSig1, &zSig0, &zSig1);
    zExp = aExp;
 normalizeRoundAndPack:
    return
        normalizeRoundAndPackFloatx80(get_float_rounding_precision(status),
            zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of adding the extended double-precision floating-point
| values `a' and `b'.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_add(floatx80 a, floatx80 b, float_status_t *status)
{
    int aSign = extractFloatx80Sign(a);
    int bSign = extractFloatx80Sign(b);

    if (aSign == bSign)
        return addFloatx80Sigs(a, b, aSign, status);
    else
        return subFloatx80Sigs(a, b, aSign, status);
}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the extended double-precision floating-
| point values `a' and `b'.  The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_sub(floatx80 a, floatx80 b, float_status_t *status)
{
    int aSign = extractFloatx80Sign(a);
    int bSign = extractFloatx80Sign(b);

    if (aSign == bSign)
        return subFloatx80Sigs(a, b, aSign, status);
    else
        return addFloatx80Sigs(a, b, aSign, status);
}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the extended double-precision floating-
| point values `a' and `b'.  The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_mul(floatx80 a, floatx80 b, float_status_t *status)
{
    int aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a) || floatx80_is_unsupported(b))
    {
 invalid:
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);
    bSign = extractFloatx80Sign(b);
    zSign = aSign ^ bSign;

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1) || ((bExp == 0x7FFF) && (uint64_t) (bSig<<1))) {
            return propagateFloatx80NaN_two_args(a, b, status);
        }
        if (bExp == 0) {
            if (bSig == 0) goto invalid;
            float_raise(status, float_flag_denormal);
        }
        return packFloatx80(zSign, 0x7FFF, U64(0x8000000000000000));
    }
    if (bExp == 0x7FFF) {
        if ((uint64_t) (bSig<<1)) return propagateFloatx80NaN_two_args(a, b, status);
        if (aExp == 0) {
            if (aSig == 0) goto invalid;
            float_raise(status, float_flag_denormal);
        }
        return packFloatx80(zSign, 0x7FFF, U64(0x8000000000000000));
    }
    if (aExp == 0) {
        if (aSig == 0) {
            if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
            return packFloatx80(zSign, 0, 0);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        if (bSig == 0) return packFloatx80(zSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }
    zExp = aExp + bExp - 0x3FFE;
    mul64To128(aSig, bSig, &zSig0, &zSig1);
    if (0 < (int64_t) zSig0) {
        shortShift128Left(zSig0, zSig1, 1, &zSig0, &zSig1);
        --zExp;
    }
    return
        roundAndPackFloatx80(get_float_rounding_precision(status),
             zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of dividing the extended double-precision floating-point
| value `a' by the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_div(floatx80 a, floatx80 b, float_status_t *status)
{
    int aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;
    uint64_t rem0, rem1, rem2, term0, term1, term2;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a) || floatx80_is_unsupported(b))
    {
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);
    bSign = extractFloatx80Sign(b);

    zSign = aSign ^ bSign;
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaN_two_args(a, b, status);
        if (bExp == 0x7FFF) {
            if ((uint64_t) (bSig<<1)) return propagateFloatx80NaN_two_args(a, b, status);
            float_raise(status, float_flag_invalid);
            return floatx80_default_nan;
        }
        if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
        return packFloatx80(zSign, 0x7FFF, U64(0x8000000000000000));
    }
    if (bExp == 0x7FFF) {
        if ((uint64_t) (bSig<<1)) return propagateFloatx80NaN_two_args(a, b, status);
        if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
        return packFloatx80(zSign, 0, 0);
    }
    if (bExp == 0) {
        if (bSig == 0) {
            if ((aExp | aSig) == 0) {
                float_raise(status, float_flag_invalid);
                return floatx80_default_nan;
            }
            float_raise(status, float_flag_divbyzero);
            return packFloatx80(zSign, 0x7FFF, U64(0x8000000000000000));
        }
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }
    if (aExp == 0) {
        if (aSig == 0) return packFloatx80(zSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    zExp = aExp - bExp + 0x3FFE;
    rem1 = 0;
    if (bSig <= aSig) {
        shift128Right(aSig, 0, 1, &aSig, &rem1);
        ++zExp;
    }
    zSig0 = estimateDiv128To64(aSig, rem1, bSig);
    mul64To128(bSig, zSig0, &term0, &term1);
    sub128(aSig, rem1, term0, term1, &rem0, &rem1);
    while ((int64_t) rem0 < 0) {
        --zSig0;
        add128(rem0, rem1, 0, bSig, &rem0, &rem1);
    }
    zSig1 = estimateDiv128To64(rem1, 0, bSig);
    if ((uint64_t) (zSig1<<1) <= 8) {
        mul64To128(bSig, zSig1, &term1, &term2);
        sub128(rem1, 0, term1, term2, &rem1, &rem2);
        while ((int64_t) rem1 < 0) {
            --zSig1;
            add128(rem1, rem2, 0, bSig, &rem1, &rem2);
        }
        zSig1 |= ((rem1 | rem2) != 0);
    }
    return
        roundAndPackFloatx80(get_float_rounding_precision(status),
            zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the square root of the extended double-precision floating-point
| value `a'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_sqrt(floatx80 a, float_status_t *status)
{
    int aSign;
    int32_t aExp, zExp;
    uint64_t aSig0, aSig1, zSig0, zSig1, doubleZSig0;
    uint64_t rem0, rem1, rem2, rem3, term0, term1, term2, term3;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a))
    {
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    aSig0 = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig0<<1)) return propagateFloatx80NaN(a, status);
        if (! aSign) return a;
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }
    if (aSign) {
        if ((aExp | aSig0) == 0) return a;
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }
    if (aExp == 0) {
        if (aSig0 == 0) return packFloatx80(0, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig0, &aExp, &aSig0);
    }
    zExp = ((aExp - 0x3FFF)>>1) + 0x3FFF;
    zSig0 = estimateSqrt32(aExp, aSig0>>32);
    shift128Right(aSig0, 0, 2 + (aExp & 1), &aSig0, &aSig1);
    zSig0 = estimateDiv128To64(aSig0, aSig1, zSig0<<32) + (zSig0<<30);
    doubleZSig0 = zSig0<<1;
    mul64To128(zSig0, zSig0, &term0, &term1);
    sub128(aSig0, aSig1, term0, term1, &rem0, &rem1);
    while ((int64_t) rem0 < 0) {
        --zSig0;
        doubleZSig0 -= 2;
        add128(rem0, rem1, zSig0>>63, doubleZSig0 | 1, &rem0, &rem1);
    }
    zSig1 = estimateDiv128To64(rem1, 0, doubleZSig0);
    if ((zSig1 & U64(0x3FFFFFFFFFFFFFFF)) <= 5) {
        if (zSig1 == 0) zSig1 = 1;
        mul64To128(doubleZSig0, zSig1, &term1, &term2);
        sub128(rem1, 0, term1, term2, &rem1, &rem2);
        mul64To128(zSig1, zSig1, &term2, &term3);
        sub192(rem1, rem2, 0, 0, term2, term3, &rem1, &rem2, &rem3);
        while ((int64_t) rem1 < 0) {
            --zSig1;
            shortShift128Left(0, zSig1, 1, &term2, &term3);
            term3 |= 1;
            term2 |= doubleZSig0;
            add192(rem1, rem2, rem3, 0, term2, term3, &rem1, &rem2, &rem3);
        }
        zSig1 |= ((rem1 | rem2 | rem3) != 0);
    }
    shortShift128Left(0, zSig1, 1, &zSig0, &zSig1);
    zSig0 |= doubleZSig0;
    return
        roundAndPackFloatx80(get_float_rounding_precision(status),
            0, zExp, zSig0, zSig1, status);
}

#endif

#ifdef FLOAT128

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the quadruple-precision floating-point format. The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 floatx80_to_float128(floatx80 a, float_status_t *status)
{
    uint64_t zSig0, zSig1;

    uint64_t aSig = extractFloatx80Frac(a);
    int32_t aExp = extractFloatx80Exp(a);
    int   aSign = extractFloatx80Sign(a);

    if ((aExp == 0x7FFF) && (uint64_t) (aSig<<1))
        return commonNaNToFloat128(floatx80ToCommonNaN(a, status));

    shift128Right(aSig<<1, 0, 16, &zSig0, &zSig1);
    return packFloat128(aSign, aExp, zSig0, zSig1);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the extended double-precision floating-point format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 float128_to_floatx80(float128 a, float_status_t *status)
{
    int32_t aExp;
    uint64_t aSig0, aSig1;

    aSig1 = extractFloat128Frac1(a);
    aSig0 = extractFloat128Frac0(a);
    aExp = extractFloat128Exp(a);
    int aSign = extractFloat128Sign(a);

    if (aExp == 0x7FFF) {
        if (aSig0 | aSig1)
            return commonNaNToFloatx80(float128ToCommonNaN(a, status));

        return packFloatx80(aSign, 0x7FFF, U64(0x8000000000000000));
    }

    if (aExp == 0) {
        if ((aSig0 | aSig1) == 0) return packFloatx80(aSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat128Subnormal(aSig0, aSig1, &aExp, &aSig0, &aSig1);
    }
    else aSig0 |= U64(0x0001000000000000);

    shortShift128Left(aSig0, aSig1, 15, &aSig0, &aSig1);
    return roundAndPackFloatx80(80, aSign, aExp, aSig0, aSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the extended double-precision floating-
| point value `a' and quadruple-precision floating point value `b'. The
| operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_mul_with_float128(floatx80 a, float128 b, float_status_t *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig0, bSig1, zSig0, zSig1, zSig2;
    int aSign, bSign, zSign;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a))
    {
 invalid:
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    bSig0 = extractFloat128Frac0(b);
    bSig1 = extractFloat128Frac1(b);
    bExp = extractFloat128Exp(b);
    bSign = extractFloat128Sign(b);

    zSign = aSign ^ bSign;

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)
             || ((bExp == 0x7FFF) && (bSig0 | bSig1)))
        {
            floatx80 r = commonNaNToFloatx80(float128ToCommonNaN(b, status));
            return propagateFloatx80NaN_two_args(a, r, status);
        }
        if (bExp == 0) {
            if ((bSig0 | bSig1) == 0) goto invalid;
            float_raise(status, float_flag_denormal);
        }
        return packFloatx80(zSign, 0x7FFF, U64(0x8000000000000000));
    }
    if (bExp == 0x7FFF) {
        if (bSig0 | bSig1) {
            floatx80 r = commonNaNToFloatx80(float128ToCommonNaN(b, status));
            return propagateFloatx80NaN_two_args(a, r, status);
        }
        if (aExp == 0) {
            if (aSig == 0) goto invalid;
            float_raise(status, float_flag_denormal);
        }
        return packFloatx80(zSign, 0x7FFF, U64(0x8000000000000000));
    }
    if (aExp == 0) {
        if (aSig == 0) {
            if ((bExp == 0) && (bSig0 | bSig1)) float_raise(status, float_flag_denormal);
            return packFloatx80(zSign, 0, 0);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        if ((bSig0 | bSig1) == 0) return packFloatx80(zSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat128Subnormal(bSig0, bSig1, &bExp, &bSig0, &bSig1);
    }
    else bSig0 |= U64(0x0001000000000000);

    zExp = aExp + bExp - 0x3FFE;
    shortShift128Left(bSig0, bSig1, 15, &bSig0, &bSig1);
    mul128By64To192(bSig0, bSig1, aSig, &zSig0, &zSig1, &zSig2);
    if (0 < (int64_t) zSig0) {
        shortShift128Left(zSig0, zSig1, 1, &zSig0, &zSig1);
        --zExp;
    }
    return
        roundAndPackFloatx80(get_float_rounding_precision(status),
             zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of adding the absolute values of the quadruple-precision
| floating-point values `a' and `b'. If `zSign' is 1, the sum is negated
| before being returned. `zSign' is ignored if the result is a NaN.
| The addition is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float128 addFloat128Sigs(float128 a, float128 b, int zSign, float_status_t *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2;
    int32_t expDiff;

    aSig1 = extractFloat128Frac1(a);
    aSig0 = extractFloat128Frac0(a);
    aExp = extractFloat128Exp(a);
    bSig1 = extractFloat128Frac1(b);
    bSig0 = extractFloat128Frac0(b);
    bExp = extractFloat128Exp(b);
    expDiff = aExp - bExp;

    if (0 < expDiff) {
        if (aExp == 0x7FFF) {
            if (aSig0 | aSig1) return propagateFloat128NaN(a, b, status);
            return a;
        }
        if (bExp == 0) --expDiff;
        else bSig0 |= U64(0x0001000000000000);
        shift128ExtraRightJamming(bSig0, bSig1, 0, expDiff, &bSig0, &bSig1, &zSig2);
        zExp = aExp;
    }
    else if (expDiff < 0) {
        if (bExp == 0x7FFF) {
            if (bSig0 | bSig1) return propagateFloat128NaN(a, b, status);
            return packFloat128(zSign, 0x7FFF, 0, 0);
        }
        if (aExp == 0) ++expDiff;
        else aSig0 |= U64(0x0001000000000000);
        shift128ExtraRightJamming(aSig0, aSig1, 0, -expDiff, &aSig0, &aSig1, &zSig2);
        zExp = bExp;
    }
    else {
        if (aExp == 0x7FFF) {
            if (aSig0 | aSig1 | bSig0 | bSig1)
                return propagateFloat128NaN(a, b, status);

            return a;
        }
        add128(aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1);
        if (aExp == 0) return packFloat128(zSign, 0, zSig0, zSig1);
        zSig2 = 0;
        zSig0 |= U64(0x0002000000000000);
        zExp = aExp;
        goto shiftRight1;
    }
    aSig0 |= U64(0x0001000000000000);
    add128(aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1);
    --zExp;
    if (zSig0 < U64(0x0002000000000000)) goto roundAndPack;
    ++zExp;
 shiftRight1:
    shift128ExtraRightJamming(zSig0, zSig1, zSig2, 1, &zSig0, &zSig1, &zSig2);
 roundAndPack:
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);
}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the absolute values of the quadruple-
| precision floating-point values `a' and `b'.  If `zSign' is 1, the
| difference is negated before being returned.  `zSign' is ignored if the
| result is a NaN.  The subtraction is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float128 subFloat128Sigs(float128 a, float128 b, int zSign, float_status_t *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1;
    int32_t expDiff;

    aSig1 = extractFloat128Frac1(a);
    aSig0 = extractFloat128Frac0(a);
    aExp = extractFloat128Exp(a);
    bSig1 = extractFloat128Frac1(b);
    bSig0 = extractFloat128Frac0(b);
    bExp = extractFloat128Exp(b);

    expDiff = aExp - bExp;
    shortShift128Left(aSig0, aSig1, 14, &aSig0, &aSig1);
    shortShift128Left(bSig0, bSig1, 14, &bSig0, &bSig1);
    if (0 < expDiff) goto aExpBigger;
    if (expDiff < 0) goto bExpBigger;
    if (aExp == 0x7FFF) {
        if (aSig0 | aSig1 | bSig0 | bSig1)
            return propagateFloat128NaN(a, b, status);

        float_raise(status, float_flag_invalid);
        return float128_default_nan;
    }
    if (aExp == 0) {
        aExp = 1;
        bExp = 1;
    }
    if (bSig0 < aSig0) goto aBigger;
    if (aSig0 < bSig0) goto bBigger;
    if (bSig1 < aSig1) goto aBigger;
    if (aSig1 < bSig1) goto bBigger;
    return packFloat128_simple(0, 0);

 bExpBigger:
    if (bExp == 0x7FFF) {
        if (bSig0 | bSig1) return propagateFloat128NaN(a, b, status);
        return packFloat128(zSign ^ 1, 0x7FFF, 0, 0);
    }
    if (aExp == 0) ++expDiff;
    else {
        aSig0 |= U64(0x4000000000000000);
    }
    shift128RightJamming(aSig0, aSig1, - expDiff, &aSig0, &aSig1);
    bSig0 |= U64(0x4000000000000000);
 bBigger:
    sub128(bSig0, bSig1, aSig0, aSig1, &zSig0, &zSig1);
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if (aExp == 0x7FFF) {
        if (aSig0 | aSig1) return propagateFloat128NaN(a, b, status);
        return a;
    }
    if (bExp == 0) --expDiff;
    else {
        bSig0 |= U64(0x4000000000000000);
    }
    shift128RightJamming(bSig0, bSig1, expDiff, &bSig0, &bSig1);
    aSig0 |= U64(0x4000000000000000);
 aBigger:
    sub128(aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1);
    zExp = aExp;
 normalizeRoundAndPack:
    --zExp;
    return normalizeRoundAndPackFloat128(zSign, zExp - 14, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of adding the quadruple-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_add(float128 a, float128 b, float_status_t *status)
{
    int aSign = extractFloat128Sign(a);
    int bSign = extractFloat128Sign(b);

    if (aSign == bSign) {
        return addFloat128Sigs(a, b, aSign, status);
    }
    else {
        return subFloat128Sigs(a, b, aSign, status);
    }
}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the quadruple-precision floating-point
| values `a' and `b'.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_sub(float128 a, float128 b, float_status_t *status)
{
    int aSign = extractFloat128Sign(a);
    int bSign = extractFloat128Sign(b);

    if (aSign == bSign) {
        return subFloat128Sigs(a, b, aSign, status);
    }
    else {
        return addFloat128Sigs(a, b, aSign, status);
    }
}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the quadruple-precision floating-point
| values `a' and `b'.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_mul(float128 a, float128 b, float_status_t *status)
{
    int aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2, zSig3;

    aSig1 = extractFloat128Frac1(a);
    aSig0 = extractFloat128Frac0(a);
    aExp = extractFloat128Exp(a);
    aSign = extractFloat128Sign(a);
    bSig1 = extractFloat128Frac1(b);
    bSig0 = extractFloat128Frac0(b);
    bExp = extractFloat128Exp(b);
    bSign = extractFloat128Sign(b);

    zSign = aSign ^ bSign;
    if (aExp == 0x7FFF) {
        if ((aSig0 | aSig1) || ((bExp == 0x7FFF) && (bSig0 | bSig1))) {
            return propagateFloat128NaN(a, b, status);
        }
        if ((bExp | bSig0 | bSig1) == 0) {
            float_raise(status, float_flag_invalid);
            return float128_default_nan;
        }
        return packFloat128(zSign, 0x7FFF, 0, 0);
    }
    if (bExp == 0x7FFF) {
        if (bSig0 | bSig1) return propagateFloat128NaN(a, b, status);
        if ((aExp | aSig0 | aSig1) == 0) {
            float_raise(status, float_flag_invalid);
            return float128_default_nan;
        }
        return packFloat128(zSign, 0x7FFF, 0, 0);
    }
    if (aExp == 0) {
        if ((aSig0 | aSig1) == 0) return packFloat128(zSign, 0, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat128Subnormal(aSig0, aSig1, &aExp, &aSig0, &aSig1);
    }
    if (bExp == 0) {
        if ((bSig0 | bSig1) == 0) return packFloat128(zSign, 0, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat128Subnormal(bSig0, bSig1, &bExp, &bSig0, &bSig1);
    }
    zExp = aExp + bExp - 0x4000;
    aSig0 |= U64(0x0001000000000000);
    shortShift128Left(bSig0, bSig1, 16, &bSig0, &bSig1);
    mul128To256(aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1, &zSig2, &zSig3);
    add128(zSig0, zSig1, aSig0, aSig1, &zSig0, &zSig1);
    zSig2 |= (zSig3 != 0);
    if (U64(0x0002000000000000) <= zSig0) {
        shift128ExtraRightJamming(zSig0, zSig1, zSig2, 1, &zSig0, &zSig1, &zSig2);
        ++zExp;
    }
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);
}

/*----------------------------------------------------------------------------
| Returns the result of dividing the quadruple-precision floating-point value
| `a' by the corresponding value `b'.  The operation is performed according to
| the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_div(float128 a, float128 b, float_status_t *status)
{
    int aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2;
    uint64_t rem0, rem1, rem2, rem3, term0, term1, term2, term3;

    aSig1 = extractFloat128Frac1(a);
    aSig0 = extractFloat128Frac0(a);
    aExp = extractFloat128Exp(a);
    aSign = extractFloat128Sign(a);
    bSig1 = extractFloat128Frac1(b);
    bSig0 = extractFloat128Frac0(b);
    bExp = extractFloat128Exp(b);
    bSign = extractFloat128Sign(b);

    zSign = aSign ^ bSign;
    if (aExp == 0x7FFF) {
        if (aSig0 | aSig1) return propagateFloat128NaN(a, b, status);
        if (bExp == 0x7FFF) {
            if (bSig0 | bSig1) return propagateFloat128NaN(a, b, status);
            float_raise(status, float_flag_invalid);
            return float128_default_nan;
        }
        return packFloat128(zSign, 0x7FFF, 0, 0);
    }
    if (bExp == 0x7FFF) {
        if (bSig0 | bSig1) return propagateFloat128NaN(a, b, status);
        return packFloat128(zSign, 0, 0, 0);
    }
    if (bExp == 0) {
        if ((bSig0 | bSig1) == 0) {
            if ((aExp | aSig0 | aSig1) == 0) {
                float_raise(status, float_flag_invalid);
                return float128_default_nan;
            }
            float_raise(status, float_flag_divbyzero);
            return packFloat128(zSign, 0x7FFF, 0, 0);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloat128Subnormal(bSig0, bSig1, &bExp, &bSig0, &bSig1);
    }
    if (aExp == 0) {
        if ((aSig0 | aSig1) == 0) return packFloat128(zSign, 0, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloat128Subnormal(aSig0, aSig1, &aExp, &aSig0, &aSig1);
    }
    zExp = aExp - bExp + 0x3FFD;
    shortShift128Left(
        aSig0 | U64(0x0001000000000000), aSig1, 15, &aSig0, &aSig1);
    shortShift128Left(
        bSig0 | U64(0x0001000000000000), bSig1, 15, &bSig0, &bSig1);
    if (le128(bSig0, bSig1, aSig0, aSig1)) {
        shift128Right(aSig0, aSig1, 1, &aSig0, &aSig1);
        ++zExp;
    }
    zSig0 = estimateDiv128To64(aSig0, aSig1, bSig0);
    mul128By64To192(bSig0, bSig1, zSig0, &term0, &term1, &term2);
    sub192(aSig0, aSig1, 0, term0, term1, term2, &rem0, &rem1, &rem2);
    while ((int64_t) rem0 < 0) {
        --zSig0;
        add192(rem0, rem1, rem2, 0, bSig0, bSig1, &rem0, &rem1, &rem2);
    }
    zSig1 = estimateDiv128To64(rem1, rem2, bSig0);
    if ((zSig1 & 0x3FFF) <= 4) {
        mul128By64To192(bSig0, bSig1, zSig1, &term1, &term2, &term3);
        sub192(rem1, rem2, 0, term1, term2, term3, &rem1, &rem2, &rem3);
        while ((int64_t) rem1 < 0) {
            --zSig1;
            add192(rem1, rem2, rem3, 0, bSig0, bSig1, &rem1, &rem2, &rem3);
        }
        zSig1 |= ((rem1 | rem2 | rem3) != 0);
    }
    shift128ExtraRightJamming(zSig0, zSig1, 0, 15, &zSig0, &zSig1, &zSig2);
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit two's complement integer `a' to
| the quadruple-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 int64_to_float128(int64_t a)
{
    uint64_t zSig0, zSig1;

    if (a == 0) return packFloat128(0, 0, 0, 0);
    int zSign = (a < 0);
    uint64_t absA = zSign ? - a : a;
    uint8_t shiftCount = countLeadingZeros64(absA) + 49;
    int32_t zExp = 0x406E - shiftCount;
    if (64 <= shiftCount) {
        zSig1 = 0;
        zSig0 = absA;
        shiftCount -= 64;
    }
    else {
        zSig1 = absA;
        zSig0 = 0;
    }
    shortShift128Left(zSig0, zSig1, shiftCount, &zSig0, &zSig1);
    return packFloat128(zSign, zExp, zSig0, zSig1);
}

#endif

/*============================================================================
This source file is an extension to the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b, written for Bochs (x86 achitecture simulator)
floating point emulation.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

/*============================================================================
 * Written for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

/*============================================================================
 * Adapted for Halfix (x86 achitecture simulator)
 * ==========================================================================*/

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 16-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic - which means in particular that the conversion
| is rounded according to the current rounding mode. If `a' is a NaN or the
| conversion overflows, the integer indefinite value is returned.
*----------------------------------------------------------------------------*/

int16_t floatx80_to_int16(floatx80 a, float_status_t *status)
{
   if (floatx80_is_unsupported(a)) {
        float_raise(status, float_flag_invalid);
        return int16_indefinite;
   }

   int32_t v32 = floatx80_to_int32(a, status);

   if ((v32 > 32767) || (v32 < -32768)) {
        status->float_exception_flags = float_flag_invalid; // throw away other flags
        return int16_indefinite;
   }

   return (int16_t) v32;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 16-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic, except that the conversion is always rounded
| toward zero.  If `a' is a NaN or the conversion overflows, the integer
| indefinite value is returned.
*----------------------------------------------------------------------------*/

int16_t floatx80_to_int16_round_to_zero(floatx80 a, float_status_t *status)
{
   if (floatx80_is_unsupported(a)) {
        float_raise(status, float_flag_invalid);
        return int16_indefinite;
   }

   int32_t v32 = floatx80_to_int32_round_to_zero(a, status);

   if ((v32 > 32767) || (v32 < -32768)) {
        status->float_exception_flags = float_flag_invalid; // throw away other flags
        return int16_indefinite;
   }

   return (int16_t) v32;
}

/*----------------------------------------------------------------------------
| Separate the source extended double-precision floating point value `a'
| into its exponent and significand, store the significant back to the
| 'a' and return the exponent. The operation performed is a superset of
| the IEC/IEEE recommended logb(x) function.
*----------------------------------------------------------------------------*/

floatx80 floatx80_extract(floatx80 *input, float_status_t *status)
{
    floatx80 a = *input;
    uint64_t aSig = extractFloatx80Frac(a);
    int32_t aExp = extractFloatx80Exp(a);
    int   aSign = extractFloatx80Sign(a);

    if (floatx80_is_unsupported(a))
    {
        float_raise(status, float_flag_invalid);
        a = floatx80_default_nan;
        *input = a;
        return a;
    }

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1))
        {
            a = propagateFloatx80NaN(a, status);
            *input = a;
            return a;
        }
        *input = a;
        return packFloatx80(0, 0x7FFF, U64(0x8000000000000000));
    }
    if (aExp == 0)
    {
        if (aSig == 0) {
            float_raise(status, float_flag_divbyzero);
            a = packFloatx80(aSign, 0, 0);
            *input = a;
            return packFloatx80(1, 0x7FFF, U64(0x8000000000000000));
        }
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    a.exp = (aSign << 15) + 0x3FFF;
    a.fraction = aSig;
    *input = a;
    return int32_to_floatx80(aExp - 0x3FFF);
}

/*----------------------------------------------------------------------------
| Scales extended double-precision floating-point value in operand `a' by
| value `b'. The function truncates the value in the second operand 'b' to
| an integral value and adds that value to the exponent of the operand 'a'.
| The operation performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_scale(floatx80 a, floatx80 b, float_status_t *status)
{
    int32_t aExp, bExp;
    uint64_t aSig, bSig;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a) || floatx80_is_unsupported(b))
    {
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    int aSign = extractFloatx80Sign(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);
    int bSign = extractFloatx80Sign(b);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1) || ((bExp == 0x7FFF) && (uint64_t) (bSig<<1)))
        {
            return propagateFloatx80NaN_two_args(a, b, status);
        }
        if ((bExp == 0x7FFF) && bSign) {
            float_raise(status, float_flag_invalid);
            return floatx80_default_nan;
        }
        if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
        return a;
    }
    if (bExp == 0x7FFF) {
        if ((uint64_t) (bSig<<1)) return propagateFloatx80NaN_two_args(a, b, status);
        if ((aExp | aSig) == 0) {
            if (! bSign) {
                float_raise(status, float_flag_invalid);
                return floatx80_default_nan;
            }
            return a;
        }
        if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
        if (bSign) return packFloatx80(aSign, 0, 0);
        return packFloatx80(aSign, 0x7FFF, U64(0x8000000000000000));
    }
    if (aExp == 0) {
        if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
        if (aSig == 0) return a;
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
        if (bExp < 0x3FFF)
            return normalizeRoundAndPackFloatx80(80, aSign, aExp, aSig, 0, status);
    }
    if (bExp == 0) {
        if (bSig == 0) return a;
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }

    if (bExp > 0x400E) {
        /* generate appropriate overflow/underflow */
        return roundAndPackFloatx80(80, aSign,
                          bSign ? -0x3FFF : 0x7FFF, aSig, 0, status);
    }

    if (bExp < 0x3FFF) return a;

    int shiftCount = 0x403E - bExp;
    bSig >>= shiftCount;
    int32_t scale = (int32_t) bSig;
    if (bSign) scale = -scale; /* -32768..32767 */
    return
        roundAndPackFloatx80(80, aSign, aExp+scale, aSig, 0, status);
}

/*----------------------------------------------------------------------------
| Determine extended-precision floating-point number class.
*----------------------------------------------------------------------------*/

float_class_t floatx80_class(floatx80 a)
{
   int32_t aExp = extractFloatx80Exp(a);
   uint64_t aSig = extractFloatx80Frac(a);

   if(aExp == 0) {
       if (aSig == 0)
           return float_zero;

       /* denormal or pseudo-denormal */
       return float_denormal;
   }

   /* valid numbers have the MS bit set */
   if (!(aSig & U64(0x8000000000000000)))
       return float_SNaN; /* report unsupported as SNaNs */

   if(aExp == 0x7fff) {
       int aSign = extractFloatx80Sign(a);

       if (((uint64_t) (aSig<< 1)) == 0)
           return (aSign) ? float_negative_inf : float_positive_inf;

       return (aSig & U64(0x4000000000000000)) ? float_QNaN : float_SNaN;
   }

   return float_normalized;
}

/*----------------------------------------------------------------------------
| Compare  between  two extended precision  floating  point  numbers. Returns
| 'float_relation_equal'  if the operands are equal, 'float_relation_less' if
| the    value    'a'   is   less   than   the   corresponding   value   `b',
| 'float_relation_greater' if the value 'a' is greater than the corresponding
| value `b', or 'float_relation_unordered' otherwise.
*----------------------------------------------------------------------------*/
 
int floatx80_compare_internal(floatx80 a, floatx80 b, int quiet, float_status_t *status)
{
    float_class_t aClass = floatx80_class(a);
    float_class_t bClass = floatx80_class(b);

    if (aClass == float_SNaN || bClass == float_SNaN)
    {
        /* unsupported reported as SNaN */
        float_raise(status, float_flag_invalid);
        return float_relation_unordered;
    }

    if (aClass == float_QNaN || bClass == float_QNaN) {
        if (! quiet) float_raise(status, float_flag_invalid);
        return float_relation_unordered;
    }

    if (aClass == float_denormal || bClass == float_denormal) {
        float_raise(status, float_flag_denormal);
    }

    int aSign = extractFloatx80Sign(a);
    int bSign = extractFloatx80Sign(b);

    if (aClass == float_zero) {
        if (bClass == float_zero) return float_relation_equal;
        return bSign ? float_relation_greater : float_relation_less;
    }

    if (bClass == float_zero || aSign != bSign) {
        return aSign ? float_relation_less : float_relation_greater;
    }

    uint64_t aSig = extractFloatx80Frac(a);
    int32_t aExp = extractFloatx80Exp(a);
    uint64_t bSig = extractFloatx80Frac(b);
    int32_t bExp = extractFloatx80Exp(b);

    if (aClass == float_denormal)
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);

    if (bClass == float_denormal)
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);

    if (aExp == bExp && aSig == bSig)
        return float_relation_equal;

    int less_than =
        aSign ? ((bExp < aExp) || ((bExp == aExp) && (bSig < aSig)))
              : ((aExp < bExp) || ((aExp == bExp) && (aSig < bSig)));

    if (less_than) return float_relation_less;
    return float_relation_greater;
}

/*============================================================================
This C source fragment is part of the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://www.cs.berkeley.edu/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

#define FLOAT128

/*============================================================================
 * Adapted for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#include "softfloat/softfloat.h"
#include "softfloat/softfloat-specialize.h"

/*----------------------------------------------------------------------------
| Takes two single-precision floating-point values `a' and `b', one of which
| is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
| signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

float32 propagateFloat32NaN_two_args(float32 a, float32 b, float_status_t *status)
{
    int aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;

    aIsNaN = float32_is_nan(a);
    aIsSignalingNaN = float32_is_signaling_nan(a);
    bIsNaN = float32_is_nan(b);
    bIsSignalingNaN = float32_is_signaling_nan(b);
    a |= 0x00400000;
    b |= 0x00400000;
    if (aIsSignalingNaN | bIsSignalingNaN) float_raise(status, float_flag_invalid);
    if (get_float_nan_handling_mode(status) == float_larger_significand_nan) {
        if (aIsSignalingNaN) {
            if (bIsSignalingNaN) goto returnLargerSignificand;
            return bIsNaN ? b : a;
        }
        else if (aIsNaN) {
            if (bIsSignalingNaN | ! bIsNaN) return a;
      returnLargerSignificand:
            if ((uint32_t) (a<<1) < (uint32_t) (b<<1)) return b;
            if ((uint32_t) (b<<1) < (uint32_t) (a<<1)) return a;
            return (a < b) ? a : b;
        }
        else {
            return b;
        }
    } else {
        return (aIsSignalingNaN | aIsNaN) ? a : b;
    }
}

/*----------------------------------------------------------------------------
| Takes two double-precision floating-point values `a' and `b', one of which
| is a NaN, and returns the appropriate NaN result.  If either `a' or `b' is a
| signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

float64 propagateFloat64NaN_two_args(float64 a, float64 b, float_status_t *status)
{
    int aIsNaN = float64_is_nan(a);
    int aIsSignalingNaN = float64_is_signaling_nan(a);
    int bIsNaN = float64_is_nan(b);
    int bIsSignalingNaN = float64_is_signaling_nan(b);
    a |= U64(0x0008000000000000);
    b |= U64(0x0008000000000000);
    if (aIsSignalingNaN | bIsSignalingNaN) float_raise(status, float_flag_invalid);
    if (get_float_nan_handling_mode(status) == float_larger_significand_nan) {
        if (aIsSignalingNaN) {
            if (bIsSignalingNaN) goto returnLargerSignificand;
            return bIsNaN ? b : a;
        }
        else if (aIsNaN) {
            if (bIsSignalingNaN | ! bIsNaN) return a;
      returnLargerSignificand:
            if ((uint64_t) (a<<1) < (uint64_t) (b<<1)) return b;
            if ((uint64_t) (b<<1) < (uint64_t) (a<<1)) return a;
            return (a < b) ? a : b;
        }
        else {
            return b;
        }
    } else {
        return (aIsSignalingNaN | aIsNaN) ? a : b;
    }
}

#ifdef FLOATX80

/*----------------------------------------------------------------------------
| Takes two extended double-precision floating-point values `a' and `b', one
| of which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

floatx80 propagateFloatx80NaN_two_args(floatx80 a, floatx80 b, float_status_t *status)
{
    int aIsNaN = floatx80_is_nan(a);
    int aIsSignalingNaN = floatx80_is_signaling_nan(a);
    int bIsNaN = floatx80_is_nan(b);
    int bIsSignalingNaN = floatx80_is_signaling_nan(b);
    a.fraction |= U64(0xC000000000000000);
    b.fraction |= U64(0xC000000000000000);
    if (aIsSignalingNaN | bIsSignalingNaN) float_raise(status, float_flag_invalid);
    if (aIsSignalingNaN) {
        if (bIsSignalingNaN) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if (aIsNaN) {
        if (bIsSignalingNaN | ! bIsNaN) return a;
 returnLargerSignificand:
        if (a.fraction < b.fraction) return b;
        if (b.fraction < a.fraction) return a;
        return (a.exp < b.exp) ? a : b;
    }
    else {
        return b;
    }
}

/*----------------------------------------------------------------------------
| The pattern for a default generated extended double-precision NaN.
*----------------------------------------------------------------------------*/
const floatx80 floatx80_default_nan =
    packFloatx80Constant(0, floatx80_default_nan_exp, floatx80_default_nan_fraction);

#endif /* FLOATX80 */

#ifdef FLOAT128

/*----------------------------------------------------------------------------
| Takes two quadruple-precision floating-point values `a' and `b', one of
| which is a NaN, and returns the appropriate NaN result.  If either `a' or
| `b' is a signaling NaN, the invalid exception is raised.
*----------------------------------------------------------------------------*/

float128 propagateFloat128NaN(float128 a, float128 b, float_status_t *status)
{
    int aIsNaN, aIsSignalingNaN, bIsNaN, bIsSignalingNaN;
    aIsNaN = float128_is_nan(a);
    aIsSignalingNaN = float128_is_signaling_nan(a);
    bIsNaN = float128_is_nan(b);
    bIsSignalingNaN = float128_is_signaling_nan(b);
    a.hi |= U64(0x0000800000000000);
    b.hi |= U64(0x0000800000000000);
    if (aIsSignalingNaN | bIsSignalingNaN) float_raise(status, float_flag_invalid);
    if (aIsSignalingNaN) {
        if (bIsSignalingNaN) goto returnLargerSignificand;
        return bIsNaN ? b : a;
    }
    else if (aIsNaN) {
        if (bIsSignalingNaN | !bIsNaN) return a;
 returnLargerSignificand:
        if (lt128(a.hi<<1, a.lo, b.hi<<1, b.lo)) return b;
        if (lt128(b.hi<<1, b.lo, a.hi<<1, a.lo)) return a;
        return (a.hi < b.hi) ? a : b;
    }
    else {
        return b;
    }
}

/*----------------------------------------------------------------------------
| The pattern for a default generated quadruple-precision NaN.
*----------------------------------------------------------------------------*/
const float128 float128_default_nan =
    packFloat128Constant(float128_default_nan_hi, float128_default_nan_lo);

#endif /* FLOAT128 */

/*============================================================================
This C source file is part of the SoftFloat IEC/IEEE Floating-point Arithmetic
Package, Release 2b.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://www.cs.berkeley.edu/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

#define FLOAT128

/*============================================================================
 * Adapted for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#include "softfloat/softfloat.h"
#include "softfloat/softfloat-round-pack.h"

/*----------------------------------------------------------------------------
| Primitive arithmetic functions, including multi-word arithmetic, and
| division and square root approximations. (Can be specialized to target
| if desired).
*----------------------------------------------------------------------------*/
#include "softfloat/softfloat-macros.h"

/*----------------------------------------------------------------------------
| Functions and definitions to determine:  (1) whether tininess for underflow
| is detected before or after rounding by default, (2) what (if anything)
| happens when exceptions are raised, (3) how signaling NaNs are distinguished
| from quiet NaNs, (4) the default generated quiet NaNs, and (5) how NaNs
| are propagated from function inputs to output.  These details are target-
| specific.
*----------------------------------------------------------------------------*/
#include "softfloat/softfloat-specialize.h"

/*----------------------------------------------------------------------------
| Takes a 64-bit fixed-point value `absZ' with binary point between bits 6
| and 7, and returns the properly rounded 32-bit integer corresponding to the
| input.  If `zSign' is 1, the input is negated before being converted to an
| integer.  Bit 63 of `absZ' must be zero.  Ordinarily, the fixed-point input
| is simply rounded to an integer, with the inexact exception raised if the
| input cannot be represented exactly as an integer.  However, if the fixed-
| point input is too large, the invalid exception is raised and the integer
| indefinite value is returned.
*----------------------------------------------------------------------------*/

int32_t roundAndPackInt32(int zSign, uint64_t exactAbsZ, float_status_t *status)
{
    int roundingMode = get_float_rounding_mode(status);
    int roundNearestEven = (roundingMode == float_round_nearest_even);
    int roundIncrement = 0x40;
    if (! roundNearestEven) {
        if (roundingMode == float_round_to_zero) roundIncrement = 0;
        else {
            roundIncrement = 0x7F;
            if (zSign) {
                if (roundingMode == float_round_up) roundIncrement = 0;
            }
            else {
                if (roundingMode == float_round_down) roundIncrement = 0;
            }
        }
    }
    int roundBits = (int)(exactAbsZ & 0x7F);
    uint64_t absZ = (exactAbsZ + roundIncrement)>>7;
    absZ &= ~(((roundBits ^ 0x40) == 0) & roundNearestEven);
    int32_t z = (int32_t) absZ;
    if (zSign) z = -z;
    if ((absZ>>32) || (z && ((z < 0) ^ zSign))) {
        float_raise(status, float_flag_invalid);
        return (int32_t)(int32_indefinite);
    }
    if (roundBits) {
        float_raise(status, float_flag_inexact);
        if ((absZ << 7) > exactAbsZ)
            set_float_rounding_up(status);
    }
    return z;
}

/*----------------------------------------------------------------------------
| Takes the 128-bit fixed-point value formed by concatenating `absZ0' and
| `absZ1', with binary point between bits 63 and 64 (between the input words),
| and returns the properly rounded 64-bit integer corresponding to the input.
| If `zSign' is 1, the input is negated before being converted to an integer.
| Ordinarily, the fixed-point input is simply rounded to an integer, with
| the inexact exception raised if the input cannot be represented exactly as
| an integer.  However, if the fixed-point input is too large, the invalid
| exception is raised and the integer indefinite value is returned.
*----------------------------------------------------------------------------*/

int64_t roundAndPackInt64(int zSign, uint64_t absZ0, uint64_t absZ1, float_status_t *status)
{
    int64_t z;
    int roundingMode = get_float_rounding_mode(status);
    int roundNearestEven = (roundingMode == float_round_nearest_even);
    int increment = ((int64_t) absZ1 < 0);
    if (! roundNearestEven) {
        if (roundingMode == float_round_to_zero) increment = 0;
        else {
            if (zSign) {
                increment = (roundingMode == float_round_down) && absZ1;
            }
            else {
                increment = (roundingMode == float_round_up) && absZ1;
            }
        }
    }
    uint64_t exactAbsZ0 = absZ0;
    if (increment) {
        ++absZ0;
        if (absZ0 == 0) goto overflow;
        absZ0 &= ~(((uint64_t) (absZ1<<1) == 0) & roundNearestEven);
    }
    z = absZ0;
    if (zSign) z = -z;
    if (z && ((z < 0) ^ zSign)) {
 overflow:
        float_raise(status, float_flag_invalid);
        return (int64_t)(int64_indefinite);
    }
    if (absZ1) {
        float_raise(status, float_flag_inexact);
        if (absZ0 > exactAbsZ0)
            set_float_rounding_up(status);
    }
    return z;
}

/*----------------------------------------------------------------------------
| Takes the 128-bit fixed-point value formed by concatenating `absZ0' and
| `absZ1', with binary point between bits 63 and 64 (between the input words),
| and returns the properly rounded 64-bit unsigned integer corresponding to the
| input.  Ordinarily, the fixed-point input is simply rounded to an integer,
| with the inexact exception raised if the input cannot be represented exactly
| as an integer. However, if the fixed-point input is too large, the invalid
| exception is raised and the largest unsigned integer is returned.
*----------------------------------------------------------------------------*/

uint64_t roundAndPackUint64(int zSign, uint64_t absZ0, uint64_t absZ1, float_status_t *status)
{
    int roundingMode = get_float_rounding_mode(status);
    int roundNearestEven = (roundingMode == float_round_nearest_even);
    int increment = ((int64_t) absZ1 < 0);
    if (!roundNearestEven) {
        if (roundingMode == float_round_to_zero) {
            increment = 0;
        } else if (absZ1) {
            if (zSign) {
                increment = (roundingMode == float_round_down) && absZ1;
            } else {
                increment = (roundingMode == float_round_up) && absZ1;
            }
        }
    }
    if (increment) {
        ++absZ0;
        if (absZ0 == 0) {
            float_raise(status, float_flag_invalid);
            return uint64_indefinite;
        }
        absZ0 &= ~(((uint64_t) (absZ1<<1) == 0) & roundNearestEven);
    }

    if (zSign && absZ0) {
        float_raise(status, float_flag_invalid);
        return uint64_indefinite;
    }

    if (absZ1) {
        float_raise(status, float_flag_inexact);
    }
    return absZ0;
}

#ifdef FLOAT16

/*----------------------------------------------------------------------------
| Normalizes the subnormal half-precision floating-point value represented
| by the denormalized significand `aSig'.  The normalized exponent and
| significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

void normalizeFloat16Subnormal(uint16_t aSig, int16_t *zExpPtr, uint16_t *zSigPtr)
{
    int shiftCount = countLeadingZeros16(aSig) - 5;
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper half-precision floating-
| point value corresponding to the abstract input.  Ordinarily, the abstract
| value is simply rounded and packed into the half-precision format, with
| the inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal single-
| precision floating-point number.
|     The input significand `zSig' has its binary point between bits 14
| and 13, which is 4 bits to the left of the usual location.  This shifted
| significand must be normalized or smaller.  If `zSig' is not normalized,
| `zExp' must be 0; in that case, the result returned is a subnormal number,
| and it must not require rounding.  In the usual case that `zSig' is
| normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
| The handling of underflow and overflow follows the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float16 roundAndPackFloat16(int zSign, int16_t zExp, uint16_t zSig, float_status_t *status)
{
    int16_t roundIncrement, roundBits, roundMask;

    int roundingMode = get_float_rounding_mode(status);
    int roundNearestEven = (roundingMode == float_round_nearest_even);
    roundIncrement = 8;
    roundMask = 0xF;

    if (! roundNearestEven) {
        if (roundingMode == float_round_to_zero) roundIncrement = 0;
        else {
            roundIncrement = roundMask;
            if (zSign) {
                if (roundingMode == float_round_up) roundIncrement = 0;
            }
            else {
                if (roundingMode == float_round_down) roundIncrement = 0;
            }
        }
    }
    roundBits = zSig & roundMask;
    if (0x1D <= (uint16_t) zExp) {
        if ((0x1D < zExp)
             || ((zExp == 0x1D) && ((int16_t) (zSig + roundIncrement) < 0)))
        {
            float_raise(status, float_flag_overflow);
            if (roundBits || float_exception_masked(status, float_flag_overflow)) {
                float_raise(status, float_flag_inexact);
            }
            return packFloat16(zSign, 0x1F, 0) - (roundIncrement == 0);
        }
        if (zExp < 0) {
            int isTiny = (zExp < -1) || (zSig + roundIncrement < 0x8000);
            zSig = shift16RightJamming(zSig, -zExp);
            zExp = 0;
            roundBits = zSig & roundMask;
            if (isTiny) {
                if(get_flush_underflow_to_zero(status)) {
                    float_raise(status, float_flag_underflow | float_flag_inexact);
                    return packFloat16(zSign, 0, 0);
                }
                // signal the #P according to roundBits calculated AFTER denormalization
                if (roundBits || !float_exception_masked(status, float_flag_underflow)) {
                    float_raise(status, float_flag_underflow);
                }
            }
        }
    }
    if (roundBits) float_raise(status, float_flag_inexact);
    uint16_t zSigRound = ((zSig + roundIncrement) & ~roundMask) >> 4;
    zSigRound &= ~(((roundBits ^ 0x10) == 0) & roundNearestEven);
    if (zSigRound == 0) zExp = 0;
    return packFloat16(zSign, zExp, zSigRound);
}

#endif

/*----------------------------------------------------------------------------
| Normalizes the subnormal single-precision floating-point value represented
| by the denormalized significand `aSig'.  The normalized exponent and
| significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

void normalizeFloat32Subnormal(uint32_t aSig, int16_t *zExpPtr, uint32_t *zSigPtr)
{
    int shiftCount = countLeadingZeros32(aSig) - 8;
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper single-precision floating-
| point value corresponding to the abstract input.  Ordinarily, the abstract
| value is simply rounded and packed into the single-precision format, with
| the inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal single-
| precision floating-point number.
|     The input significand `zSig' has its binary point between bits 30
| and 29, which is 7 bits to the left of the usual location.  This shifted
| significand must be normalized or smaller.  If `zSig' is not normalized,
| `zExp' must be 0; in that case, the result returned is a subnormal number,
| and it must not require rounding.  In the usual case that `zSig' is
| normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
| The handling of underflow and overflow follows the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 roundAndPackFloat32(int zSign, int16_t zExp, uint32_t zSig, float_status_t *status)
{
    int32_t roundIncrement, roundBits;
    const int32_t roundMask = 0x7F;

    int roundingMode = get_float_rounding_mode(status);
    int roundNearestEven = (roundingMode == float_round_nearest_even);
    roundIncrement = 0x40;

    if (! roundNearestEven) {
        if (roundingMode == float_round_to_zero) roundIncrement = 0;
        else {
            roundIncrement = roundMask;
            if (zSign) {
                if (roundingMode == float_round_up) roundIncrement = 0;
            }
            else {
                if (roundingMode == float_round_down) roundIncrement = 0;
            }
        }
    }
    roundBits = zSig & roundMask;
    if (0xFD <= (uint16_t) zExp) {
        if ((0xFD < zExp)
             || ((zExp == 0xFD) && ((int32_t) (zSig + roundIncrement) < 0)))
        {
            float_raise(status, float_flag_overflow);
            if (roundBits || float_exception_masked(status, float_flag_overflow)) {
                float_raise(status, float_flag_inexact);
                if (roundIncrement != 0) set_float_rounding_up(status);
            }
            return packFloat32(zSign, 0xFF, 0) - (roundIncrement == 0);
        }
        if (zExp < 0) {
            int isTiny = (zExp < -1) || (zSig + roundIncrement < 0x80000000);
            if (isTiny) {
                if (!float_exception_masked(status, float_flag_underflow)) {
                    float_raise(status, float_flag_underflow);
                    zExp += 192; // bias unmasked underflow
                }
            }
            if (zExp < 0) {
                zSig = shift32RightJamming(zSig, -zExp);
                zExp = 0;
                roundBits = zSig & roundMask;
                if (isTiny) {
                    // masked underflow
                    if(get_flush_underflow_to_zero(status)) {
                        float_raise(status, float_flag_underflow | float_flag_inexact);
                        return packFloat32(zSign, 0, 0);
                    }
                    if (roundBits) float_raise(status, float_flag_underflow);
                }
            }
        }
    }
    uint32_t zSigRound = ((zSig + roundIncrement) & ~roundMask) >> 7;
    zSigRound &= ~(((roundBits ^ 0x40) == 0) & roundNearestEven);
    if (zSigRound == 0) zExp = 0;
    if (roundBits) {
        float_raise(status, float_flag_inexact);
        if ((zSigRound << 7) > zSig) set_float_rounding_up(status);
    }
    return packFloat32(zSign, zExp, zSigRound);
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper single-precision floating-
| point value corresponding to the abstract input.  This routine is just like
| `roundAndPackFloat32' except that `zSig' does not have to be normalized.
| Bit 31 of `zSig' must be zero, and `zExp' must be 1 less than the ``true''
| floating-point exponent.
*----------------------------------------------------------------------------*/

float32 normalizeRoundAndPackFloat32(int zSign, int16_t zExp, uint32_t zSig, float_status_t *status)
{
    int shiftCount = countLeadingZeros32(zSig) - 1;
    return roundAndPackFloat32(zSign, zExp - shiftCount, zSig<<shiftCount, status);
}

/*----------------------------------------------------------------------------
| Normalizes the subnormal double-precision floating-point value represented
| by the denormalized significand `aSig'.  The normalized exponent and
| significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

void normalizeFloat64Subnormal(uint64_t aSig, int16_t *zExpPtr, uint64_t *zSigPtr)
{
    int shiftCount = countLeadingZeros64(aSig) - 11;
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper double-precision floating-
| point value corresponding to the abstract input.  Ordinarily, the abstract
| value is simply rounded and packed into the double-precision format, with
| the inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded
| to a subnormal number, and the underflow and inexact exceptions are raised
| if the abstract input cannot be represented exactly as a subnormal double-
| precision floating-point number.
|     The input significand `zSig' has its binary point between bits 62
| and 61, which is 10 bits to the left of the usual location.  This shifted
| significand must be normalized or smaller.  If `zSig' is not normalized,
| `zExp' must be 0; in that case, the result returned is a subnormal number,
| and it must not require rounding.  In the usual case that `zSig' is
| normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
| The handling of underflow and overflow follows the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 roundAndPackFloat64(int zSign, int16_t zExp, uint64_t zSig, float_status_t *status)
{
    int16_t roundIncrement, roundBits;
    const int16_t roundMask = 0x3FF;
    int roundingMode = get_float_rounding_mode(status);
    int roundNearestEven = (roundingMode == float_round_nearest_even);
    roundIncrement = 0x200;
    if (! roundNearestEven) {
        if (roundingMode == float_round_to_zero) roundIncrement = 0;
        else {
            roundIncrement = roundMask;
            if (zSign) {
                if (roundingMode == float_round_up) roundIncrement = 0;
            }
            else {
                if (roundingMode == float_round_down) roundIncrement = 0;
            }
        }
    }
    roundBits = (int16_t)(zSig & roundMask);
    if (0x7FD <= (uint16_t) zExp) {
        if ((0x7FD < zExp)
             || ((zExp == 0x7FD)
                  && ((int64_t) (zSig + roundIncrement) < 0)))
        {
            float_raise(status, float_flag_overflow);
            if (roundBits || float_exception_masked(status, float_flag_overflow)) {
                float_raise(status, float_flag_inexact);
                if (roundIncrement != 0) set_float_rounding_up(status);
            }
            return packFloat64(zSign, 0x7FF, 0) - (roundIncrement == 0);
        }
        if (zExp < 0) {
            int isTiny = (zExp < -1) || (zSig + roundIncrement < U64(0x8000000000000000));
            if (isTiny) {
                if (!float_exception_masked(status, float_flag_underflow)) {
                    float_raise(status, float_flag_underflow);
                    zExp += 1536; // bias unmasked underflow
                }
            }
            if (zExp < 0) {
                zSig = shift64RightJamming(zSig, -zExp);
                zExp = 0;
                roundBits = (int16_t)(zSig & roundMask);
                if (isTiny) {
                    // masked underflow
                    if(get_flush_underflow_to_zero(status)) {
                        float_raise(status, float_flag_underflow | float_flag_inexact);
                        return packFloat64(zSign, 0, 0);
                    }
                    if (roundBits) float_raise(status, float_flag_underflow);
                }
            }
        }
    }
    uint64_t zSigRound = (zSig + roundIncrement)>>10;
    zSigRound &= ~(((roundBits ^ 0x200) == 0) & roundNearestEven);
    if (zSigRound == 0) zExp = 0;
    if (roundBits) {
        float_raise(status, float_flag_inexact);
        if ((zSigRound << 10) > zSig) set_float_rounding_up(status);
    }
    return packFloat64(zSign, zExp, zSigRound);
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper double-precision floating-
| point value corresponding to the abstract input.  This routine is just like
| `roundAndPackFloat64' except that `zSig' does not have to be normalized.
| Bit 63 of `zSig' must be zero, and `zExp' must be 1 less than the ``true''
| floating-point exponent.
*----------------------------------------------------------------------------*/

float64 normalizeRoundAndPackFloat64(int zSign, int16_t zExp, uint64_t zSig, float_status_t *status)
{
    int shiftCount = countLeadingZeros64(zSig) - 1;
    return roundAndPackFloat64(zSign, zExp - shiftCount, zSig<<shiftCount, status);
}

#ifdef FLOATX80

/*----------------------------------------------------------------------------
| Normalizes the subnormal extended double-precision floating-point value
| represented by the denormalized significand `aSig'.  The normalized exponent
| and significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

void normalizeFloatx80Subnormal(uint64_t aSig, int32_t *zExpPtr, uint64_t *zSigPtr)
{
    int shiftCount = countLeadingZeros64(aSig);
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and extended significand formed by the concatenation of `zSig0' and `zSig1',
| and returns the proper extended double-precision floating-point value
| corresponding to the abstract input.  Ordinarily, the abstract value is
| rounded and packed into the extended double-precision format, with the
| inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal extended
| double-precision floating-point number.
|     If `roundingPrecision' is 32 or 64, the result is rounded to the same
| number of bits as single or double precision, respectively.  Otherwise, the
| result is rounded to the full precision of the extended double-precision
| format.
|     The input significand must be normalized or smaller.  If the input
| significand is not normalized, `zExp' must be 0; in that case, the result
| returned is a subnormal number, and it must not require rounding.  The
| handling of underflow and overflow follows the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 SoftFloatRoundAndPackFloatx80(int roundingPrecision,
        int zSign, int32_t zExp, uint64_t zSig0, uint64_t zSig1, float_status_t *status)
{
    uint64_t roundIncrement, roundMask, roundBits;
    int increment;
    uint64_t zSigExact; /* support rounding-up response */

    uint8_t roundingMode = get_float_rounding_mode(status);
    int roundNearestEven = (roundingMode == float_round_nearest_even);
    if (roundingPrecision == 64) {
        roundIncrement = U64(0x0000000000000400);
        roundMask = U64(0x00000000000007FF);
    }
    else if (roundingPrecision == 32) {
        roundIncrement = U64(0x0000008000000000);
        roundMask = U64(0x000000FFFFFFFFFF);
    }
    else goto precision80;

    zSig0 |= (zSig1 != 0);
    if (! roundNearestEven) {
        if (roundingMode == float_round_to_zero) roundIncrement = 0;
        else {
            roundIncrement = roundMask;
            if (zSign) {
                if (roundingMode == float_round_up) roundIncrement = 0;
            }
            else {
                if (roundingMode == float_round_down) roundIncrement = 0;
            }
        }
    }
    roundBits = zSig0 & roundMask;
    if (0x7FFD <= (uint32_t) (zExp - 1)) {
        if ((0x7FFE < zExp)
             || ((zExp == 0x7FFE) && (zSig0 + roundIncrement < zSig0)))
        {
            goto overflow;
        }
        if (zExp <= 0) {
            int isTiny = (zExp < 0) || (zSig0 <= zSig0 + roundIncrement);
            zSig0 = shift64RightJamming(zSig0, 1 - zExp);
            zSigExact = zSig0;
            zExp = 0;
            roundBits = zSig0 & roundMask;
            if (isTiny) {
                if (roundBits || (zSig0 && !float_exception_masked(status, float_flag_underflow)))
                    float_raise(status, float_flag_underflow);
            }
            zSig0 += roundIncrement;
            if ((int64_t) zSig0 < 0) zExp = 1;
            roundIncrement = roundMask + 1;
            if (roundNearestEven && (roundBits<<1 == roundIncrement))
                roundMask |= roundIncrement;
            zSig0 &= ~roundMask;
            if (roundBits) {
                float_raise(status, float_flag_inexact);
                if (zSig0 > zSigExact) set_float_rounding_up(status);
            }
            return packFloatx80(zSign, zExp, zSig0);
        }
    }
    if (roundBits) float_raise(status, float_flag_inexact);
    zSigExact = zSig0;
    zSig0 += roundIncrement;
    if (zSig0 < roundIncrement) {
        // Basically scale by shifting right and keep overflow
        ++zExp;
        zSig0 = U64(0x8000000000000000);
        zSigExact >>= 1; // must scale also, or else later tests will fail
    }
    roundIncrement = roundMask + 1;
    if (roundNearestEven && (roundBits<<1 == roundIncrement))
        roundMask |= roundIncrement;
    zSig0 &= ~roundMask;
    if (zSig0 > zSigExact) set_float_rounding_up(status);
    if (zSig0 == 0) zExp = 0;
    return packFloatx80(zSign, zExp, zSig0);
 precision80:
    increment = ((int64_t) zSig1 < 0);
    if (! roundNearestEven) {
        if (roundingMode == float_round_to_zero) increment = 0;
        else {
            if (zSign) {
                increment = (roundingMode == float_round_down) && zSig1;
            }
            else {
                increment = (roundingMode == float_round_up) && zSig1;
            }
        }
    }
    if (0x7FFD <= (uint32_t) (zExp - 1)) {
        if ((0x7FFE < zExp)
             || ((zExp == 0x7FFE)
                  && (zSig0 == U64(0xFFFFFFFFFFFFFFFF))
                  && increment))
        {
            roundMask = 0;
 overflow:
            float_raise(status, float_flag_overflow | float_flag_inexact);
            if ((roundingMode == float_round_to_zero)
                 || (zSign && (roundingMode == float_round_up))
                 || (! zSign && (roundingMode == float_round_down)))
            {
                return packFloatx80(zSign, 0x7FFE, ~roundMask);
            }
            set_float_rounding_up(status);
            return packFloatx80(zSign, 0x7FFF, U64(0x8000000000000000));
        }
        if (zExp <= 0) {
            int isTiny = (zExp < 0) || (! increment)
                || (zSig0 < U64(0xFFFFFFFFFFFFFFFF));
            shift64ExtraRightJamming(zSig0, zSig1, 1 - zExp, &zSig0, &zSig1);
            zExp = 0;
            if (isTiny) {
                if (zSig1 || (zSig0 && !float_exception_masked(status, float_flag_underflow)))
                    float_raise(status, float_flag_underflow);
            }
            if (zSig1) float_raise(status, float_flag_inexact);
            if (roundNearestEven) increment = ((int64_t) zSig1 < 0);
            else {
                if (zSign) {
                    increment = (roundingMode == float_round_down) && zSig1;
                } else {
                    increment = (roundingMode == float_round_up) && zSig1;
                }
            }
            if (increment) {
                zSigExact = zSig0++;
                zSig0 &= ~(((uint64_t) (zSig1<<1) == 0) & roundNearestEven);
                if (zSig0 > zSigExact) set_float_rounding_up(status);
                if ((int64_t) zSig0 < 0) zExp = 1;
            }
            return packFloatx80(zSign, zExp, zSig0);
        }
    }
    if (zSig1) float_raise(status, float_flag_inexact);
    if (increment) {
        zSigExact = zSig0++;
        if (zSig0 == 0) {
            zExp++;
            zSig0 = U64(0x8000000000000000);
            zSigExact >>= 1;  // must scale also, or else later tests will fail
        }
        else {
            zSig0 &= ~(((uint64_t) (zSig1<<1) == 0) & roundNearestEven);
        }
        if (zSig0 > zSigExact) set_float_rounding_up(status);
    }
    else {
        if (zSig0 == 0) zExp = 0;
    }
    return packFloatx80(zSign, zExp, zSig0);
}

floatx80 roundAndPackFloatx80(int roundingPrecision,
        int zSign, int32_t zExp, uint64_t zSig0, uint64_t zSig1, float_status_t *status)
{
    float_status_t round_status = *status;
    floatx80 result = SoftFloatRoundAndPackFloatx80(roundingPrecision, zSign, zExp, zSig0, zSig1, status);

    // bias unmasked undeflow
    if (status->float_exception_flags & ~status->float_exception_masks & float_flag_underflow) {
        *status = round_status;
       float_raise(status, float_flag_underflow);
       return SoftFloatRoundAndPackFloatx80(roundingPrecision, zSign, zExp + 0x6000, zSig0, zSig1, status);
    }

    // bias unmasked overflow
    if (status->float_exception_flags & ~status->float_exception_masks & float_flag_overflow) {
        *status = round_status;
       float_raise(status, float_flag_overflow);
       return SoftFloatRoundAndPackFloatx80(roundingPrecision, zSign, zExp - 0x6000, zSig0, zSig1, status);
    }

    return result;
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent
| `zExp', and significand formed by the concatenation of `zSig0' and `zSig1',
| and returns the proper extended double-precision floating-point value
| corresponding to the abstract input.  This routine is just like
| `roundAndPackFloatx80' except that the input significand does not have to be
| normalized.
*----------------------------------------------------------------------------*/

floatx80 normalizeRoundAndPackFloatx80(int roundingPrecision,
        int zSign, int32_t zExp, uint64_t zSig0, uint64_t zSig1, float_status_t *status)
{
    if (zSig0 == 0) {
        zSig0 = zSig1;
        zSig1 = 0;
        zExp -= 64;
    }
    int shiftCount = countLeadingZeros64(zSig0);
    shortShift128Left(zSig0, zSig1, shiftCount, &zSig0, &zSig1);
    zExp -= shiftCount;
    return
        roundAndPackFloatx80(roundingPrecision, zSign, zExp, zSig0, zSig1, status);
}

#endif

#ifdef FLOAT128

/*----------------------------------------------------------------------------
| Normalizes the subnormal quadruple-precision floating-point value
| represented by the denormalized significand formed by the concatenation of
| `aSig0' and `aSig1'.  The normalized exponent is stored at the location
| pointed to by `zExpPtr'.  The most significant 49 bits of the normalized
| significand are stored at the location pointed to by `zSig0Ptr', and the
| least significant 64 bits of the normalized significand are stored at the
| location pointed to by `zSig1Ptr'.
*----------------------------------------------------------------------------*/

void normalizeFloat128Subnormal(
     uint64_t aSig0, uint64_t aSig1, int32_t *zExpPtr, uint64_t *zSig0Ptr, uint64_t *zSig1Ptr)
{
    int shiftCount;

    if (aSig0 == 0) {
        shiftCount = countLeadingZeros64(aSig1) - 15;
        if (shiftCount < 0) {
            *zSig0Ptr = aSig1 >>(-shiftCount);
            *zSig1Ptr = aSig1 << (shiftCount & 63);
        }
        else {
            *zSig0Ptr = aSig1 << shiftCount;
            *zSig1Ptr = 0;
        }
        *zExpPtr = - shiftCount - 63;
    }
    else {
        shiftCount = countLeadingZeros64(aSig0) - 15;
        shortShift128Left(aSig0, aSig1, shiftCount, zSig0Ptr, zSig1Ptr);
        *zExpPtr = 1 - shiftCount;
    }
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and extended significand formed by the concatenation of `zSig0', `zSig1',
| and `zSig2', and returns the proper quadruple-precision floating-point value
| corresponding to the abstract input.  Ordinarily, the abstract value is
| simply rounded and packed into the quadruple-precision format, with the
| inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal quadruple-
| precision floating-point number.
|     The input significand must be normalized or smaller.  If the input
| significand is not normalized, `zExp' must be 0; in that case, the result
| returned is a subnormal number, and it must not require rounding.  In the
| usual case that the input significand is normalized, `zExp' must be 1 less
| than the ``true'' floating-point exponent.  The handling of underflow and
| overflow follows the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 roundAndPackFloat128(
     int zSign, int32_t zExp, uint64_t zSig0, uint64_t zSig1, uint64_t zSig2, float_status_t *status)
{
    int increment = ((int64_t) zSig2 < 0);
    if (0x7FFD <= (uint32_t) zExp) {
        if ((0x7FFD < zExp)
             || ((zExp == 0x7FFD)
                  && eq128(U64(0x0001FFFFFFFFFFFF),
                         U64(0xFFFFFFFFFFFFFFFF), zSig0, zSig1)
                  && increment))
        {
            float_raise(status, float_flag_overflow | float_flag_inexact);
            return packFloat128(zSign, 0x7FFF, 0, 0);
        }
        if (zExp < 0) {
            int isTiny = (zExp < -1)
                || ! increment
                || lt128(zSig0, zSig1,
                       U64(0x0001FFFFFFFFFFFF),
                       U64(0xFFFFFFFFFFFFFFFF));
            shift128ExtraRightJamming(
                zSig0, zSig1, zSig2, -zExp, &zSig0, &zSig1, &zSig2);
            zExp = 0;
            if (isTiny && zSig2) float_raise(status, float_flag_underflow);
            increment = ((int64_t) zSig2 < 0);
        }
    }
    if (zSig2) float_raise(status, float_flag_inexact);
    if (increment) {
        add128(zSig0, zSig1, 0, 1, &zSig0, &zSig1);
        zSig1 &= ~((zSig2 + zSig2 == 0) & 1);
    }
    else {
        if ((zSig0 | zSig1) == 0) zExp = 0;
    }
    return packFloat128(zSign, zExp, zSig0, zSig1);
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand formed by the concatenation of `zSig0' and `zSig1', and
| returns the proper quadruple-precision floating-point value corresponding
| to the abstract input.  This routine is just like `roundAndPackFloat128'
| except that the input significand has fewer bits and does not have to be
| normalized.  In all cases, `zExp' must be 1 less than the ``true'' floating-
| point exponent.
*----------------------------------------------------------------------------*/

float128 normalizeRoundAndPackFloat128(
     int zSign, int32_t zExp, uint64_t zSig0, uint64_t zSig1, float_status_t *status)
{
    uint64_t zSig2;

    if (zSig0 == 0) {
        zSig0 = zSig1;
        zSig1 = 0;
        zExp -= 64;
    }
    int shiftCount = countLeadingZeros64(zSig0) - 15;
    if (0 <= shiftCount) {
        zSig2 = 0;
        shortShift128Left(zSig0, zSig1, shiftCount, &zSig0, &zSig1);
    }
    else {
        shift128ExtraRightJamming(
            zSig0, zSig1, 0, -shiftCount, &zSig0, &zSig1, &zSig2);
    }
    zExp -= shiftCount;
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);
}

#endif

/*============================================================================
This C source file is part of the SoftFloat IEC/IEEE Floating-point Arithmetic
Package, Release 2b.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://www.cs.berkeley.edu/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

/*----------------------------------------------------------------------------
| Takes three single-precision floating-point values `a', `b' and `c', one of
| which is a NaN, and returns the appropriate NaN result.  If any of  `a',
| `b' or `c' is a signaling NaN, the invalid exception is raised.
| The input infzero indicates whether a*b was 0*inf or inf*0 (in which case
| obviously c is a NaN, and whether to propagate c or some other NaN is
| implementation defined).
*----------------------------------------------------------------------------*/

static float32 propagateFloat32MulAddNaN(float32 a, float32 b, float32 c, float_status_t *status)
{
    int aIsNaN = float32_is_nan(a);
    int bIsNaN = float32_is_nan(b);

    int aIsSignalingNaN = float32_is_signaling_nan(a);
    int bIsSignalingNaN = float32_is_signaling_nan(b);
    int cIsSignalingNaN = float32_is_signaling_nan(c);

    a |= 0x00400000;
    b |= 0x00400000;
    c |= 0x00400000;

    if (aIsSignalingNaN | bIsSignalingNaN | cIsSignalingNaN)
        float_raise(status, float_flag_invalid);

    //  operate according to float_first_operand_nan mode
    if (aIsSignalingNaN | aIsNaN) {
        return a;
    }
    else {
        return (bIsSignalingNaN | bIsNaN) ? b : c;
    }
}
 
/*----------------------------------------------------------------------------
| Takes three double-precision floating-point values `a', `b' and `c', one of
| which is a NaN, and returns the appropriate NaN result.  If any of  `a',
| `b' or `c' is a signaling NaN, the invalid exception is raised.
| The input infzero indicates whether a*b was 0*inf or inf*0 (in which case
| obviously c is a NaN, and whether to propagate c or some other NaN is
| implementation defined).
*----------------------------------------------------------------------------*/

static float64 propagateFloat64MulAddNaN(float64 a, float64 b, float64 c, float_status_t *status)
{
    int aIsNaN = float64_is_nan(a);
    int bIsNaN = float64_is_nan(b);

    int aIsSignalingNaN = float64_is_signaling_nan(a);
    int bIsSignalingNaN = float64_is_signaling_nan(b);
    int cIsSignalingNaN = float64_is_signaling_nan(c);

    a |= U64(0x0008000000000000);
    b |= U64(0x0008000000000000);
    c |= U64(0x0008000000000000);

    if (aIsSignalingNaN | bIsSignalingNaN | cIsSignalingNaN)
        float_raise(status, float_flag_invalid);

    //  operate according to float_first_operand_nan mode
    if (aIsSignalingNaN | aIsNaN) {
        return a;
    }
    else {
        return (bIsSignalingNaN | bIsNaN) ? b : c;
    }
}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the single-precision floating-point values
| `a' and `b' then adding 'c', with no intermediate rounding step after the
| multiplication.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic 754-2008.
| The flags argument allows the caller to select negation of the
| addend, the intermediate product, or the final result. (The difference
| between this and having the caller do a separate negation is that negating
| externally will flip the sign bit on NaNs.)
*----------------------------------------------------------------------------*/

float32 float32_muladd(float32 a, float32 b, float32 c, int flags, float_status_t *status)
{
    int aSign, bSign, cSign, zSign;
    int16_t aExp, bExp, cExp, pExp, zExp;
    uint32_t aSig, bSig, cSig;
    int pInf, pZero, pSign;
    uint64_t pSig64, cSig64, zSig64;
    uint32_t pSig;
    int shiftcount;

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);
    bSig = extractFloat32Frac(b);
    bExp = extractFloat32Exp(b);
    bSign = extractFloat32Sign(b);
    cSig = extractFloat32Frac(c);
    cExp = extractFloat32Exp(c);
    cSign = extractFloat32Sign(c);

    /* It is implementation-defined whether the cases of (0,inf,qnan)
     * and (inf,0,qnan) raise InvalidOperation or not (and what QNaN
     * they return if they do), so we have to hand this information
     * off to the target-specific pick-a-NaN routine.
     */
    if (((aExp == 0xff) && aSig) ||
        ((bExp == 0xff) && bSig) ||
        ((cExp == 0xff) && cSig)) {
        return propagateFloat32MulAddNaN(a, b, c, status);
    }

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
        if (cExp == 0) cSig = 0;
    }

    int infzero = ((aExp == 0 && aSig == 0 && bExp == 0xff && bSig == 0) ||
                   (aExp == 0xff && aSig == 0 && bExp == 0 && bSig == 0));

    if (infzero) {
        float_raise(status, float_flag_invalid);
        return float32_default_nan;
    }

    if (flags & float_muladd_negate_c) {
        cSign ^= 1;
    }

    /* Work out the sign and type of the product */
    pSign = aSign ^ bSign;
    if (flags & float_muladd_negate_product) {
        pSign ^= 1;
    }
    pInf = (aExp == 0xff) || (bExp == 0xff);
    pZero = ((aExp | aSig) == 0) || ((bExp | bSig) == 0);

    if (cExp == 0xff) {
        if (pInf && (pSign ^ cSign)) {
            /* addition of opposite-signed infinities => InvalidOperation */
            float_raise(status, float_flag_invalid);
            return float32_default_nan;
        }
        /* Otherwise generate an infinity of the same sign */
        if ((aSig && aExp == 0) || (bSig && bExp == 0)) {
            float_raise(status, float_flag_denormal);
        }
        return packFloat32(cSign, 0xff, 0);
    }

    if (pInf) {
        if ((aSig && aExp == 0) || (bSig && bExp == 0) || (cSig && cExp == 0)) {
            float_raise(status, float_flag_denormal);
        }
        return packFloat32(pSign, 0xff, 0);
    }

    if (pZero) {
        if (cExp == 0) {
            if (cSig == 0) {
                /* Adding two exact zeroes */
                if (pSign == cSign) {
                    zSign = pSign;
                } else if (get_float_rounding_mode(status) == float_round_down) {
                    zSign = 1;
                } else {
                    zSign = 0;
                }
                return packFloat32(zSign, 0, 0);
            }
            /* Exact zero plus a denormal */
            float_raise(status, float_flag_denormal);
            if (get_flush_underflow_to_zero(status)) {
                float_raise(status, float_flag_underflow | float_flag_inexact);
                return packFloat32(cSign, 0, 0);
            }
        }
        /* Zero plus something non-zero */
        return packFloat32(cSign, cExp, cSig);
    }

    if (aExp == 0) {
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(bSig, &bExp, &bSig);
    }

    /* Calculate the actual result a * b + c */

    /* Multiply first; this is easy. */
    /* NB: we subtract 0x7e where float32_mul() subtracts 0x7f
     * because we want the true exponent, not the "one-less-than"
     * flavour that roundAndPackFloat32() takes.
     */
    pExp = aExp + bExp - 0x7e;
    aSig = (aSig | 0x00800000) << 7;
    bSig = (bSig | 0x00800000) << 8;
    pSig64 = (uint64_t)aSig * bSig;
    if ((int64_t)(pSig64 << 1) >= 0) {
        pSig64 <<= 1;
        pExp--;
    }

    zSign = pSign;

    /* Now pSig64 is the significand of the multiply, with the explicit bit in
     * position 62.
     */
    if (cExp == 0) {
        if (!cSig) {
            /* Throw out the special case of c being an exact zero now */
            pSig = (uint32_t) shift64RightJamming(pSig64, 32);
            return roundAndPackFloat32(zSign, pExp - 1, pSig, status);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloat32Subnormal(cSig, &cExp, &cSig);
    }

    cSig64 = (uint64_t)cSig << 39;
    cSig64 |= U64(0x4000000000000000);
    int expDiff = pExp - cExp;

    if (pSign == cSign) {
        /* Addition */
        if (expDiff > 0) {
            /* scale c to match p */
            cSig64 = shift64RightJamming(cSig64, expDiff);
            zExp = pExp;
        } else if (expDiff < 0) {
            /* scale p to match c */
            pSig64 = shift64RightJamming(pSig64, -expDiff);
            zExp = cExp;
        } else {
            /* no scaling needed */
            zExp = cExp;
        }
        /* Add significands and make sure explicit bit ends up in posn 62 */
        zSig64 = pSig64 + cSig64;
        if ((int64_t)zSig64 < 0) {
            zSig64 = shift64RightJamming(zSig64, 1);
        } else {
            zExp--;
        }
        zSig64 = shift64RightJamming(zSig64, 32);
        return roundAndPackFloat32(zSign, zExp, zSig64, status);
    } else {
        /* Subtraction */
        if (expDiff > 0) {
            cSig64 = shift64RightJamming(cSig64, expDiff);
            zSig64 = pSig64 - cSig64;
            zExp = pExp;
        } else if (expDiff < 0) {
            pSig64 = shift64RightJamming(pSig64, -expDiff);
            zSig64 = cSig64 - pSig64;
            zExp = cExp;
            zSign ^= 1;
        } else {
            zExp = pExp;
            if (cSig64 < pSig64) {
                zSig64 = pSig64 - cSig64;
            } else if (pSig64 < cSig64) {
                zSig64 = cSig64 - pSig64;
                zSign ^= 1;
            } else {
                /* Exact zero */
                return packFloat32(get_float_rounding_mode(status) == float_round_down, 0, 0);
            }
        }
        --zExp;
        /* Do the equivalent of normalizeRoundAndPackFloat32() but
         * starting with the significand in a uint64_t.
         */
        shiftcount = countLeadingZeros64(zSig64) - 1;
        zSig64 <<= shiftcount;
        zExp -= shiftcount;
        zSig64 = shift64RightJamming(zSig64, 32);
        return roundAndPackFloat32(zSign, zExp, zSig64, status);
    }
}
 
/*----------------------------------------------------------------------------
| Returns the result of multiplying the double-precision floating-point values
| `a' and `b' then adding 'c', with no intermediate rounding step after the
| multiplication.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic 754-2008.
| The flags argument allows the caller to select negation of the
| addend, the intermediate product, or the final result. (The difference
| between this and having the caller do a separate negation is that negating
| externally will flip the sign bit on NaNs.)
*----------------------------------------------------------------------------*/

float64 float64_muladd(float64 a, float64 b, float64 c, int flags, float_status_t *status)
{
    int aSign, bSign, cSign, zSign;
    int16_t aExp, bExp, cExp, pExp, zExp;
    uint64_t aSig, bSig, cSig;
    int pInf, pZero, pSign;
    uint64_t pSig0, pSig1, cSig0, cSig1, zSig0, zSig1;
    int shiftcount;

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);
    bSig = extractFloat64Frac(b);
    bExp = extractFloat64Exp(b);
    bSign = extractFloat64Sign(b);
    cSig = extractFloat64Frac(c);
    cExp = extractFloat64Exp(c);
    cSign = extractFloat64Sign(c);

    /* It is implementation-defined whether the cases of (0,inf,qnan)
     * and (inf,0,qnan) raise InvalidOperation or not (and what QNaN
     * they return if they do), so we have to hand this information
     * off to the target-specific pick-a-NaN routine.
     */
    if (((aExp == 0x7ff) && aSig) ||
        ((bExp == 0x7ff) && bSig) ||
        ((cExp == 0x7ff) && cSig)) {
        return propagateFloat64MulAddNaN(a, b, c, status);
    }

    if (get_denormals_are_zeros(status)) {
        if (aExp == 0) aSig = 0;
        if (bExp == 0) bSig = 0;
        if (cExp == 0) cSig = 0;
    }

    int infzero = ((aExp == 0 && aSig == 0 && bExp == 0x7ff && bSig == 0) ||
                   (aExp == 0x7ff && aSig == 0 && bExp == 0 && bSig == 0));

    if (infzero) {
        float_raise(status, float_flag_invalid);
        return float64_default_nan;
    }

    if (flags & float_muladd_negate_c) {
        cSign ^= 1;
    }

    /* Work out the sign and type of the product */
    pSign = aSign ^ bSign;
    if (flags & float_muladd_negate_product) {
        pSign ^= 1;
    }
    pInf = (aExp == 0x7ff) || (bExp == 0x7ff);
    pZero = ((aExp | aSig) == 0) || ((bExp | bSig) == 0);

    if (cExp == 0x7ff) {
        if (pInf && (pSign ^ cSign)) {
            /* addition of opposite-signed infinities => InvalidOperation */
            float_raise(status, float_flag_invalid);
            return float64_default_nan;
        }
        /* Otherwise generate an infinity of the same sign */
        if ((aSig && aExp == 0) || (bSig && bExp == 0)) {
            float_raise(status, float_flag_denormal);
        }
        return packFloat64(cSign, 0x7ff, 0);
    }

    if (pInf) {
        if ((aSig && aExp == 0) || (bSig && bExp == 0) || (cSig && cExp == 0)) {
            float_raise(status, float_flag_denormal);
        }
        return packFloat64(pSign, 0x7ff, 0);
    }

    if (pZero) {
        if (cExp == 0) {
            if (cSig == 0) {
                /* Adding two exact zeroes */
                if (pSign == cSign) {
                    zSign = pSign;
                } else if (get_float_rounding_mode(status) == float_round_down) {
                    zSign = 1;
                } else {
                    zSign = 0;
                }
                return packFloat64(zSign, 0, 0);
            }
            /* Exact zero plus a denormal */
            float_raise(status, float_flag_denormal);
            if (get_flush_underflow_to_zero(status)) {
                float_raise(status, float_flag_underflow | float_flag_inexact);
                return packFloat64(cSign, 0, 0);
            }
        }
        /* Zero plus something non-zero */
        return packFloat64(cSign, cExp, cSig);
    }

    if (aExp == 0) {
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(bSig, &bExp, &bSig);
    }

    /* Calculate the actual result a * b + c */

    /* Multiply first; this is easy. */
    /* NB: we subtract 0x3fe where float64_mul() subtracts 0x3ff
     * because we want the true exponent, not the "one-less-than"
     * flavour that roundAndPackFloat64() takes.
     */
    pExp = aExp + bExp - 0x3fe;
    aSig = (aSig | U64(0x0010000000000000))<<10;
    bSig = (bSig | U64(0x0010000000000000))<<11;
    mul64To128(aSig, bSig, &pSig0, &pSig1);
    if ((int64_t)(pSig0 << 1) >= 0) {
        shortShift128Left(pSig0, pSig1, 1, &pSig0, &pSig1);
        pExp--;
    }

    zSign = pSign;

    /* Now [pSig0:pSig1] is the significand of the multiply, with the explicit
     * bit in position 126.
     */
    if (cExp == 0) {
        if (!cSig) {
            /* Throw out the special case of c being an exact zero now */
            shift128RightJamming(pSig0, pSig1, 64, &pSig0, &pSig1);
            return roundAndPackFloat64(zSign, pExp - 1, pSig1, status);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloat64Subnormal(cSig, &cExp, &cSig);
    }

    cSig0 = cSig << 10;
    cSig1 = 0;
    cSig0 |= U64(0x4000000000000000);
    int expDiff = pExp - cExp;

    if (pSign == cSign) {
        /* Addition */
        if (expDiff > 0) {
            /* scale c to match p */
            shift128RightJamming(cSig0, cSig1, expDiff, &cSig0, &cSig1);
            zExp = pExp;
        } else if (expDiff < 0) {
            /* scale p to match c */
            shift128RightJamming(pSig0, pSig1, -expDiff, &pSig0, &pSig1);
            zExp = cExp;
        } else {
            /* no scaling needed */
            zExp = cExp;
        }
        /* Add significands and make sure explicit bit ends up in posn 126 */
        add128(pSig0, pSig1, cSig0, cSig1, &zSig0, &zSig1);
        if ((int64_t)zSig0 < 0) {
            shift128RightJamming(zSig0, zSig1, 1, &zSig0, &zSig1);
        } else {
            zExp--;
        }
        shift128RightJamming(zSig0, zSig1, 64, &zSig0, &zSig1);
        return roundAndPackFloat64(zSign, zExp, zSig1, status);
    } else {
        /* Subtraction */
        if (expDiff > 0) {
            shift128RightJamming(cSig0, cSig1, expDiff, &cSig0, &cSig1);
            sub128(pSig0, pSig1, cSig0, cSig1, &zSig0, &zSig1);
            zExp = pExp;
        } else if (expDiff < 0) {
            shift128RightJamming(pSig0, pSig1, -expDiff, &pSig0, &pSig1);
            sub128(cSig0, cSig1, pSig0, pSig1, &zSig0, &zSig1);
            zExp = cExp;
            zSign ^= 1;
        } else {
            zExp = pExp;
            if (lt128(cSig0, cSig1, pSig0, pSig1)) {
                sub128(pSig0, pSig1, cSig0, cSig1, &zSig0, &zSig1);
            } else if (lt128(pSig0, pSig1, cSig0, cSig1)) {
                sub128(cSig0, cSig1, pSig0, pSig1, &zSig0, &zSig1);
                zSign ^= 1;
            } else {
                /* Exact zero */
                return packFloat64(get_float_rounding_mode(status) == float_round_down, 0, 0);
            }
        }
        --zExp;
        /* Do the equivalent of normalizeRoundAndPackFloat64() but
         * starting with the significand in a pair of uint64_t.
         */
        if (zSig0) {
            shiftcount = countLeadingZeros64(zSig0) - 1;
            shortShift128Left(zSig0, zSig1, shiftcount, &zSig0, &zSig1);
            if (zSig1) {
                zSig0 |= 1;
            }
            zExp -= shiftcount;
        } else {
            shiftcount = countLeadingZeros64(zSig1) - 1;
            zSig0 = zSig1 << shiftcount;
            zExp -= (shiftcount + 64);
        }
        return roundAndPackFloat64(zSign, zExp, zSig0, status);
    }
}

/*============================================================================
This source file is an extension to the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b, written for Bochs (x86 achitecture simulator)
floating point emulation.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

/*============================================================================
 * Written for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#define FLOAT128

//                            2         3         4               n
// f(x) ~ C + (C * x) + (C * x) + (C * x) + (C * x) + ... + (C * x)
//         0    1         2         3         4               n
//
//          --       2k                --        2k+1
//   p(x) = >  C  * x           q(x) = >  C   * x
//          --  2k                     --  2k+1
//
//   f(x) ~ [ p(x) + x * q(x) ]
//

float128 EvalPoly(float128 x, float128 *arr, int n, float_status_t *status)
{
    float128 r = arr[--n];

    do {
        r = float128_mul(r, x, status);
        r = float128_add(r, arr[--n], status);
    } while (n > 0);

    return r;
}

//                  2         4         6         8               2n
// f(x) ~ C + (C * x) + (C * x) + (C * x) + (C * x) + ... + (C * x)
//         0    1         2         3         4               n
//
//          --       4k                --        4k+2
//   p(x) = >  C  * x           q(x) = >  C   * x
//          --  2k                     --  2k+1
//
//                    2
//   f(x) ~ [ p(x) + x * q(x) ]
//

float128 EvenPoly(float128 x, float128 *arr, int n, float_status_t *status)
{
     return EvalPoly(float128_mul(x, x, status), arr, n, status);
}

//                        3         5         7         9               2n+1
// f(x) ~ (C * x) + (C * x) + (C * x) + (C * x) + (C * x) + ... + (C * x)
//          0         1         2         3         4               n
//                        2         4         6         8               2n
//      = x * [ C + (C * x) + (C * x) + (C * x) + (C * x) + ... + (C * x)
//               0    1         2         3         4               n
//
//          --       4k                --        4k+2
//   p(x) = >  C  * x           q(x) = >  C   * x
//          --  2k                     --  2k+1
//
//                        2
//   f(x) ~ x * [ p(x) + x * q(x) ]
//

float128 OddPoly(float128 x, float128 *arr, int n, float_status_t *status)
{
     return float128_mul(x, EvenPoly(x, arr, n, status), status);
}

/*============================================================================
This source file is an extension to the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b, written for Bochs (x86 achitecture simulator)
floating point emulation.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

/*============================================================================
 * Written for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#define FLOAT128

static const floatx80 floatx80_one =
    packFloatx80Constant(0, 0x3fff, U64(0x8000000000000000));

static const float128 float128_one =
    packFloat128Constant(U64(0x3fff000000000000), U64(0x0000000000000000));
static const float128 float128_two =
    packFloat128Constant(U64(0x4000000000000000), U64(0x0000000000000000));

static const float128 float128_ln2inv2 =
    packFloat128Constant(U64(0x400071547652b82f), U64(0xe1777d0ffda0d23a));

#define SQRT2_HALF_SIG 	U64(0xb504f333f9de6484)

extern float128 OddPoly(float128 x, float128 *arr, int n, float_status_t *status);

#define L2_ARR_SIZE 9

static float128 ln_arr[L2_ARR_SIZE] =
{
    PACK_FLOAT_128(0x3fff000000000000, 0x0000000000000000), /*  1 */
    PACK_FLOAT_128(0x3ffd555555555555, 0x5555555555555555), /*  3 */
    PACK_FLOAT_128(0x3ffc999999999999, 0x999999999999999a), /*  5 */
    PACK_FLOAT_128(0x3ffc249249249249, 0x2492492492492492), /*  7 */
    PACK_FLOAT_128(0x3ffbc71c71c71c71, 0xc71c71c71c71c71c), /*  9 */
    PACK_FLOAT_128(0x3ffb745d1745d174, 0x5d1745d1745d1746), /* 11 */
    PACK_FLOAT_128(0x3ffb3b13b13b13b1, 0x3b13b13b13b13b14), /* 13 */
    PACK_FLOAT_128(0x3ffb111111111111, 0x1111111111111111), /* 15 */
    PACK_FLOAT_128(0x3ffae1e1e1e1e1e1, 0xe1e1e1e1e1e1e1e2)  /* 17 */
};

static float128 poly_ln(float128 x1, float_status_t *status)
{
/*
    //
    //                     3     5     7     9     11     13     15
    //        1+u         u     u     u     u     u      u      u
    // 1/2 ln ---  ~ u + --- + --- + --- + --- + ---- + ---- + ---- =
    //        1-u         3     5     7     9     11     13     15
    //
    //                     2     4     6     8     10     12     14
    //                    u     u     u     u     u      u      u
    //       = u * [ 1 + --- + --- + --- + --- + ---- + ---- + ---- ] =
    //                    3     5     7     9     11     13     15
    //
    //           3                          3
    //          --       4k                --        4k+2
    //   p(u) = >  C  * u           q(u) = >  C   * u
    //          --  2k                     --  2k+1
    //          k=0                        k=0
    //
    //          1+u                 2
    //   1/2 ln --- ~ u * [ p(u) + u * q(u) ]
    //          1-u
    //
*/
    return OddPoly(x1, ln_arr, L2_ARR_SIZE, status);
}

/* required sqrt(2)/2 < x < sqrt(2) */
static float128 poly_l2(float128 x, float_status_t *status)
{
    /* using float128 for approximation */
    float128 x_p1 = float128_add(x, float128_one, status);
    float128 x_m1 = float128_sub(x, float128_one, status);
    x = float128_div(x_m1, x_p1, status);
    x = poly_ln(x, status);
    x = float128_mul(x, float128_ln2inv2, status);
    return x;
}

static float128 poly_l2p1(float128 x, float_status_t *status)
{
    /* using float128 for approximation */
    float128 x_p2 = float128_add(x, float128_two, status);
    x = float128_div(x, x_p2, status);
    x = poly_ln(x, status);
    x = float128_mul(x, float128_ln2inv2, status);
    return x;
}

// =================================================
// FYL2X                   Compute y * log (x)
//                                        2
// =================================================

//
// Uses the following identities:
//
// 1. ----------------------------------------------------------
//              ln(x)
//   log (x) = -------,  ln (x*y) = ln(x) + ln(y)
//      2       ln(2)
//
// 2. ----------------------------------------------------------
//                1+u             x-1
//   ln (x) = ln -----, when u = -----
//                1-u             x+1
//
// 3. ----------------------------------------------------------
//                        3     5     7           2n+1
//       1+u             u     u     u           u
//   ln ----- = 2 [ u + --- + --- + --- + ... + ------ + ... ]
//       1-u             3     5     7           2n+1
//

floatx80 fyl2x(floatx80 a, floatx80 b, float_status_t *status)
{
    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a) || floatx80_is_unsupported(b)) {
invalid:
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    uint64_t aSig = extractFloatx80Frac(a);
    int32_t aExp = extractFloatx80Exp(a);
    int aSign = extractFloatx80Sign(a);
    uint64_t bSig = extractFloatx80Frac(b);
    int32_t bExp = extractFloatx80Exp(b);
    int bSign = extractFloatx80Sign(b);

    int zSign = bSign ^ 1;

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)
             || ((bExp == 0x7FFF) && (uint64_t) (bSig<<1)))
        {
            return propagateFloatx80NaN_two_args(a, b, status);
        }
        if (aSign) goto invalid;
        else {
            if (bExp == 0) {
                if (bSig == 0) goto invalid;
                float_raise(status, float_flag_denormal);
            }
            return packFloatx80(bSign, 0x7FFF, U64(0x8000000000000000));
        }
    }
    if (bExp == 0x7FFF)
    {
        if ((uint64_t) (bSig<<1)) return propagateFloatx80NaN_two_args(a, b, status);
        if (aSign && (uint64_t)(aExp | aSig)) goto invalid;
        if (aSig && (aExp == 0))
            float_raise(status, float_flag_denormal);
        if (aExp < 0x3FFF) {
            return packFloatx80(zSign, 0x7FFF, U64(0x8000000000000000));
        }
        if (aExp == 0x3FFF && ((uint64_t) (aSig<<1) == 0)) goto invalid;
        return packFloatx80(bSign, 0x7FFF, U64(0x8000000000000000));
    }
    if (aExp == 0) {
        if (aSig == 0) {
            if ((bExp | bSig) == 0) goto invalid;
            float_raise(status, float_flag_divbyzero);
            return packFloatx80(zSign, 0x7FFF, U64(0x8000000000000000));
        }
        if (aSign) goto invalid;
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    if (aSign) goto invalid;
    if (bExp == 0) {
        if (bSig == 0) {
            if (aExp < 0x3FFF) return packFloatx80(zSign, 0, 0);
            return packFloatx80(bSign, 0, 0);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }
    if (aExp == 0x3FFF && ((uint64_t) (aSig<<1) == 0))
        return packFloatx80(bSign, 0, 0);

    float_raise(status, float_flag_inexact);

    int ExpDiff = aExp - 0x3FFF;
    aExp = 0;
    if (aSig >= SQRT2_HALF_SIG) {
        ExpDiff++;
        aExp--;
    }

    /* ******************************** */
    /* using float128 for approximation */
    /* ******************************** */

    uint64_t zSig0, zSig1;
    shift128Right(aSig<<1, 0, 16, &zSig0, &zSig1);
    float128 x = packFloat128(0, aExp+0x3FFF, zSig0, zSig1);
    x = poly_l2(x, status);
    x = float128_add(x, int64_to_float128((int64_t) ExpDiff), status);
    return floatx80_mul_with_float128(b, x, status);
}

// =================================================
// FYL2XP1                 Compute y * log (x + 1)
//                                        2
// =================================================

//
// Uses the following identities:
//
// 1. ----------------------------------------------------------
//              ln(x)
//   log (x) = -------
//      2       ln(2)
//
// 2. ----------------------------------------------------------
//                  1+u              x
//   ln (x+1) = ln -----, when u = -----
//                  1-u             x+2
//
// 3. ----------------------------------------------------------
//                        3     5     7           2n+1
//       1+u             u     u     u           u
//   ln ----- = 2 [ u + --- + --- + --- + ... + ------ + ... ]
//       1-u             3     5     7           2n+1
//

floatx80 fyl2xp1(floatx80 a, floatx80 b, float_status_t *status)
{
    int32_t aExp, bExp;
    uint64_t aSig, bSig, zSig0, zSig1, zSig2;
    int aSign, bSign;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a) || floatx80_is_unsupported(b)) {
invalid:
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);
    bSign = extractFloatx80Sign(b);
    int zSign = aSign ^ bSign;

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)
             || ((bExp == 0x7FFF) && (uint64_t) (bSig<<1)))
        {
            return propagateFloatx80NaN_two_args(a, b, status);
        }
        if (aSign) goto invalid;
        else {
            if (bExp == 0) {
                if (bSig == 0) goto invalid;
                float_raise(status, float_flag_denormal);
            }
            return packFloatx80(bSign, 0x7FFF, U64(0x8000000000000000));
        }
    }
    if (bExp == 0x7FFF)
    {
        if ((uint64_t) (bSig<<1))
            return propagateFloatx80NaN_two_args(a, b, status);

        if (aExp == 0) {
            if (aSig == 0) goto invalid;
            float_raise(status, float_flag_denormal);
        }

        return packFloatx80(zSign, 0x7FFF, U64(0x8000000000000000));
    }
    if (aExp == 0) {
        if (aSig == 0) {
            if (bSig && (bExp == 0)) float_raise(status, float_flag_denormal);
            return packFloatx80(zSign, 0, 0);
        }
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        if (bSig == 0) return packFloatx80(zSign, 0, 0);
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }

    float_raise(status, float_flag_inexact);

    if (aSign && aExp >= 0x3FFF)
        return a;

    if (aExp >= 0x3FFC) // big argument
    {
        return fyl2x(floatx80_add(a, floatx80_one, status), b, status);
    }

    // handle tiny argument
    if (aExp < FLOATX80_EXP_BIAS-70)
    {
        // first order approximation, return (a*b)/ln(2)
        int32_t zExp = aExp + FLOAT_LN2INV_EXP - 0x3FFE;

	mul128By64To192(FLOAT_LN2INV_HI, FLOAT_LN2INV_LO, aSig, &zSig0, &zSig1, &zSig2);
        if (0 < (int64_t) zSig0) {
            shortShift128Left(zSig0, zSig1, 1, &zSig0, &zSig1);
            --zExp;
        }

        zExp = zExp + bExp - 0x3FFE;
	mul128By64To192(zSig0, zSig1, bSig, &zSig0, &zSig1, &zSig2);
        if (0 < (int64_t) zSig0) {
            shortShift128Left(zSig0, zSig1, 1, &zSig0, &zSig1);
            --zExp;
        }

        return
            roundAndPackFloatx80(80, aSign ^ bSign, zExp, zSig0, zSig1, status);
    }

    /* ******************************** */
    /* using float128 for approximation */
    /* ******************************** */

    shift128Right(aSig<<1, 0, 16, &zSig0, &zSig1);
    float128 x = packFloat128(aSign, aExp, zSig0, zSig1);
    x = poly_l2p1(x, status);
    return floatx80_mul_with_float128(b, x, status);
}

/*============================================================================
This source file is an extension to the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b, written for Bochs (x86 achitecture simulator)
floating point emulation.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

/*============================================================================
 * Written for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#define FLOAT128

#define USE_estimateDiv128To64 // ?? needed?

/* reduce trigonometric function argument using 128-bit precision
   M_PI approximation */
static uint64_t argument_reduction_kernel(uint64_t aSig0, int Exp, uint64_t *zSig0, uint64_t *zSig1)
{
    uint64_t term0, term1, term2;
    uint64_t aSig1 = 0;

    shortShift128Left(aSig1, aSig0, Exp, &aSig1, &aSig0);
    uint64_t q = estimateDiv128To64(aSig1, aSig0, FLOAT_PI_HI);
    mul128By64To192(FLOAT_PI_HI, FLOAT_PI_LO, q, &term0, &term1, &term2);
    sub128(aSig1, aSig0, term0, term1, zSig1, zSig0);
    while ((int64_t)(*zSig1) < 0) {
        --q;
        add192(*zSig1, *zSig0, term2, 0, FLOAT_PI_HI, FLOAT_PI_LO, zSig1, zSig0, &term2);
    }
    *zSig1 = term2;
    return q;
}


static int reduce_trig_arg(int expDiff, int *zSign_input, uint64_t *aSig0_input, uint64_t *aSig1_input)
{
    int zSign = *zSign_input;
    uint64_t aSig0 = *aSig0_input, aSig1 = *aSig1_input;

    uint64_t term0, term1, q = 0;

    if (expDiff < 0) {
        shift128Right(aSig0, 0, 1, &aSig0, &aSig1);
        expDiff = 0;
    }
    if (expDiff > 0) {
        q = argument_reduction_kernel(aSig0, expDiff, &aSig0, &aSig1);
    }
    else {
        if (FLOAT_PI_HI <= aSig0) {
            aSig0 -= FLOAT_PI_HI;
            q = 1;
        }
    }

    shift128Right(FLOAT_PI_HI, FLOAT_PI_LO, 1, &term0, &term1);
    if (! lt128(aSig0, aSig1, term0, term1))
    {
        int lt = lt128(term0, term1, aSig0, aSig1);
        int eq = eq128(aSig0, aSig1, term0, term1);

        if ((eq && (q & 1)) || lt) {
            zSign = !zSign;
            ++q;
        }
        if (lt) sub128(FLOAT_PI_HI, FLOAT_PI_LO, aSig0, aSig1, &aSig0, &aSig1);
    }

    *zSign_input = zSign;
    *aSig0_input = aSig0;
    *aSig1_input = aSig1;
    return (int)(q & 3);
}

#define SIN_ARR_SIZE 11
#define COS_ARR_SIZE 11

static float128 sin_arr[SIN_ARR_SIZE] =
{
    PACK_FLOAT_128(0x3fff000000000000, 0x0000000000000000), /*  1 */
    PACK_FLOAT_128(0xbffc555555555555, 0x5555555555555555), /*  3 */
    PACK_FLOAT_128(0x3ff8111111111111, 0x1111111111111111), /*  5 */
    PACK_FLOAT_128(0xbff2a01a01a01a01, 0xa01a01a01a01a01a), /*  7 */
    PACK_FLOAT_128(0x3fec71de3a556c73, 0x38faac1c88e50017), /*  9 */
    PACK_FLOAT_128(0xbfe5ae64567f544e, 0x38fe747e4b837dc7), /* 11 */
    PACK_FLOAT_128(0x3fde6124613a86d0, 0x97ca38331d23af68), /* 13 */
    PACK_FLOAT_128(0xbfd6ae7f3e733b81, 0xf11d8656b0ee8cb0), /* 15 */
    PACK_FLOAT_128(0x3fce952c77030ad4, 0xa6b2605197771b00), /* 17 */
    PACK_FLOAT_128(0xbfc62f49b4681415, 0x724ca1ec3b7b9675), /* 19 */
    PACK_FLOAT_128(0x3fbd71b8ef6dcf57, 0x18bef146fcee6e45)  /* 21 */
};

static float128 cos_arr[COS_ARR_SIZE] =
{
    PACK_FLOAT_128(0x3fff000000000000, 0x0000000000000000), /*  0 */
    PACK_FLOAT_128(0xbffe000000000000, 0x0000000000000000), /*  2 */
    PACK_FLOAT_128(0x3ffa555555555555, 0x5555555555555555), /*  4 */
    PACK_FLOAT_128(0xbff56c16c16c16c1, 0x6c16c16c16c16c17), /*  6 */
    PACK_FLOAT_128(0x3fefa01a01a01a01, 0xa01a01a01a01a01a), /*  8 */
    PACK_FLOAT_128(0xbfe927e4fb7789f5, 0xc72ef016d3ea6679), /* 10 */
    PACK_FLOAT_128(0x3fe21eed8eff8d89, 0x7b544da987acfe85), /* 12 */
    PACK_FLOAT_128(0xbfda93974a8c07c9, 0xd20badf145dfa3e5), /* 14 */
    PACK_FLOAT_128(0x3fd2ae7f3e733b81, 0xf11d8656b0ee8cb0), /* 16 */
    PACK_FLOAT_128(0xbfca6827863b97d9, 0x77bb004886a2c2ab), /* 18 */
    PACK_FLOAT_128(0x3fc1e542ba402022, 0x507a9cad2bf8f0bb)  /* 20 */
};

extern float128 OddPoly (float128 x, float128 *arr, int n, float_status_t *status);

/* 0 <= x <= pi/4 */
BX_CPP_INLINE float128 poly_sin(float128 x, float_status_t *status)
{
    //                 3     5     7     9     11     13     15
    //                x     x     x     x     x      x      x
    // sin (x) ~ x - --- + --- - --- + --- - ---- + ---- - ---- =
    //                3!    5!    7!    9!    11!    13!    15!
    //
    //                 2     4     6     8     10     12     14
    //                x     x     x     x     x      x      x
    //   = x * [ 1 - --- + --- - --- + --- - ---- + ---- - ---- ] =
    //                3!    5!    7!    9!    11!    13!    15!
    //
    //           3                          3
    //          --       4k                --        4k+2
    //   p(x) = >  C  * x   > 0     q(x) = >  C   * x     < 0
    //          --  2k                     --  2k+1
    //          k=0                        k=0
    //
    //                          2
    //   sin(x) ~ x * [ p(x) + x * q(x) ]
    //

    return OddPoly(x, sin_arr, SIN_ARR_SIZE, status);
}

extern float128 EvenPoly(float128 x, float128 *arr, int n, float_status_t *status);

/* 0 <= x <= pi/4 */
BX_CPP_INLINE float128 poly_cos(float128 x, float_status_t *status)
{
    //                 2     4     6     8     10     12     14
    //                x     x     x     x     x      x      x
    // cos (x) ~ 1 - --- + --- - --- + --- - ---- + ---- - ----
    //                2!    4!    6!    8!    10!    12!    14!
    //
    //           3                          3
    //          --       4k                --        4k+2
    //   p(x) = >  C  * x   > 0     q(x) = >  C   * x     < 0
    //          --  2k                     --  2k+1
    //          k=0                        k=0
    //
    //                      2
    //   cos(x) ~ [ p(x) + x * q(x) ]
    //

    return EvenPoly(x, cos_arr, COS_ARR_SIZE, status);
}

BX_CPP_INLINE void sincos_invalid(floatx80 *sin_a, floatx80 *cos_a, floatx80 a)
{
    if (sin_a) *sin_a = a;
    if (cos_a) *cos_a = a;
}

BX_CPP_INLINE void sincos_tiny_argument(floatx80 *sin_a, floatx80 *cos_a, floatx80 a)
{
    if (sin_a) *sin_a = a;
    if (cos_a) *cos_a = floatx80_one;
}

static floatx80 sincos_approximation(int neg, float128 r, uint64_t quotient, float_status_t *status)
{
    if (quotient & 0x1) {
        r = poly_cos(r, status);
        neg = 0;
    } else  {
        r = poly_sin(r, status);
    }

    floatx80 result = float128_to_floatx80(r, status);
    if (quotient & 0x2)
        neg = ! neg;

    if (neg)
        floatx80_chs(&result);

    return result;
}

// =================================================
// FSINCOS               Compute sin(x) and cos(x)
// =================================================

//
// Uses the following identities:
// ----------------------------------------------------------
//
//  sin(-x) = -sin(x)
//  cos(-x) =  cos(x)
//
//  sin(x+y) = sin(x)*cos(y)+cos(x)*sin(y)
//  cos(x+y) = sin(x)*sin(y)+cos(x)*cos(y)
//
//  sin(x+ pi/2)  =  cos(x)
//  sin(x+ pi)    = -sin(x)
//  sin(x+3pi/2)  = -cos(x)
//  sin(x+2pi)    =  sin(x)
//

int fsincos(floatx80 a, floatx80 *sin_a, floatx80 *cos_a, float_status_t *status)
{
    uint64_t aSig0, aSig1 = 0;
    int32_t aExp, zExp, expDiff;
    int aSign, zSign;
    int q = 0;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a)) {
        goto invalid;
    }

    aSig0 = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    /* invalid argument */
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig0<<1)) {
            sincos_invalid(sin_a, cos_a, propagateFloatx80NaN(a, status));
            return 0;
        }

    invalid:
        float_raise(status, float_flag_invalid);
        sincos_invalid(sin_a, cos_a, floatx80_default_nan);
        return 0;
    }

    if (aExp == 0) {
        if (aSig0 == 0) {
            sincos_tiny_argument(sin_a, cos_a, a);
            return 0;
        }

        float_raise(status, float_flag_denormal);

        /* handle pseudo denormals */
        if (! (aSig0 & U64(0x8000000000000000)))
        {
            float_raise(status, float_flag_inexact);
            if (sin_a)
                float_raise(status, float_flag_underflow);
            sincos_tiny_argument(sin_a, cos_a, a);
            return 0;
        }

        normalizeFloatx80Subnormal(aSig0, &aExp, &aSig0);
    }

    zSign = aSign;
    zExp = FLOATX80_EXP_BIAS;
    expDiff = aExp - zExp;

    /* argument is out-of-range */
    if (expDiff >= 63)
        return -1;

    float_raise(status, float_flag_inexact);

    if (expDiff < -1) {    // doesn't require reduction
        if (expDiff <= -68) {
            a = packFloatx80(aSign, aExp, aSig0);
            sincos_tiny_argument(sin_a, cos_a, a);
            return 0;
        }
        zExp = aExp;
    }
    else {
        q = reduce_trig_arg(expDiff, &zSign, &aSig0, &aSig1);
    }

    /* **************************** */
    /* argument reduction completed */
    /* **************************** */

    /* using float128 for approximation */
    float128 r = normalizeRoundAndPackFloat128(0, zExp-0x10, aSig0, aSig1, status);

    if (aSign) q = -q;
    if (sin_a) *sin_a = sincos_approximation(zSign, r,   q, status);
    if (cos_a) *cos_a = sincos_approximation(zSign, r, q+1, status);

    return 0;
}

int fsin(floatx80 *a, float_status_t *status)
{
    return fsincos(*a, a, 0, status);
}

int fcos(floatx80 *a, float_status_t *status)
{
    return fsincos(*a, 0, a, status);
}

// =================================================
// FPTAN                 Compute tan(x)
// =================================================

//
// Uses the following identities:
//
// 1. ----------------------------------------------------------
//
//  sin(-x) = -sin(x)
//  cos(-x) =  cos(x)
//
//  sin(x+y) = sin(x)*cos(y)+cos(x)*sin(y)
//  cos(x+y) = sin(x)*sin(y)+cos(x)*cos(y)
//
//  sin(x+ pi/2)  =  cos(x)
//  sin(x+ pi)    = -sin(x)
//  sin(x+3pi/2)  = -cos(x)
//  sin(x+2pi)    =  sin(x)
//
// 2. ----------------------------------------------------------
//
//           sin(x)
//  tan(x) = ------
//           cos(x)
//

int ftan(floatx80 *a_input, float_status_t *status)
{
    floatx80 a = *a_input;
    uint64_t aSig0, aSig1 = 0;
    int32_t aExp, zExp, expDiff;
    int aSign, zSign;
    int q = 0;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a)) {
        goto invalid;
    }

    aSig0 = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    /* invalid argument */
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig0<<1))
        {
            a = propagateFloatx80NaN(a, status);
            *a_input = a;
            return 0;
        }

    invalid:
        float_raise(status, float_flag_invalid);
        a = floatx80_default_nan;
        *a_input = a;
        return 0;
    }

    if (aExp == 0) {
        if (aSig0 == 0) return 0;
        float_raise(status, float_flag_denormal);
        /* handle pseudo denormals */
        if (! (aSig0 & U64(0x8000000000000000)))
        {
            float_raise(status, float_flag_inexact | float_flag_underflow);
            *a_input = a;
            return 0;
        }
        normalizeFloatx80Subnormal(aSig0, &aExp, &aSig0);
    }

    zSign = aSign;
    zExp = FLOATX80_EXP_BIAS;
    expDiff = aExp - zExp;

    /* argument is out-of-range */
    if (expDiff >= 63){
        *a_input = a;
        return -1;
    }

    float_raise(status, float_flag_inexact);

    if (expDiff < -1) {    // doesn't require reduction
        if (expDiff <= -68) {
            a = packFloatx80(aSign, aExp, aSig0);
            *a_input = a;
            return 0;
        }
        zExp = aExp;
    }
    else {
        q = reduce_trig_arg(expDiff, &zSign, &aSig0, &aSig1);
    }

    /* **************************** */
    /* argument reduction completed */
    /* **************************** */

    /* using float128 for approximation */
    float128 r = normalizeRoundAndPackFloat128(0, zExp-0x10, aSig0, aSig1, status);

    float128 sin_r = poly_sin(r, status);
    float128 cos_r = poly_cos(r, status);

    if (q & 0x1) {
        r = float128_div(cos_r, sin_r, status);
        zSign = ! zSign;
    } else {
        r = float128_div(sin_r, cos_r, status);
    }

    a = float128_to_floatx80(r, status);
    if (zSign)
        floatx80_chs(&a);

    *a_input = a;
    return 0;
}

/*============================================================================
This source file is an extension to the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b, written for Bochs (x86 achitecture simulator)
floating point emulation.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

/*============================================================================
 * Written for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#include "softfloat/softfloatx80.h"
#include "softfloat/softfloat-round-pack.h"
#define USE_estimateDiv128To64
#include "softfloat/softfloat-macros.h"

/* executes single exponent reduction cycle */
static uint64_t remainder_kernel(uint64_t aSig0, uint64_t bSig, int expDiff, uint64_t *zSig0, uint64_t *zSig1)
{
    uint64_t term0, term1;
    uint64_t aSig1 = 0;

    shortShift128Left(aSig1, aSig0, expDiff, &aSig1, &aSig0);
    uint64_t q = estimateDiv128To64(aSig1, aSig0, bSig);
    mul64To128(bSig, q, &term0, &term1);
    sub128(aSig1, aSig0, term0, term1, zSig1, zSig0);
    while ((int64_t)(*zSig1) < 0) {
        --q;
        add128(*zSig1, *zSig0, 0, bSig, zSig1, zSig0);
    }
    return q;
}

static int do_fprem(floatx80 a, floatx80 b, floatx80 *r_input, uint64_t *q_input, int rounding_mode, float_status_t *status)
{
    floatx80 r = *r_input;
    uint64_t q = *q_input;
    int32_t aExp, bExp, zExp, expDiff;
    uint64_t aSig0, aSig1, bSig;
    int aSign;
    q = 0;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a) || floatx80_is_unsupported(b))
    {
        float_raise(status, float_flag_invalid);
        r = floatx80_default_nan;
        *r_input = r;
        *q_input = q;
        return -1;
    }

    aSig0 = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig0<<1) || ((bExp == 0x7FFF) && (uint64_t) (bSig<<1))) {
            r = propagateFloatx80NaN_two_args(a, b, status);
            *r_input = r;
            *q_input = q;
            return -1;
        }
        float_raise(status, float_flag_invalid);
        r = floatx80_default_nan;
        *r_input = r;
        *q_input = q;
        return -1;
    }
    if (bExp == 0x7FFF) {
        if ((uint64_t) (bSig<<1)) {
            r = propagateFloatx80NaN_two_args(a, b, status);
            *r_input = r;
            *q_input = q;
            return -1;
        }
        if (aExp == 0 && aSig0) {
            float_raise(status, float_flag_denormal);
            normalizeFloatx80Subnormal(aSig0, &aExp, &aSig0);
            r = (a.fraction & U64(0x8000000000000000)) ?
                    packFloatx80(aSign, aExp, aSig0) : a;
            *r_input = r;
            *q_input = q;
            return 0;
        }
        r = a;
        *r_input = r;
        *q_input = q;
        return 0;

    }
    if (bExp == 0) {
        if (bSig == 0) {
            float_raise(status, float_flag_invalid);
            r = floatx80_default_nan;
            *r_input = r;
            *q_input = q;
            return -1;
        }
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }
    if (aExp == 0) {
        if (aSig0 == 0) {
            r = a;
            *r_input = r;
            *q_input = q;
            return 0;
        }
        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig0, &aExp, &aSig0);
    }
    expDiff = aExp - bExp;
    aSig1 = 0;

    uint32_t overflow = 0;

    if (expDiff >= 64) {
        int n = (expDiff & 0x1f) | 0x20;
        remainder_kernel(aSig0, bSig, n, &aSig0, &aSig1);
        zExp = aExp - n;
        overflow = 1;
    }
    else {
        zExp = bExp;

        if (expDiff < 0) {
            if (expDiff < -1) {
               r = (a.fraction & U64(0x8000000000000000)) ?
                    packFloatx80(aSign, aExp, aSig0) : a;
                *r_input = r;
                *q_input = q;
               return 0;
            }
            shift128Right(aSig0, 0, 1, &aSig0, &aSig1);
            expDiff = 0;
        }

        if (expDiff > 0) {
            q = remainder_kernel(aSig0, bSig, expDiff, &aSig0, &aSig1);
        }
        else {
            if (bSig <= aSig0) {
               aSig0 -= bSig;
               q = 1;
            }
        }

        if (rounding_mode == float_round_nearest_even)
        {
            uint64_t term0, term1;
            shift128Right(bSig, 0, 1, &term0, &term1);

            if (! lt128(aSig0, aSig1, term0, term1))
            {
               int lt = lt128(term0, term1, aSig0, aSig1);
               int eq = eq128(aSig0, aSig1, term0, term1);

               if ((eq && (q & 1)) || lt) {
                  aSign = !aSign;
                  ++q;
               }
               if (lt) sub128(bSig, 0, aSig0, aSig1, &aSig0, &aSig1);
            }
        }
    }

    r = normalizeRoundAndPackFloatx80(80, aSign, zExp, aSig0, aSig1, status);
    *r_input = r;
    *q_input = q;
    return overflow;
}

static int do_fprem(floatx80 a, floatx80 b, floatx80 *r_input, uint64_t *q_input, int rounding_mode, float_status_t *status);

/*----------------------------------------------------------------------------
| Returns the remainder of the extended double-precision floating-point value
| `a' with respect to the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_ieee754_remainder(floatx80 a, floatx80 b, floatx80 *r, uint64_t *q, float_status_t *status)
{
    return do_fprem(a, b, r, q, float_round_nearest_even, status);
}

/*----------------------------------------------------------------------------
| Returns the remainder of the extended double-precision floating-point value
| `a' with  respect to  the corresponding value `b'. Unlike previous function
| the  function  does not compute  the remainder  specified  in  the IEC/IEEE
| Standard  for Binary  Floating-Point  Arithmetic.  This  function  operates
| differently  from the  previous  function in  the way  that it  rounds  the
| quotient of 'a' divided by 'b' to an integer.
*----------------------------------------------------------------------------*/

int floatx80_remainder(floatx80 a, floatx80 b, floatx80 *r, uint64_t *q, float_status_t *status)
{
    return do_fprem(a, b, r, q, float_round_to_zero, status);
}

/*============================================================================
This source file is an extension to the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b, written for Bochs (x86 achitecture simulator)
floating point emulation.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

/*============================================================================
 * Written for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#define FLOAT128

#define FPATAN_ARR_SIZE 11

static const float128 float128_sqrt3 =
        packFloat128Constant(U64(0x3fffbb67ae8584ca), U64(0xa73b25742d7078b8));
static const floatx80 floatx80_pi  =
        packFloatx80Constant(0, 0x4000, U64(0xc90fdaa22168c235));

static const float128 float128_pi2 =
        packFloat128Constant(U64(0x3fff921fb54442d1), U64(0x8469898CC5170416));
static const float128 float128_pi4 =
        packFloat128Constant(U64(0x3ffe921fb54442d1), U64(0x8469898CC5170416));
static const float128 float128_pi6 =
        packFloat128Constant(U64(0x3ffe0c152382d736), U64(0x58465BB32E0F580F));

static float128 atan_arr[FPATAN_ARR_SIZE] =
{
    PACK_FLOAT_128(0x3fff000000000000, 0x0000000000000000), /*  1 */
    PACK_FLOAT_128(0xbffd555555555555, 0x5555555555555555), /*  3 */
    PACK_FLOAT_128(0x3ffc999999999999, 0x999999999999999a), /*  5 */
    PACK_FLOAT_128(0xbffc249249249249, 0x2492492492492492), /*  7 */
    PACK_FLOAT_128(0x3ffbc71c71c71c71, 0xc71c71c71c71c71c), /*  9 */
    PACK_FLOAT_128(0xbffb745d1745d174, 0x5d1745d1745d1746), /* 11 */
    PACK_FLOAT_128(0x3ffb3b13b13b13b1, 0x3b13b13b13b13b14), /* 13 */
    PACK_FLOAT_128(0xbffb111111111111, 0x1111111111111111), /* 15 */
    PACK_FLOAT_128(0x3ffae1e1e1e1e1e1, 0xe1e1e1e1e1e1e1e2), /* 17 */
    PACK_FLOAT_128(0xbffaaf286bca1af2, 0x86bca1af286bca1b), /* 19 */
    PACK_FLOAT_128(0x3ffa861861861861, 0x8618618618618618)  /* 21 */
};

extern float128 OddPoly(float128 x, float128 *arr, int n, float_status_t *status);

/* |x| < 1/4 */
static float128 poly_atan(float128 x1, float_status_t *status)
{
/*
    //                 3     5     7     9     11     13     15     17
    //                x     x     x     x     x      x      x      x
    // atan(x) ~ x - --- + --- - --- + --- - ---- + ---- - ---- + ----
    //                3     5     7     9     11     13     15     17
    //
    //                 2     4     6     8     10     12     14     16
    //                x     x     x     x     x      x      x      x
    //   = x * [ 1 - --- + --- - --- + --- - ---- + ---- - ---- + ---- ]
    //                3     5     7     9     11     13     15     17
    //
    //           5                          5
    //          --       4k                --        4k+2
    //   p(x) = >  C  * x           q(x) = >  C   * x
    //          --  2k                     --  2k+1
    //          k=0                        k=0
    //
    //                            2
    //    atan(x) ~ x * [ p(x) + x * q(x) ]
    //
*/
    return OddPoly(x1, atan_arr, FPATAN_ARR_SIZE, status);
}

// =================================================
// FPATAN                  Compute y * log (x)
//                                        2
// =================================================

//
// Uses the following identities:
//
// 1. ----------------------------------------------------------
//
//   atan(-x) = -atan(x)
//
// 2. ----------------------------------------------------------
//
//                             x + y
//   atan(x) + atan(y) = atan -------, xy < 1
//                             1-xy
//
//                             x + y
//   atan(x) + atan(y) = atan ------- + PI, x > 0, xy > 1
//                             1-xy
//
//                             x + y
//   atan(x) + atan(y) = atan ------- - PI, x < 0, xy > 1
//                             1-xy
//
// 3. ----------------------------------------------------------
//
//   atan(x) = atan(INF) + atan(- 1/x)
//
//                           x-1
//   atan(x) = PI/4 + atan( ----- )
//                           x+1
//
//                           x * sqrt(3) - 1
//   atan(x) = PI/6 + atan( ----------------- )
//                             x + sqrt(3)
//
// 4. ----------------------------------------------------------
//                   3     5     7     9                 2n+1
//                  x     x     x     x              n  x
//   atan(x) = x - --- + --- - --- + --- - ... + (-1)  ------ + ...
//                  3     5     7     9                 2n+1
//

floatx80 fpatan(floatx80 a, floatx80 b, float_status_t *status)
{
    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a) || floatx80_is_unsupported(b)) {
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    uint64_t aSig = extractFloatx80Frac(a);
    int32_t aExp = extractFloatx80Exp(a);
    int aSign = extractFloatx80Sign(a);
    uint64_t bSig = extractFloatx80Frac(b);
    int32_t bExp = extractFloatx80Exp(b);
    int bSign = extractFloatx80Sign(b);

    int zSign = aSign ^ bSign;

    if (bExp == 0x7FFF)
    {
        if ((uint64_t) (bSig<<1))
            return propagateFloatx80NaN_two_args(a, b, status);

        if (aExp == 0x7FFF) {
            if ((uint64_t) (aSig<<1))
                return propagateFloatx80NaN_two_args(a, b, status);

            if (aSign) {   /* return 3PI/4 */
                return roundAndPackFloatx80(80, bSign,
                        FLOATX80_3PI4_EXP, FLOAT_3PI4_HI, FLOAT_3PI4_LO, status);
            }
            else {         /* return  PI/4 */
                return roundAndPackFloatx80(80, bSign,
                        FLOATX80_PI4_EXP, FLOAT_PI_HI, FLOAT_PI_LO, status);
            }
        }

        if (aSig && (aExp == 0))
            float_raise(status, float_flag_denormal);

        /* return PI/2 */
        return roundAndPackFloatx80(80, bSign, FLOATX80_PI2_EXP, FLOAT_PI_HI, FLOAT_PI_LO, status);
    }
    if (aExp == 0x7FFF)
    {
        if ((uint64_t) (aSig<<1))
            return propagateFloatx80NaN_two_args(a, b, status);

        if (bSig && (bExp == 0))
            float_raise(status, float_flag_denormal);

return_PI_or_ZERO:

        if (aSign) {   /* return PI */
            return roundAndPackFloatx80(80, bSign, FLOATX80_PI_EXP, FLOAT_PI_HI, FLOAT_PI_LO, status);
        } else {       /* return  0 */
            return packFloatx80(bSign, 0, 0);
        }
    }
    if (bExp == 0)
    {
        if (bSig == 0) {
             if (aSig && (aExp == 0)) float_raise(status, float_flag_denormal);
             goto return_PI_or_ZERO;
        }

        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }
    if (aExp == 0)
    {
        if (aSig == 0)   /* return PI/2 */
            return roundAndPackFloatx80(80, bSign, FLOATX80_PI2_EXP, FLOAT_PI_HI, FLOAT_PI_LO, status);

        float_raise(status, float_flag_denormal);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    float_raise(status, float_flag_inexact);

    /* |a| = |b| ==> return PI/4 */
    if (aSig == bSig && aExp == bExp)
        return roundAndPackFloatx80(80, bSign, FLOATX80_PI4_EXP, FLOAT_PI_HI, FLOAT_PI_LO, status);

    /* ******************************** */
    /* using float128 for approximation */
    /* ******************************** */

    float128 a128 = normalizeRoundAndPackFloat128(0, aExp-0x10, aSig, 0, status);
    float128 b128 = normalizeRoundAndPackFloat128(0, bExp-0x10, bSig, 0, status);
    float128 x;
    int swap = 0, add_pi6 = 0, add_pi4 = 0;

    if (aExp > bExp || (aExp == bExp && aSig > bSig))
    {
        x = float128_div(b128, a128, status);
    }
    else {
        x = float128_div(a128, b128, status);
        swap = 1;
    }

    int32_t xExp = extractFloat128Exp(x);

    if (xExp <= FLOATX80_EXP_BIAS-40)
        goto approximation_completed;

    if (x.hi >= U64(0x3ffe800000000000))        // 3/4 < x < 1
    {
        /*
        arctan(x) = arctan((x-1)/(x+1)) + pi/4
        */
        float128 t1 = float128_sub(x, float128_one, status);
        float128 t2 = float128_add(x, float128_one, status);
        x = float128_div(t1, t2, status);
        add_pi4 = 1;
    }
    else
    {
        /* argument correction */
        if (xExp >= 0x3FFD)                     // 1/4 < x < 3/4
        {
            /*
            arctan(x) = arctan((x*sqrt(3)-1)/(x+sqrt(3))) + pi/6
            */
            float128 t1 = float128_mul(x, float128_sqrt3, status);
            float128 t2 = float128_add(x, float128_sqrt3, status);
            x = float128_sub(t1, float128_one, status);
            x = float128_div(x, t2, status);
            add_pi6 = 1;
        }
    }

    x = poly_atan(x, status);
    if (add_pi6) x = float128_add(x, float128_pi6, status);
    if (add_pi4) x = float128_add(x, float128_pi4, status);

approximation_completed:
    if (swap) x = float128_sub(float128_pi2, x, status);
    floatx80 result = float128_to_floatx80(x, status);
    if (zSign) floatx80_chs(&result);
    int rSign = extractFloatx80Sign(result);
    if (!bSign && rSign)
        return floatx80_add(result, floatx80_pi, status);
    if (bSign && !rSign)
        return floatx80_sub(result, floatx80_pi, status);
    return result;
}

/*============================================================================
This source file is an extension to the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2b, written for Bochs (x86 achitecture simulator)
floating point emulation.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) the source code for the derivative work includes prominent notice that
the work is derivative, and (2) the source code includes prominent notice with
these four paragraphs for those parts of this code that are retained.
=============================================================================*/

/*============================================================================
 * Written for Bochs (x86 achitecture simulator) by
 *            Stanislav Shwartsman [sshwarts at sourceforge net]
 * ==========================================================================*/

#define FLOAT128

static const floatx80 floatx80_negone  = packFloatx80Constant(1, 0x3fff, U64(0x8000000000000000));
static const floatx80 floatx80_neghalf = packFloatx80Constant(1, 0x3ffe, U64(0x8000000000000000));
static const float128 float128_ln2     =
    packFloat128Constant(U64(0x3ffe62e42fefa39e), U64(0xf35793c7673007e6));

#ifdef BETTER_THAN_PENTIUM

#define LN2_SIG_HI U64(0xb17217f7d1cf79ab)
#define LN2_SIG_LO U64(0xc9e3b39800000000)  /* 96 bit precision */

#else

#define LN2_SIG_HI U64(0xb17217f7d1cf79ab)
#define LN2_SIG_LO U64(0xc000000000000000)  /* 67-bit precision */

#endif

#define EXP_ARR_SIZE 15

static float128 exp_arr[EXP_ARR_SIZE] =
{
    PACK_FLOAT_128(0x3fff000000000000, 0x0000000000000000), /*  1 */
    PACK_FLOAT_128(0x3ffe000000000000, 0x0000000000000000), /*  2 */
    PACK_FLOAT_128(0x3ffc555555555555, 0x5555555555555555), /*  3 */
    PACK_FLOAT_128(0x3ffa555555555555, 0x5555555555555555), /*  4 */
    PACK_FLOAT_128(0x3ff8111111111111, 0x1111111111111111), /*  5 */
    PACK_FLOAT_128(0x3ff56c16c16c16c1, 0x6c16c16c16c16c17), /*  6 */
    PACK_FLOAT_128(0x3ff2a01a01a01a01, 0xa01a01a01a01a01a), /*  7 */
    PACK_FLOAT_128(0x3fefa01a01a01a01, 0xa01a01a01a01a01a), /*  8 */
    PACK_FLOAT_128(0x3fec71de3a556c73, 0x38faac1c88e50017), /*  9 */
    PACK_FLOAT_128(0x3fe927e4fb7789f5, 0xc72ef016d3ea6679), /* 10 */
    PACK_FLOAT_128(0x3fe5ae64567f544e, 0x38fe747e4b837dc7), /* 11 */
    PACK_FLOAT_128(0x3fe21eed8eff8d89, 0x7b544da987acfe85), /* 12 */
    PACK_FLOAT_128(0x3fde6124613a86d0, 0x97ca38331d23af68), /* 13 */
    PACK_FLOAT_128(0x3fda93974a8c07c9, 0xd20badf145dfa3e5), /* 14 */
    PACK_FLOAT_128(0x3fd6ae7f3e733b81, 0xf11d8656b0ee8cb0)  /* 15 */
};

extern float128 EvalPoly(float128 x, float128 *arr, int n, float_status_t *status);

/* required -1 < x < 1 */
static float128 poly_exp(float128 x, float_status_t *status)
{
/*
    //               2     3     4     5     6     7     8     9
    //  x           x     x     x     x     x     x     x     x
    // e - 1 ~ x + --- + --- + --- + --- + --- + --- + --- + --- + ...
    //              2!    3!    4!    5!    6!    7!    8!    9!
    //
    //                     2     3     4     5     6     7     8
    //              x     x     x     x     x     x     x     x
    //   = x [ 1 + --- + --- + --- + --- + --- + --- + --- + --- + ... ]
    //              2!    3!    4!    5!    6!    7!    8!    9!
    //
    //           8                          8
    //          --       2k                --        2k+1
    //   p(x) = >  C  * x           q(x) = >  C   * x
    //          --  2k                     --  2k+1
    //          k=0                        k=0
    //
    //    x
    //   e  - 1 ~ x * [ p(x) + x * q(x) ]
    //
*/
    float128 t = EvalPoly(x, exp_arr, EXP_ARR_SIZE, status);
    return float128_mul(t, x, status);
}

// =================================================
//                                  x
// FX2M1                   Compute 2  - 1
// =================================================

//
// Uses the following identities:
//
// 1. ----------------------------------------------------------
//      x    x*ln(2)
//     2  = e
//
// 2. ----------------------------------------------------------
//                      2     3     4     5           n
//      x        x     x     x     x     x           x
//     e  = 1 + --- + --- + --- + --- + --- + ... + --- + ...
//               1!    2!    3!    4!    5!          n!
//

floatx80 f2xm1(floatx80 a, float_status_t *status)
{
    uint64_t zSig0, zSig1, zSig2;

    // handle unsupported extended double-precision floating encodings
    if (floatx80_is_unsupported(a))
    {
        float_raise(status, float_flag_invalid);
        return floatx80_default_nan;
    }

    uint64_t aSig = extractFloatx80Frac(a);
    int32_t aExp = extractFloatx80Exp(a);
    int aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1))
            return propagateFloatx80NaN(a, status);

        return (aSign) ? floatx80_negone : a;
    }

    if (aExp == 0) {
        if (aSig == 0) return a;
        float_raise(status, float_flag_denormal | float_flag_inexact);
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);

    tiny_argument:
        mul128By64To192(LN2_SIG_HI, LN2_SIG_LO, aSig, &zSig0, &zSig1, &zSig2);
        if (0 < (int64_t) zSig0) {
            shortShift128Left(zSig0, zSig1, 1, &zSig0, &zSig1);
            --aExp;
        }
        return
            roundAndPackFloatx80(80, aSign, aExp, zSig0, zSig1, status);
    }

    float_raise(status, float_flag_inexact);

    if (aExp < 0x3FFF)
    {
        if (aExp < FLOATX80_EXP_BIAS-68)
            goto tiny_argument;

        /* ******************************** */
        /* using float128 for approximation */
        /* ******************************** */

        float128 x = floatx80_to_float128(a, status);
        x = float128_mul(x, float128_ln2, status);
        x = poly_exp(x, status);
        return float128_to_floatx80(x, status);
    }
    else
    {
        if (a.exp == 0xBFFF && ! (aSig<<1))
           return floatx80_neghalf;

        return a;
    }
}