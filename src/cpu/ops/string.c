// String operations. Most of them are automatically generated 

#include "cpu/cpu.h"
#include "cpu/opcodes.h"
#include "cpu/ops.h"
#define repz_or_repnz(flags) (flags & (I_PREFIX_REPZ | I_PREFIX_REPNZ))
#define EXCEPTION_HANDLER return -1 // Note: -1, not 1 like most other exception handlers
#define MAX_CYCLES_TO_RUN 65536

// <<< BEGIN AUTOGENERATE "ops" >>>
int movsb16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, src, ds_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read8(ds_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_write8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[SI] += add;
        cpu.reg16[DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read8(ds_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_write8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[SI] += add;
        cpu.reg16[DI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int movsb32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, src, ds_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read8(ds_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_write8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[ESI] += add;
        cpu.reg32[EDI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read8(ds_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_write8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[ESI] += add;
        cpu.reg32[EDI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int movsw16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, src, ds_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read16(ds_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_write16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[SI] += add;
        cpu.reg16[DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read16(ds_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_write16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[SI] += add;
        cpu.reg16[DI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int movsw32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, src, ds_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read16(ds_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_write16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[ESI] += add;
        cpu.reg32[EDI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read16(ds_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_write16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[ESI] += add;
        cpu.reg32[EDI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int movsd16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, src, ds_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read32(ds_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_write32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[SI] += add;
        cpu.reg16[DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read32(ds_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_write32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[SI] += add;
        cpu.reg16[DI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int movsd32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, src, ds_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read32(ds_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_write32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[ESI] += add;
        cpu.reg32[EDI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read32(ds_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_write32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[ESI] += add;
        cpu.reg32[EDI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int stosb16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, src = cpu.reg8[AL];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_write8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_write8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int stosb32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, src = cpu.reg8[AL];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_write8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_write8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int stosw16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, src = cpu.reg16[AX];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_write16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_write16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int stosw32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, src = cpu.reg16[AX];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_write16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_write16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int stosd16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, src = cpu.reg32[EAX];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_write32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_write32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int stosd32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, src = cpu.reg32[EAX];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_write32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_write32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int scasb16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -1 : 1;
    uint8_t dest = cpu.reg8[AL], src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
        cpu.reg16[DI] += add;
        cpu.lr = (int8_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB8;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int8_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB8;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg16[CX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int8_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB8;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg16[CX] != 0;
    }
    CPU_FATAL("unreachable");
}
int scasb32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -1 : 1;
    uint8_t dest = cpu.reg8[AL], src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
        cpu.reg32[EDI] += add;
        cpu.lr = (int8_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB8;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int8_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB8;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int8_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB8;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
    }
    CPU_FATAL("unreachable");
}
int scasw16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -2 : 2;
    uint16_t dest = cpu.reg16[AX], src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
        cpu.reg16[DI] += add;
        cpu.lr = (int16_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB16;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int16_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB16;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg16[CX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int16_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB16;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg16[CX] != 0;
    }
    CPU_FATAL("unreachable");
}
int scasw32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -2 : 2;
    uint16_t dest = cpu.reg16[AX], src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
        cpu.reg32[EDI] += add;
        cpu.lr = (int16_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB16;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int16_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB16;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int16_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB16;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
    }
    CPU_FATAL("unreachable");
}
int scasd16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -4 : 4;
    uint32_t dest = cpu.reg32[EAX], src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
        cpu.reg16[DI] += add;
        cpu.lr = (int32_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB32;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int32_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB32;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg16[CX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int32_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB32;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg16[CX] != 0;
    }
    CPU_FATAL("unreachable");
}
int scasd32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -4 : 4;
    uint32_t dest = cpu.reg32[EAX], src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
        cpu.reg32[EDI] += add;
        cpu.lr = (int32_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB32;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int32_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB32;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int32_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB32;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
    }
    CPU_FATAL("unreachable");
}
int insb16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 8 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        src = cpu_inb(cpu.reg16[DX]);
        cpu_write8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        src = cpu_inb(cpu.reg16[DX]);
        cpu_write8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int insb32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 8 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        src = cpu_inb(cpu.reg16[DX]);
        cpu_write8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        src = cpu_inb(cpu.reg16[DX]);
        cpu_write8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int insw16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 16 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        src = cpu_inw(cpu.reg16[DX]);
        cpu_write16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        src = cpu_inw(cpu.reg16[DX]);
        cpu_write16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int insw32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 16 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        src = cpu_inw(cpu.reg16[DX]);
        cpu_write16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        src = cpu_inw(cpu.reg16[DX]);
        cpu_write16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int insd16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 32 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        src = cpu_ind(cpu.reg16[DX]);
        cpu_write32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        src = cpu_ind(cpu.reg16[DX]);
        cpu_write32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_write);
        cpu.reg16[DI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int insd32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 32 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        src = cpu_ind(cpu.reg16[DX]);
        cpu_write32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        src = cpu_ind(cpu.reg16[DX]);
        cpu_write32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_write);
        cpu.reg32[EDI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int outsb16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, src, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 8 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        cpu_read8(seg_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_outb(cpu.reg16[DX], src);
        cpu.reg16[SI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read8(seg_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_outb(cpu.reg16[DX], src);
        cpu.reg16[SI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int outsb32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, src, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 8 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        cpu_read8(seg_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_outb(cpu.reg16[DX], src);
        cpu.reg32[ESI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read8(seg_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_outb(cpu.reg16[DX], src);
        cpu.reg32[ESI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int outsw16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, src, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 16 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        cpu_read16(seg_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_outw(cpu.reg16[DX], src);
        cpu.reg16[SI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read16(seg_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_outw(cpu.reg16[DX], src);
        cpu.reg16[SI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int outsw32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, src, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 16 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        cpu_read16(seg_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_outw(cpu.reg16[DX], src);
        cpu.reg32[ESI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read16(seg_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_outw(cpu.reg16[DX], src);
        cpu.reg32[ESI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int outsd16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, src, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 32 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        cpu_read32(seg_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_outd(cpu.reg16[DX], src);
        cpu.reg16[SI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read32(seg_base + cpu.reg16[SI], src, cpu.tlb_shift_read);
        cpu_outd(cpu.reg16[DX], src);
        cpu.reg16[SI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int outsd32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, src, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], 32 >> 3)) return -1;
    if(!repz_or_repnz(flags)){
        cpu_read32(seg_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_outd(cpu.reg16[DX], src);
        cpu.reg32[ESI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read32(seg_base + cpu.reg32[ESI], src, cpu.tlb_shift_read);
        cpu_outd(cpu.reg16[DX], src);
        cpu.reg32[ESI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int cmpsb16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    uint8_t dest, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read8(seg_base + cpu.reg16[SI], dest, cpu.tlb_shift_read);
        cpu_read8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
        cpu.reg16[DI] += add;
        cpu.reg16[SI] += add;
        cpu.lr = (int8_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB8;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read8(seg_base + cpu.reg16[SI], dest, cpu.tlb_shift_read);
            cpu_read8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[SI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int8_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB8;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg16[CX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read8(seg_base + cpu.reg16[SI], dest, cpu.tlb_shift_read);
            cpu_read8(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[SI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int8_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB8;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg16[CX] != 0;
    }
    CPU_FATAL("unreachable");
}
int cmpsb32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    uint8_t dest, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read8(seg_base + cpu.reg32[ESI], dest, cpu.tlb_shift_read);
        cpu_read8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
        cpu.reg32[EDI] += add;
        cpu.reg32[ESI] += add;
        cpu.lr = (int8_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB8;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read8(seg_base + cpu.reg32[ESI], dest, cpu.tlb_shift_read);
            cpu_read8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ESI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int8_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB8;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read8(seg_base + cpu.reg32[ESI], dest, cpu.tlb_shift_read);
            cpu_read8(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ESI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int8_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB8;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
    }
    CPU_FATAL("unreachable");
}
int cmpsw16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    uint16_t dest, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read16(seg_base + cpu.reg16[SI], dest, cpu.tlb_shift_read);
        cpu_read16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
        cpu.reg16[DI] += add;
        cpu.reg16[SI] += add;
        cpu.lr = (int16_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB16;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read16(seg_base + cpu.reg16[SI], dest, cpu.tlb_shift_read);
            cpu_read16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[SI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int16_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB16;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg16[CX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read16(seg_base + cpu.reg16[SI], dest, cpu.tlb_shift_read);
            cpu_read16(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[SI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int16_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB16;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg16[CX] != 0;
    }
    CPU_FATAL("unreachable");
}
int cmpsw32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    uint16_t dest, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read16(seg_base + cpu.reg32[ESI], dest, cpu.tlb_shift_read);
        cpu_read16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
        cpu.reg32[EDI] += add;
        cpu.reg32[ESI] += add;
        cpu.lr = (int16_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB16;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read16(seg_base + cpu.reg32[ESI], dest, cpu.tlb_shift_read);
            cpu_read16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ESI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int16_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB16;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read16(seg_base + cpu.reg32[ESI], dest, cpu.tlb_shift_read);
            cpu_read16(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ESI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int16_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB16;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
    }
    CPU_FATAL("unreachable");
}
int cmpsd16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    uint32_t dest, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read32(seg_base + cpu.reg16[SI], dest, cpu.tlb_shift_read);
        cpu_read32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
        cpu.reg16[DI] += add;
        cpu.reg16[SI] += add;
        cpu.lr = (int32_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB32;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read32(seg_base + cpu.reg16[SI], dest, cpu.tlb_shift_read);
            cpu_read32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[SI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int32_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB32;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg16[CX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read32(seg_base + cpu.reg16[SI], dest, cpu.tlb_shift_read);
            cpu_read32(cpu.seg_base[ES] + cpu.reg16[DI], src, cpu.tlb_shift_read);
            cpu.reg16[DI] += add;
            cpu.reg16[SI] += add;
            cpu.reg16[CX]--;
            // XXX don't set this every time
            cpu.lr = (int32_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB32;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg16[CX] != 0;
    }
    CPU_FATAL("unreachable");
}
int cmpsd32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    uint32_t dest, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
        cpu_read32(seg_base + cpu.reg32[ESI], dest, cpu.tlb_shift_read);
        cpu_read32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
        cpu.reg32[EDI] += add;
        cpu.reg32[ESI] += add;
        cpu.lr = (int32_t)(dest - src);
        cpu.lop2 = src;
        cpu.laux = SUB32;
        return 0;
        case 1: // REPZ
        for (int i = 0; i < count; i++) {
            cpu_read32(seg_base + cpu.reg32[ESI], dest, cpu.tlb_shift_read);
            cpu_read32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ESI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int32_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB32;
            //cpu.cycles_to_run--;
            if(src != dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
        case 2: // REPNZ
        for (int i = 0; i < count; i++) {
            cpu_read32(seg_base + cpu.reg32[ESI], dest, cpu.tlb_shift_read);
            cpu_read32(cpu.seg_base[ES] + cpu.reg32[EDI], src, cpu.tlb_shift_read);
            cpu.reg32[EDI] += add;
            cpu.reg32[ESI] += add;
            cpu.reg32[ECX]--;
            // XXX don't set this every time
            cpu.lr = (int32_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB32;
            //cpu.cycles_to_run--;
            if(src == dest) return 0;
        }
        return cpu.reg32[ECX] != 0;
    }
    CPU_FATAL("unreachable");
}
int lodsb16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read8(seg_base + cpu.reg16[SI], cpu.reg8[AL], cpu.tlb_shift_read);
        cpu.reg16[SI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read8(seg_base + cpu.reg16[SI], cpu.reg8[AL], cpu.tlb_shift_read);
        cpu.reg16[SI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int lodsb32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -1 : 1, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read8(seg_base + cpu.reg32[ESI], cpu.reg8[AL], cpu.tlb_shift_read);
        cpu.reg32[ESI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read8(seg_base + cpu.reg32[ESI], cpu.reg8[AL], cpu.tlb_shift_read);
        cpu.reg32[ESI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int lodsw16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read16(seg_base + cpu.reg16[SI], cpu.reg16[AX], cpu.tlb_shift_read);
        cpu.reg16[SI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read16(seg_base + cpu.reg16[SI], cpu.reg16[AX], cpu.tlb_shift_read);
        cpu.reg16[SI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int lodsw32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -2 : 2, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read16(seg_base + cpu.reg32[ESI], cpu.reg16[AX], cpu.tlb_shift_read);
        cpu.reg32[ESI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read16(seg_base + cpu.reg32[ESI], cpu.reg16[AX], cpu.tlb_shift_read);
        cpu.reg32[ESI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}
int lodsd16(int flags)
{
    int count = cpu.reg16[CX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read32(seg_base + cpu.reg16[SI], cpu.reg32[EAX], cpu.tlb_shift_read);
        cpu.reg16[SI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read32(seg_base + cpu.reg16[SI], cpu.reg32[EAX], cpu.tlb_shift_read);
        cpu.reg16[SI] += add;
        cpu.reg16[CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg16[CX] != 0;
}
int lodsd32(int flags)
{
    int count = cpu.reg32[ECX], add = cpu.eflags & EFLAGS_DF ? -4 : 4, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
    count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read32(seg_base + cpu.reg32[ESI], cpu.reg32[EAX], cpu.tlb_shift_read);
        cpu.reg32[ESI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read32(seg_base + cpu.reg32[ESI], cpu.reg32[EAX], cpu.tlb_shift_read);
        cpu.reg32[ESI] += add;
        cpu.reg32[ECX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg32[ECX] != 0;
}

// <<< END AUTOGENERATE "ops" >>>