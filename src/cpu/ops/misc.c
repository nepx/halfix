// Miscellaneous operations
#include "cpu/cpu.h"
#include "cpuapi.h"
#ifdef INSTRUMENT
#include "cpu/instrument.h"
#endif

#define EXCEPTION_HANDLER return 1

void cpuid(void)
{
    CPU_LOG("CPUID called with EAX=%08x\n", cpu.reg32[EAX]);
    switch (cpu.reg32[EAX]) {
    // TODO: Allow this instruction to be customized
    case 0:
        cpu.reg32[EAX] = 2;
        cpu.reg32[ECX] = 0x6c65746e;
        cpu.reg32[EDX] = 0x49656e69;
        cpu.reg32[EBX] = 0x756e6547; // GenuineIntel
        break;
    case 1:
        cpu.reg32[EAX] = 0x000006a0;
        cpu.reg32[ECX] = 0;
        cpu.reg32[EDX] = 0x1842c1bf | cpu_apic_connected() << 9;
        cpu.reg32[EBX] = 0x00010000;
        break;
    case 2:
        cpu.reg32[EAX] = 0x00410601;
        cpu.reg32[ECX] = 0;
        cpu.reg32[EDX] = 0;
        cpu.reg32[EBX] = 0;
        break;
    case 0x80000000:
        cpu.reg32[EAX] = 0x80000008;
        cpu.reg32[ECX] = cpu.reg32[EDX] = cpu.reg32[EBX] = 0;
        break;
    case 0x80000001:
        cpu.reg32[EBX] = 0;
        cpu.reg32[ECX] = cpu.reg32[EDX] = cpu.reg32[EAX] = 0;
        break;
    case 0x80000002 ... 0x80000004: {
        static const char* brand_string = "Halfix Virtual CPU                             ";
        static const int reg_ids[] = { EAX, EBX, ECX, EDX }; // Note: not in ordinary A/C/D/B order
        int offset = (cpu.reg32[EAX] - 0x80000002) << 4;
        for (int i = 0; i < 16; i++) {
            int shift = (i & 3) << 3, reg = reg_ids[i >> 2];
            cpu.reg32[reg] &= ~(0xFF << shift);
            cpu.reg32[reg] |= brand_string[offset + i] << shift;
        }
        break;
    }
    case 0x80000005: // TLB/cache information
        cpu.reg32[EAX] = 0x01ff01ff;
        cpu.reg32[ECX] = 0x40020140;
        cpu.reg32[EBX] = 0x01ff01ff;
        cpu.reg32[EDX] = 0x40020140;
        break;
    case 0x80000006: // TLB/cache information
        cpu.reg32[EAX] = 0;
        cpu.reg32[ECX] = 0x02008140;
        cpu.reg32[EBX] = 0x42004200;
        cpu.reg32[EDX] = 0;
        break;
    case 0x80000008:
        cpu.reg32[EAX] = 0x2028; // TODO: 0x2024 for 36-bit address space?
        cpu.reg32[ECX] = cpu.reg32[EDX] = cpu.reg32[EBX] = 0;
        break;
    default:
        CPU_DEBUG("Unknown CPUID level: 0x%08x\n", cpu.reg32[EAX]);
    case 0x80860000 ... 0x80860007: // Transmeta
        cpu.reg32[EAX] = 0;
        cpu.reg32[ECX] = cpu.reg32[EDX] = cpu.reg32[EBX] = 0;
        break;
    }
}

int rdmsr(uint32_t index, uint32_t* high, uint32_t* low)
{
    uint64_t value;
    switch (index) {
    case 0x1B:
        if(!cpu_apic_connected()) EXCEPTION_GP(0);
        value = cpu.apic_base;
        break;
    case 0x8B: // ??
    case 0x179: // MCG_CAP
    case 0x17A: // MCG_STATUS
    case 0x17B: // MCG_CTL
    case 0x186: // EVNTSEL0
    case 0x187: // EVNTSEL1
    case 0x400: // MC0_CTL
    case 0x19A: // ?? Windows Vista reads from this one
    case 0x19B: // ??
    case 0x19C: // ??
    case 0x1A0: // ??
    case 0x17: // ??
        CPU_LOG("Unknown MSR: 0x%x\n", index); 
        value = 0;
        break;
    case 0x10:
        value = cpu_get_cycles() - cpu.tsc_fudge;
        break;
    default:
        CPU_FATAL("Unknown MSR: 0x%x\n", index);
    }

    *high = value >> 32;
    *low = value & 0xFFFFFFFF;

#ifdef INSTRUMENT
    cpu_instrument_access_msr(index, *high, *low, 0);
#endif
    return 0;
}

