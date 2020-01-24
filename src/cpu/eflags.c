// Methods to compute various flags values (OSZAPC)
// Note that cpu.eflags is only reliable for non-OSZAPC values like direction flag and interrupt flag.
// Various instructions set/clear values OSZAPC bits in cpu.eflags, but they may or may not be accurate.
// The only way to derive the "true" value of EFLAGS is by calculating each individual bit using the cpu.l* fields
// Convieniently, this is what this file does.

// Assumptions made here:
//  - All modifiactions to cpu.lr will come alongside a modification of cpu.laux.
//  - Every time cpu.lr/aux is modified, every flag is being modified.

#include "cpu/cpu.h"
#include "cpu/opcodes.h"

// Sign flag -- set when result has the sign bit set
int cpu_get_sf(void)
{
    return (cpu.lr ^ cpu.laux) >> 31;
}

void cpu_set_sf(int set)
{
    // Determine the current value of SF and modify cpu.laux accordingly.
    // Top-most bit of cpu.laux is set to XOR of current sf and new sf
    cpu.laux = (cpu.laux & ~0x80000000) | ((cpu.lr ^ (set << 31)) & 0x80000000);
}

// Parity flag -- set when the low 8 bits of the result have an even number of set/unset bits
int cpu_get_pf(void)
{
    uint32_t v = (cpu.lr ^ (cpu.laux & 0x80)) & 0xFF;
    v ^= v >> 4;
    v &= 0x0F;
    return (0x9669 >> v) & 1;
}

void cpu_set_pf(int set)
{
    // Note: n and (n ^ 0x80) cause the parity to be opposite
    cpu.laux = (cpu.laux & ~0x80) | (cpu_get_pf() ^ set) << 7;
}

// Get the status bits
static inline int cpu_get_oac(void)
{
    uint32_t eflags = (cpu_get_of() * EFLAGS_OF) | (cpu_get_af() * EFLAGS_AF) | (cpu_get_cf() * EFLAGS_CF);
    cpu.eflags = (cpu.eflags & ~(EFLAGS_OF | EFLAGS_AF | EFLAGS_CF)) | (eflags & (EFLAGS_OF | EFLAGS_AF | EFLAGS_CF));
    return eflags;
}

// Overflow flag -- usually set when an arith operation overflows, but can be set for other purposes
int cpu_get_of(void)
{
    uint32_t lop1;
    switch (cpu.laux & LAUX_METHOD_MASK) {
    case MUL:
        return cpu.lop1 != cpu.lop2;
    case BIT:
    case SAR8... SAR32:
        return 0;
    case ADD8:
        lop1 = cpu.lr - cpu.lop2;
        return ((lop1 ^ cpu.lop2 ^ 0xFF) & (cpu.lop2 ^ cpu.lr)) >> 7 & 1;
    case ADD16:
        lop1 = cpu.lr - cpu.lop2;
        return ((lop1 ^ cpu.lop2 ^ 0xFFFF) & (cpu.lop2 ^ cpu.lr)) >> 15 & 1;
    case ADD32:
        lop1 = cpu.lr - cpu.lop2;
        return ((lop1 ^ cpu.lop2 ^ 0xFFFFFFFF) & (cpu.lop2 ^ cpu.lr)) >> 31 & 1;
    case SUB8:
        lop1 = cpu.lop2 + cpu.lr;
        return ((lop1 ^ cpu.lop2) & (lop1 ^ cpu.lr)) >> 7 & 1;
    case SUB16:
        lop1 = cpu.lop2 + cpu.lr;
        return ((lop1 ^ cpu.lop2) & (lop1 ^ cpu.lr)) >> 15 & 1;
    case SUB32:
        lop1 = cpu.lop2 + cpu.lr;
        return ((lop1 ^ cpu.lop2) & (lop1 ^ cpu.lr)) >> 31 & 1;
    case ADC8:
        return ((cpu.lop1 ^ cpu.lr) & (cpu.lop2 ^ cpu.lr)) >> 7 & 1;
    case ADC16:
        return ((cpu.lop1 ^ cpu.lr) & (cpu.lop2 ^ cpu.lr)) >> 15 & 1;
    case ADC32:
        return ((cpu.lop1 ^ cpu.lr) & (cpu.lop2 ^ cpu.lr)) >> 31 & 1;
    case SBB8:
        return ((cpu.lr ^ cpu.lop1) & (cpu.lop2 ^ cpu.lop1)) >> 7 & 1;
    case SBB16:
        return ((cpu.lr ^ cpu.lop1) & (cpu.lop2 ^ cpu.lop1)) >> 15 & 1;
    case SBB32:
        return ((cpu.lr ^ cpu.lop1) & (cpu.lop2 ^ cpu.lop1)) >> 31 & 1;
    case SHL8:
        return ((cpu.lr >> 7) ^ (cpu.lop1 >> (8 - cpu.lop2))) & 1;
    case SHL16:
        return ((cpu.lr >> 15) ^ (cpu.lop1 >> (16 - cpu.lop2))) & 1;
    case SHL32:
        return ((cpu.lr >> 31) ^ (cpu.lop1 >> (32 - cpu.lop2))) & 1;
    case SHR8:
        return (cpu.lr << 1 ^ cpu.lr) >> 7 & 1;
    case SHR16:
        return (cpu.lr << 1 ^ cpu.lr) >> 15 & 1;
    case SHR32:
        return (cpu.lr << 1 ^ cpu.lr) >> 31 & 1;
    case SHLD16:
        return cpu_get_cf() ^ (cpu.lr >> 15 & 1);
    case SHLD32:
        return cpu_get_cf() ^ (cpu.lr >> 31 & 1);
    case SHRD16:
        return (cpu.lr << 1 ^ cpu.lr) >> 15 & 1;
    case SHRD32:
        return (cpu.lr << 1 ^ cpu.lr) >> 31 & 1;
    case INC8:
        return (cpu.lr & 0xFF) == 0x80;
    case INC16:
        return (cpu.lr & 0xFFFF) == 0x8000;
    case INC32:
        return cpu.lr == 0x80000000;
    case DEC8:
        return (cpu.lr & 0xFF) == 0x7F;
    case DEC16:
        return (cpu.lr & 0xFFFF) == 0x7FFF;
    case DEC32:
        return cpu.lr == 0x7FFFFFFF;
    case EFLAGS_FULL_UPDATE:
        return cpu.eflags >> 11 & 1;
    default:
        CPU_FATAL("Unknown of op: %d\n", cpu.laux & LAUX_METHOD_MASK);
    }
}

