// Segment handlers
#include "cpu/cpu.h"

#define EXCEPTION_HANDLER return 1

static void reload_cs_base(void)
{
    uint32_t virt_eip = VIRT_EIP(), lin_eip = virt_eip + cpu.seg_base[CS];
    // Calculate physical EIP
    // Refresh cpu.last_phys_eip
    uint32_t lin_page = lin_eip >> 12,
             shift = cpu.tlb_shift_read,
             tag = cpu.tlb_tags[lin_eip >> 12] >> shift;

    if (tag & 2) {
        // Not translated yet - let cpu_get_trace handle this
        cpu.last_phys_eip = cpu.phys_eip + 0x1000; // Make this value invalid
        return;
    }

    // Recompute the physical EIP state
    cpu.phys_eip = PTR_TO_PHYS(cpu.tlb[lin_page] + lin_eip);
    cpu.last_phys_eip = cpu.phys_eip & ~0xFFF;
    cpu.eip_phys_bias = virt_eip - cpu.phys_eip;
}

void cpu_load_csip_real(uint16_t cs, uint32_t eip)
{
    SET_VIRT_EIP(eip);
    cpu_seg_load_real(CS, cs);
    reload_cs_base();
}
void cpu_load_csip_virtual(uint16_t cs, uint32_t eip)
{
    SET_VIRT_EIP(eip);
    cpu_seg_load_virtual(CS, cs);
    reload_cs_base();
}
int cpu_load_csip_protected(uint16_t cs, struct seg_desc* info, uint32_t eip)
{
    SET_VIRT_EIP(eip);
    // If the following line faults (unlikely), then cpu.phys_eip will be modified accordingly
    if (cpu_seg_load_protected(CS, cs, info))
        return 1;
    reload_cs_base();
    return 0;
}

void cpu_seg_load_virtual(int id, uint16_t sel)
{
    cpu.seg[id] = sel;
    cpu.seg_base[id] = sel << 4;
    cpu.seg_limit[id] = 0xFFFF;
    cpu.seg_access[id] &= ~(ACCESS_DPL_MASK | ACCESS_B);
    switch (id) {
    case CS:
        cpu.state_hash = STATE_ADDR16 | STATE_CODE16;
        break;
    case SS:
        cpu.esp_mask = 0xFFFF;
        break;
    }
}
void cpu_seg_load_real(int id, uint16_t sel)
{
    cpu.seg[id] = sel;
    cpu.seg_base[id] = sel << 4;
    cpu.seg_limit[id] = 0xFFFF;
    cpu.seg_access[id] &= ~(ACCESS_DPL_MASK | ACCESS_B);

    switch (id) {
    case CS:
        cpu.state_hash = STATE_ADDR16 | STATE_CODE16;
        break;
    case SS:
        cpu.esp_mask = 0xFFFF;
        break;
    }
}
// Note: May raise exception since there's a physical write to update the dirty bit
int cpu_seg_load_protected(int id, uint16_t sel, struct seg_desc* info)
{
    cpu.seg[id] = sel;
    cpu.seg_base[id] = cpu_seg_get_base(info);
    cpu.seg_limit[id] = cpu_seg_get_limit(info);
    cpu.seg_access[id] = DESC_ACCESS(info);

    uint32_t linaddr = cpu_seg_descriptor_address(-1, sel);
    if (linaddr == RESULT_INVALID)
        CPU_FATAL("Out of limits in internal function\n");
    info->raw[1] |= 0x100;
    cpu_write8(linaddr + 5, info->raw[1] >> 8 & 0xFF, TLB_SYSTEM_WRITE);

    switch (id) {
    case CS:
        if (cpu.seg_access[CS] & ACCESS_B)
            cpu.state_hash = 0;
        else
            cpu.state_hash = STATE_ADDR16 | STATE_CODE16;
        cpu.cpl = sel & 3;
        cpu_prot_update_cpl();
        break;
    case SS:
        if (cpu.seg_access[SS] & ACCESS_B)
            cpu.esp_mask = -1;
        else
            cpu.esp_mask = 0xFFFF;
        break;
    }
    return 0;
}

// Load a descriptor from a table, raising "exception" if the descriptor is out of bounds
int cpu_seg_load_descriptor2(int table, uint32_t selector, struct seg_desc* seg, int exception, int code)
{
    if ((selector | 7) > cpu.seg_limit[table]) {
        if (exception == -1)
            return -1; // Some instructions, like VERR, don't cause write faults.
        EXCEPTION2(exception, code);
    }
    int addr = (selector & ~7) + cpu.seg_base[table];
    cpu_read32(addr, seg->raw[0], TLB_SYSTEM_READ);
    cpu_read32(addr + 4, seg->raw[1], TLB_SYSTEM_READ);
    return 0;
}