int wrmsr(uint32_t index, uint32_t high, uint32_t low)
{
    uint64_t msr_value = ((uint64_t)high) << 32 | (uint64_t)low;
    switch (index) {
    case 0x8B: // ??
    case 0x17: // ??
    case 0x179: // MCG_CAP
    case 0x17A: // MCG_STATUS
    case 0x17B: // MCG_CTL
    case 0x186: // EVNTSEL0
    case 0x187: // EVNTSEL1
    case 0x19A: // Windows Vista MSR
    case 0x19B: // ??
        CPU_LOG("Unknown MSR: 0x%x\n", index);
        break;
    case 0x10:
        cpu.tsc_fudge = cpu_get_cycles() - msr_value;
        break;
    default:
        CPU_FATAL("Unknown MSR: 0x%x\n", index);
    }

#ifdef INSTRUMENT
    cpu_instrument_access_msr(index, high, low, 1);
#endif
    return 0;
}

int pushf(void)
{
    if ((cpu.eflags & EFLAGS_VM && get_iopl() < 3)) {
        if ((cpu.cr[4] & CR4_VME) != 0) {
            uint16_t flags = cpu_get_eflags();
            flags &= ~(1 << 9); // IF is replaced with VIF
            flags |= ((cpu.eflags & (1 << 19)) != 0) << 9;
            flags |= EFLAGS_IOPL; // IOPL=3 in image pushed to stack
            return cpu_push16(flags);
        } else
            EXCEPTION_GP(0);
    }
    return cpu_push16(cpu_get_eflags() & 0xFFFF);
}
int pushfd(void)
{
    if ((cpu.eflags & EFLAGS_VM && get_iopl() < 3)) {
        EXCEPTION_GP(0);
    }
    return cpu_push32(cpu_get_eflags() & 0x00FCFFFF);
}

// https://mudongliang.github.io/x86/html/file_module_x86_id_250.html
// https://www.felixcloutier.com/x86/popf:popfd:popfq
int popf(void)
{
    if (cpu.eflags & EFLAGS_VM) {
        if (get_iopl() == 3)
            goto cpl_gt_0;
        else { // iopl < 3
            if (cpu.cr[4] & CR4_VME) {
                uint16_t temp_flags;
                if (cpu_pop16(&temp_flags))
                    return 1;
                if (
                    !((cpu.eflags & EFLAGS_VIP && temp_flags & (1 << 9)) || (temp_flags & (1 << 8)))) {
                    cpu.eflags &= ~EFLAGS_VIF;
                    cpu.eflags |= (temp_flags & (1 << 9)) ? EFLAGS_VIF : 0;
                    const uint32_t flags_mask = 0xFFFF ^ (EFLAGS_IF | EFLAGS_IOPL);
                    cpu_set_eflags((temp_flags & flags_mask) | (cpu.eflags & ~flags_mask));
                    return 0;
                }
            }
            EXCEPTION_GP(0);
        }
    }
    uint16_t eflags;
    if (cpu.cpl == 0 || (cpu.cr[0] & CR0_PE) == 0) {
        if (cpu_pop16(&eflags))
            return 1;
        cpu_set_eflags(eflags | (cpu.eflags & 0xFFFF0000));
    } else { // cpu.cpl > 0
    cpl_gt_0:
        if (cpu_pop16(&eflags))
            return 1;
        cpu_set_eflags((eflags & ~EFLAGS_IOPL) | (cpu.eflags & (0xFFFF0000 | EFLAGS_IOPL)));
    }
    return 0;
}
int popfd(void)
{
    uint32_t eflags;
    if (cpu.eflags & EFLAGS_VM) {
        if (get_iopl() == 3) {
            if (cpu_pop32(&eflags))
                return 1;
            eflags &= ~(EFLAGS_IOPL | EFLAGS_VIP | EFLAGS_VIF | EFLAGS_VM | EFLAGS_RF);
            cpu_set_eflags(eflags | (cpu.eflags & (EFLAGS_IOPL | EFLAGS_VIP | EFLAGS_VIF | EFLAGS_VM | EFLAGS_RF)));
        } else
            EXCEPTION_GP(0);
    } else {
        if (cpu.cpl == 0 || (cpu.cr[0] & CR0_PE) == 0) {
            if (cpu_pop32(&eflags))
                return 1;
            eflags = eflags & ~EFLAGS_RF;
            const uint32_t preserve = EFLAGS_VIP | EFLAGS_VIF | EFLAGS_VM;
            cpu_set_eflags((eflags & ~preserve) | (cpu.eflags & preserve));
        } else { // CPL > 0
            if (cpu_pop32(&eflags))
                return 1;
            eflags = eflags & ~EFLAGS_RF;
            uint32_t preserve = EFLAGS_IOPL | EFLAGS_VIP | EFLAGS_VIF | EFLAGS_VM;
            if ((unsigned int)cpu.cpl > get_iopl())
                preserve |= EFLAGS_IF;
            cpu_set_eflags((eflags & ~preserve) | (cpu.eflags & preserve));
        }
    }
    return 0;
}