void cpu_set_of(int set)
{
    cpu_get_oac();
    cpu.eflags &= ~EFLAGS_OF;
    cpu.eflags |= set * EFLAGS_OF;
    cpu.laux = (cpu.laux & ~LAUX_METHOD_MASK) | EFLAGS_FULL_UPDATE;
}

// Auxiliary Carry Flag -- If the lower 4 bits of the result carried out
int cpu_get_af(void)
{
    uint32_t lop1;
    switch (cpu.laux & LAUX_METHOD_MASK) {
    case BIT:
    case MUL:
    case SHL8... SHL32:
    case SHR8... SHR32:
    case SHLD16... SHLD32:
    case SHRD16... SHRD32:
        return 0;
    case SAR8... SAR32:
#if 0
        return cpu.lr & 1;
#else
        return 0;
#endif
    case ADD8... ADD32:
        lop1 = cpu.lr - cpu.lop2;
        return (lop1 ^ cpu.lop2 ^ cpu.lr) >> 4 & 1;
    case SUB8... SUB32:
        lop1 = cpu.lr + cpu.lop2;
        return (lop1 ^ cpu.lop2 ^ cpu.lr) >> 4 & 1;
    case ADC8... ADC32:
    case SBB8... SBB32:
        return (cpu.lop1 ^ cpu.lop2 ^ cpu.lr) >> 4 & 1;
    case INC8... INC32:
        return (cpu.lr & 15) == 0;
    case DEC8... DEC32:
        return (cpu.lr & 15) == 15;
    case EFLAGS_FULL_UPDATE:
        return cpu.eflags >> 4 & 1;
    default:
        CPU_FATAL("Unknown af op: %d\n", cpu.laux & LAUX_METHOD_MASK);
    }
}
void cpu_set_af(int set)
{
    cpu_get_oac();
    cpu.eflags &= ~EFLAGS_AF;
    cpu.eflags |= set * EFLAGS_AF;
    cpu.laux = (cpu.laux & ~LAUX_METHOD_MASK) | EFLAGS_FULL_UPDATE;
}