// Load a descriptor from the LDT or GDT
int cpu_seg_load_descriptor(uint32_t selector, struct seg_desc* seg, int exception, int code)
{
    if (cpu_seg_load_descriptor2(SELECTOR_LDT(selector) ? SEG_LDTR : SEG_GDTR, selector, seg, exception, code))
        return 1;
    return 0;
}

int cpu_seg_get_dpl(int seg)
{
    return ACCESS_DPL(cpu.seg_access[seg]);
}

uint32_t cpu_seg_get_base(struct seg_desc* info)
{
    uint32_t base = info->raw[0] >> 16;
    base |= (info->raw[1] << 16 & 0xFF0000) | (info->raw[1] & 0xFF000000);
    return base;
}

uint32_t cpu_seg_get_limit(struct seg_desc* info)
{
    uint32_t limit = info->raw[0] & 0xFFFF;
    limit |= (info->raw[1] & 0xF0000);
    if (DESC_ACCESS(info) & ACCESS_G) {
        limit <<= 12;
        limit |= 0xFFF;
    }
    return limit;
}

uint32_t cpu_seg_gate_target_segment(struct seg_desc* info)
{
    return info->raw[0] >> 16 & 0xFFFF;
}

uint32_t cpu_seg_gate_target_offset(struct seg_desc* info)
{
    uint32_t offset = info->raw[0] & 0xFFFF;
    int access = DESC_ACCESS(info);
    switch (ACCESS_TYPE(access)) {
    case CALL_GATE_386:
    case INTERRUPT_GATE_386:
    case TRAP_GATE_386:
        return offset | (info->raw[1] & ~0xFFFF);
    default:
        return offset;
    }
}

uint32_t cpu_seg_gate_parameter_count(struct seg_desc* info)
{
    return info->raw[1] & 0x1F;
}

// Get linear address of descriptor table
uint32_t cpu_seg_descriptor_address(int tbl, uint16_t sel)
{
    if (tbl == -1) {
        if (SELECTOR_LDT(sel))
            tbl = SEG_LDTR;
        else
            tbl = SEG_GDTR;
    }
    if ((sel | 7) > cpu.seg_limit[tbl])
        return RESULT_INVALID;
    return (sel & ~7) + cpu.seg_base[tbl];
}

// For use by mov sreg, r/m functions
int cpu_load_seg_value_mov(int seg, uint16_t val)
{
    if ((cpu.cr[0] & CR0_PE) == 0 || (cpu.eflags & EFLAGS_VM)) {
        if (!(cpu.cr[0] & CR0_PE))
            cpu_seg_load_real(seg, val);
        else
            cpu_seg_load_virtual(seg, val);
    } else {
        struct seg_desc info;

        // Prevent GCC from complaining
        info.raw[0] = 0;
        info.raw[1] = 0;

        uint16_t val_offset = val & 0xFFFC;
        int rpl, dpl, type, access;
        switch (seg) {
        case CS:
            return cpu_seg_load_protected(seg, val, &info);
        case SS:
            if (!val_offset)
                EXCEPTION_GP(0);
            if (cpu_seg_load_descriptor(val, &info, EX_GP, val_offset))
                return 1;

            access = DESC_ACCESS(&info);
            rpl = SELECTOR_RPL(val);
            dpl = ACCESS_DPL(access);

            if (cpu.cpl != rpl || cpu.cpl != dpl)
                EXCEPTION_GP(val_offset);

            type = ACCESS_TYPE(access);
            if (type == 0x12 || type == 0x13 || type == 0x16 || type == 0x17) {
                if (!(access & ACCESS_P))
                    EXCEPTION_GP(val_offset);

                return cpu_seg_load_protected(seg, val, &info);
            } else
                EXCEPTION_GP(val_offset);
            break;
        case ES:
        case FS:
        case GS:
        case DS:
            if (val_offset) {
                if (cpu_seg_load_descriptor(val, &info, EX_GP, val_offset))
                    return 1;
                access = DESC_ACCESS(&info);
                type = ACCESS_TYPE(access);
                switch (type) {
                default: // Invalid type for a data segment.
                    EXCEPTION_GP(val_offset);
                    break;
                case 0x1A:
                case 0x1B:
                case 0x1E:
                case 0x1F: // Readable code segment
                    // No checks required?
                    break;
                case 0x10 ... 0x17: // Data segment
                case 0x18 ... 0x19: // Conforming code segment (cases 0x1A and 0x1B are handled above)
                    dpl = ACCESS_DPL(access);
                    rpl = SELECTOR_RPL(val);
                    if (dpl < cpu.cpl || dpl < rpl)
                        EXCEPTION_GP(val_offset);
                    break;
                }
                if (!(access & ACCESS_P))
                    EXCEPTION_NP(val_offset);
                return cpu_seg_load_protected(seg, val, &info);
            } else {
                cpu.seg[seg] = val;
                cpu.seg_base[seg] = 0;
                cpu.seg_limit[seg] = 0;
                cpu.seg_access[seg] = 0;
            }
            break;
        }
    }
    return 0;
}