int ltr(uint32_t selector)
{
    // Load task register
    printf("%04x\n", selector);
    uint32_t selector_offset = selector & 0xFFFC, tss_access, tss_addr;
    struct seg_desc tss_desc;
    // Cannot be NULL
    if (selector_offset == 0)
        EXCEPTION_GP(0);
    // Must be global
    if (SELECTOR_LDT(selector))
        EXCEPTION_GP(selector_offset);
    if (cpu_seg_load_descriptor2(SEG_GDTR, selector, &tss_desc, EX_GP, selector_offset))
        return 1;
    tss_access = DESC_ACCESS(&tss_desc);
    if ((tss_access & ACCESS_P) == 0)
        EXCEPTION_NP(selector_offset);

    tss_addr = cpu_seg_descriptor_address(SEG_GDTR, selector);

    tss_desc.raw[1] |= 0x200; // Set BSY bit
    // Set busy bit
    cpu_write32(tss_addr + 4, tss_desc.raw[1], TLB_SYSTEM_WRITE);

    // Load segment selector/descriptor
    cpu.seg_base[SEG_TR] = cpu_seg_get_base(&tss_desc);
    cpu.seg_limit[SEG_TR] = cpu_seg_get_limit(&tss_desc);
    cpu.seg_access[SEG_TR] = DESC_ACCESS(&tss_desc);
    cpu.seg[SEG_TR] = selector;
    return 0;
}
int lldt(uint32_t selector)
{
    uint32_t selector_offset = selector & 0xFFFC, ldt_access;
    struct seg_desc ldt_desc;

    if (selector_offset == 0) {
        //  bits 2-15 of the source operand are 0, LDTR is marked invalid and the LLDT instruction completes silently.
        CPU_LOG("Disabling LDT (selector=0)\n");
        cpu.seg_base[SEG_LDTR] = 0;
        cpu.seg_limit[SEG_LDTR] = 0;
        cpu.seg_access[SEG_LDTR] = 0;
        cpu.seg[SEG_LDTR] = selector;
        return 0;
    }
    // Must be global
    if (SELECTOR_LDT(selector_offset))
        EXCEPTION_GP(selector_offset);
    if (cpu_seg_load_descriptor2(SEG_GDTR, selector, &ldt_desc, EX_GP, selector_offset))
        return 1;
    ldt_access = DESC_ACCESS(&ldt_desc);
    if ((ldt_access & ACCESS_P) == 0)
        EXCEPTION_NP(selector_offset);

    // Load segment selector/descriptor
    cpu.seg_base[SEG_LDTR] = cpu_seg_get_base(&ldt_desc);
    cpu.seg_limit[SEG_LDTR] = cpu_seg_get_limit(&ldt_desc);
    cpu.seg_access[SEG_LDTR] = DESC_ACCESS(&ldt_desc);
    cpu.seg[SEG_LDTR] = selector;
    return 0;
}