// Carry Flag -- If the result overflowed, or a way to indicate error on the BIOS/DOS
int cpu_get_cf(void)
{
    uint32_t lop1;
    switch (cpu.laux & LAUX_METHOD_MASK) {
    case MUL:
        return cpu.lop1 != cpu.lop2;
    case ADD8:
        return (cpu.lr & 0xFF) < (cpu.lop2 & 0xFF);
    case ADD16:
        return (cpu.lr & 0xFFFF) < (cpu.lop2 & 0xFFFF);
    case ADD32:
        return cpu.lr < cpu.lop2;
    case SUB8: // a - b = c --> a = b + c
        lop1 = cpu.lop2 + cpu.lr;
        return cpu.lop2 > (lop1 & 0xFF);
    case SUB16:
        lop1 = cpu.lop2 + cpu.lr;
        return cpu.lop2 > (lop1 & 0xFFFF);
    case SUB32:
        lop1 = cpu.lop2 + cpu.lr;
        return cpu.lop2 > lop1;
    case ADC8:
        return (cpu.lop1 ^ ((cpu.lop1 ^ cpu.lop2) & (cpu.lop2 ^ cpu.lr))) >> 7 & 1;
    case ADC16:
        return (cpu.lop1 ^ ((cpu.lop1 ^ cpu.lop2) & (cpu.lop2 ^ cpu.lr))) >> 15 & 1;
    case ADC32:
        return (cpu.lop1 ^ ((cpu.lop1 ^ cpu.lop2) & (cpu.lop2 ^ cpu.lr))) >> 31 & 1;
    case SBB8:
        return (cpu.lr ^ ((cpu.lr ^ cpu.lop2) & (cpu.lop1 ^ cpu.lop2))) >> 7 & 1;
    case SBB16:
        return (cpu.lr ^ ((cpu.lr ^ cpu.lop2) & (cpu.lop1 ^ cpu.lop2))) >> 15 & 1;
    case SBB32:
        return (cpu.lr ^ ((cpu.lr ^ cpu.lop2) & (cpu.lop1 ^ cpu.lop2))) >> 31 & 1;
    case SHR8:
    case SAR8:
    case SHR16:
    case SAR16:
    case SHR32:
    case SAR32:
        return cpu.lop1 >> (cpu.lop2 - 1) & 1;
    case SHL8:
        return (cpu.lop1 >> (8 - cpu.lop2)) & 1;
    case SHL16:
        return (cpu.lop1 >> (16 - cpu.lop2)) & 1;
    case SHL32:
        return (cpu.lop1 >> (32 - cpu.lop2)) & 1;
    case SHLD16:
        if (cpu.lop2 <= 16)
            return cpu.lop1 >> (16 - cpu.lop2) & 1;
        return cpu.lop1 >> (32 - cpu.lop2) & 1;
    case SHLD32:
        return cpu.lop1 >> (32 - cpu.lop2) & 1;
    case SHRD16:
    case SHRD32:
        return cpu.lop1 >> (cpu.lop2 - 1) & 1;
    case INC8... INC32:
    case DEC8... DEC32:
    case EFLAGS_FULL_UPDATE:
        return cpu.eflags & 1;
    case BIT:
        return 0;
    default:
        CPU_FATAL("Unknown cf op: %d\n", cpu.laux & LAUX_METHOD_MASK);
    }
}
void cpu_set_cf(int set)
{
    cpu_get_oac();
    cpu.eflags &= ~EFLAGS_CF;
    cpu.eflags |= (set * EFLAGS_CF);
    cpu.laux = (cpu.laux & ~LAUX_METHOD_MASK) | EFLAGS_FULL_UPDATE;
}

void cpu_set_zf(int set)
{
    // More accurate, but slower:
    cpu_set_eflags((cpu_get_eflags() & ~EFLAGS_ZF) | (set * EFLAGS_ZF));
}

// Get the "real" value of eflags
uint32_t cpu_get_eflags(void)
{
    uint32_t eflags = cpu.eflags & ~arith_flag_mask;
    eflags |= cpu_get_of() * EFLAGS_OF;
    eflags |= cpu_get_sf() * EFLAGS_SF;
    eflags |= cpu_get_zf() * EFLAGS_ZF;
    eflags |= cpu_get_af() * EFLAGS_AF;
    eflags |= cpu_get_pf() * EFLAGS_PF;
    eflags |= cpu_get_cf() * EFLAGS_CF;
    return eflags;
}

// Selects between one of the 16 conditions
int cpu_cond(int val)
{
    int cond;
    switch (val >> 1 & 7) {
    case 0:
        cond = cpu_get_of();
        break;
    case 1:
        cond = cpu_get_cf();
        break;
    case 2:
        cond = cpu_get_zf();
        break;
    case 3:
        cond = cpu_get_zf() || cpu_get_cf();
        break;
    case 4:
        cond = cpu_get_sf();
        break;
    case 5:
        cond = cpu_get_pf();
        break;
    case 6:
        cond = cpu_get_sf() != cpu_get_of();
        break;
    case 7:
        cond = cpu_get_zf() || (cpu_get_sf() != cpu_get_of());
        break;
    }
    return cond ^ (val & 1);
}

// Sets te value of eflags. It only masks out reserved/undefined bits
void cpu_set_eflags(uint32_t eflags)
{
    int old_eflags = cpu.eflags;
    cpu.eflags = (cpu.eflags & ~valid_flag_mask) | (eflags & valid_flag_mask);
    cpu.lr = !(eflags & EFLAGS_ZF);
    int pf = cpu.eflags >> 2 & 1, sf = eflags & EFLAGS_SF;

    // SF is always zero by this point, and PF matches ZF. Set LAUX bits accordingly
    cpu.laux = EFLAGS_FULL_UPDATE | // We have updated all OAC flags...
        (sf << 24) | // Since SF is zero, simply set laux fudge bit to whatever sf is
        ((pf ^ cpu.lr ^ 1) << 7); // Determine parity and XOR it with pairty. Store it in bit 7

    // If IF has been modified, then request a fast exit
    if ((old_eflags ^ cpu.eflags) & EFLAGS_IF)
        INTERNAL_CPU_LOOP_EXIT();
}