// Returns access rights byte if all is successful.
uint32_t lar(uint16_t op1, uint32_t op2)
{
    uint16_t op_offset = op1 & 0xFFFC;
    struct seg_desc op_info;
    uint32_t op_access;
    if (!op_offset)
        goto invalid;
    if (cpu_seg_load_descriptor(op1, &op_info, -1, -1))
        goto invalid;
    op_access = DESC_ACCESS(&op_info);
    int dpl;
    switch (ACCESS_TYPE(op_access)) {
    case 0:
    case INTERRUPT_GATE_286: // 6
    case TRAP_GATE_286: // 7
    case 8:
    case 10:
    case 13:
    case INTERRUPT_GATE_386: // 14
    case TRAP_GATE_386: // 15
        goto invalid;
    case 0x18 ... 0x1B:
        // Non-conforming code segment
        dpl = ACCESS_DPL(op_access);
        // DPL must be >= cpl and rpl
        if (dpl < cpu.cpl || dpl < SELECTOR_RPL(op1))
            goto invalid;

    // INTENTIONAL FALLTHROUGH
    case 0x1C ... 0x1F: // Conforming code segment (no checks)
    default:
        cpu_set_zf(1);
        return op_info.raw[1] & 0xFFFF00;
    }
invalid:
    cpu_set_zf(0);
    return op2;
}

uint32_t lsl(uint16_t op, uint32_t op2)
{
    if ((cpu.cr[0] & CR0_PE) == 0 || (cpu.eflags & EFLAGS_VM))
        EXCEPTION_UD();

    uint16_t op_offset = op & 0xFFFC;
    int op_access;
    // Check if NULL
    if (!op_offset)
        goto invalid;

    struct seg_desc op_info;
    if (cpu_seg_load_descriptor(op, &op_info, -1, -1))
        goto invalid;

    int dpl;
    op_access = DESC_ACCESS(&op_info);
    switch (ACCESS_TYPE(op_access)) {
    case 0:
    case 4 ... 7:
    case 12 ... 15:
    case 0x1E:
        goto invalid;
    case 0x18 ... 0x1B:
        // Non-conforming code segment
        dpl = ACCESS_DPL(op_access);
        // DPL must be >= cpl and rpl
        if (dpl < cpu.cpl || dpl < SELECTOR_RPL(op))
            goto invalid;
    // Intentional fallthrough
    default:
        cpu_set_zf(1);
        return cpu_seg_get_limit(&op_info);
    }

invalid:
    cpu_set_zf(0);
    return op2;
}

// Verify a segment's read/write permissions. Defaults to read if "write" parameter is not set.
void verify_segment_access(uint16_t sel, int write)
{
    uint16_t sel_offset = sel & 0xFFFC;
    int zf = 0;
    struct seg_desc seg;
    if (sel_offset) {
        if (cpu_seg_load_descriptor(sel, &seg, -1, -1) == 0) {
            int access = DESC_ACCESS(&seg);
            int type = ACCESS_TYPE(access);
            int dpl = ACCESS_DPL(access), rpl = SELECTOR_RPL(sel);
            zf = 1;
            if (write) {
                if (!(type == 0x12 || type == 0x13 || type == 0x16 || type == 0x17))
                    zf = 0;
                else {
                    if ((dpl < cpu.cpl) || (dpl < rpl))
                        zf = 0;
                }
            } else {
                if (type >= 0x10 && type <= 0x1B) { // Data segment & Non-conforming code segment
                    if ((dpl < cpu.cpl) || (dpl < rpl))
                        zf = 0;
                }
            }
        } // Otherwise ZF=0
    } // Otherwise, ZF=0
    cpu_set_zf(zf);
}

void arpl(uint16_t* ptr, uint16_t reg)
{
    reg &= 3;
    if ((*ptr & 3) < reg) {
        *ptr = (*ptr & ~3) | reg;
        cpu_set_zf(1);
    } else
        cpu_set_zf(0);
}