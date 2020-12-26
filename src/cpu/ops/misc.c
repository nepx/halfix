// Miscellaneous operations
#include "cpu/cpu.h"
#include "cpuapi.h"
#ifdef INSTRUMENT
#include "cpu/instrument.h"
#endif

#define EXCEPTION_HANDLER return 1

// If every define below is commented, then it'll pretend to be a generic Pentium-compatible CPU.
// Uncomment this to make the CPU pretend it's a 486 (necessary for NT 3.51 install)
//#define I486_SUPPORT 
// Uncomment this to make the CPU pretend it's a Pentium 4
//#define P4_SUPPORT
// Uncomment this to make the CPU pretend it's a Core Duo
// Required for Windows 8
//#define CORE_DUO_SUPPORT
// Uncomment this to make the CPU pretend it's an Intel Atom N270
#define ATOM_N270_SUPPORT
static int winnt_limit_cpuid;

// Sets CPUID information. Currently unimplemented
int cpu_set_cpuid(struct cpu_config* x)
{
    winnt_limit_cpuid = x->cpuid_limit_winnt;
    return 0;
}

void cpuid(void)
{
    //CPU_LOG("CPUID called with EAX=%08x\n", cpu.reg32[EAX]);
    switch (cpu.reg32[EAX]) {
    // TODO: Allow this instruction to be customized
    case 0:
#if defined(CORE_DUO_SUPPORT) || defined(ATOM_N270_SUPPORT)
        cpu.reg32[EAX] = 10;
#elif defined (I486_SUPPORT)
        cpu.reg32[EAX] = 1;
#else
        cpu.reg32[EAX] = 2; // Windows NT doesn't like big CPU levels!
#endif
        cpu.reg32[ECX] = 0x6c65746e;
        cpu.reg32[EDX] = 0x49656e69;
        cpu.reg32[EBX] = 0x756e6547; // GenuineIntel

        if (winnt_limit_cpuid)
            cpu.reg32[EAX] = 2;
        break;
    case 1:
#ifdef P4_SUPPORT
        cpu.reg32[EAX] = 0x00000f12;
        cpu.reg32[ECX] = 0;
        cpu.reg32[EDX] = 0x1febfbff | cpu_apic_connected() << 9;
        cpu.reg32[EBX] = 0x00010800;
#elif defined(CORE_DUO_SUPPORT)
        cpu.reg32[EAX] = 0x000006EC;
        cpu.reg32[ECX] = 0xC189;
        cpu.reg32[EDX] = 0x9febf9ff | cpu_apic_connected() << 9;
        cpu.reg32[EBX] = 0x00010800;
#elif defined(ATOM_N270_SUPPORT)
        cpu.reg32[EAX] = 0x000106C2;
        cpu.reg32[ECX] = 0x40C39D;
        cpu.reg32[EDX] = 0xBFEBF9FF | cpu_apic_connected() << 9;
        cpu.reg32[EBX] = 0x00010800;
#elif defined (I486_SUPPORT)
        cpu.reg32[EAX] = 0x402;
        cpu.reg32[ECX] = 0;
        cpu.reg32[EDX] = 1; // FPU
        cpu.reg32[EBX] = 0;
#else
        cpu.reg32[EAX] = 0x000006a0;
        cpu.reg32[ECX] = 0;
        cpu.reg32[EDX] = 0x1842c1bf | cpu_apic_connected() << 9;
        cpu.reg32[EBX] = 0x00010000;
#endif
        break;
#ifndef I486_SUPPORT
    case 2:
#ifdef P4_SUPPORT
        cpu.reg32[EAX] = 0x665b5001;
        cpu.reg32[ECX] = 0;
        cpu.reg32[EDX] = 0x007a7040;
        cpu.reg32[EBX] = 0;
#elif defined(CORE_DUO_SUPPORT)
        cpu.reg32[EAX] = 0x02b3b001;
        cpu.reg32[ECX] = 0;
        cpu.reg32[EDX] = 0x2c04307d;
        cpu.reg32[EBX] = 0xF0;
#elif defined(ATOM_N270_SUPPORT)
        cpu.reg32[EAX] = 0x4fba5901;
        cpu.reg32[ECX] = 0;
        cpu.reg32[EDX] = 0;
        cpu.reg32[EBX] = 0x0e3080c0;
#else
        cpu.reg32[EAX] = 0x00410601;
        cpu.reg32[ECX] = 0;
        cpu.reg32[EDX] = 0;
        cpu.reg32[EBX] = 0;
#endif
        break;
#if defined(CORE_DUO_SUPPORT) || defined(ATOM_N270_SUPPORT)
    case 4:
        switch (cpu.reg32[ECX]) {
        case 0:
#if defined (CORE_DUO_SUPPORT)
            cpu.reg32[EAX] = 0x04000121;
            cpu.reg32[EBX] = 0x01C0003F;
            cpu.reg32[ECX] = 0x0000003F;
            cpu.reg32[EDX] = 0x00000001;
#else // ATOM_N270_SUPPORT
            cpu.reg32[EAX] = 0x00004121;
            cpu.reg32[EBX] = 0x0140003f;
            cpu.reg32[ECX] = 0x0000003F;
            cpu.reg32[EDX] = 0x00000001;
#endif
            break;
        case 1:
#if defined (CORE_DUO_SUPPORT)
            cpu.reg32[EAX] = 0x04000122;
            cpu.reg32[EBX] = 0x01C0003F;
            cpu.reg32[ECX] = 0x0000003F;
            cpu.reg32[EDX] = 0x00000001;
#else // ATOM_N270_SUPPORT
            cpu.reg32[EAX] = 0x00004122;
            cpu.reg32[EBX] = 0x01C0003f;
            cpu.reg32[ECX] = 0x0000003F;
            cpu.reg32[EDX] = 0x00000001;
#endif
            break;
        case 2:
#if defined (CORE_DUO_SUPPORT)
            cpu.reg32[EAX] = 0x04004143;
            cpu.reg32[EBX] = 0x01C0003F;
            cpu.reg32[ECX] = 0x00000FFF;
            cpu.reg32[EDX] = 0x00000001;
#else // ATOM_N270_SUPPORT
            cpu.reg32[EAX] = 0x00004143;
            cpu.reg32[EBX] = 0x01C0003F;
            cpu.reg32[ECX] = 0x000003FF;
            cpu.reg32[EDX] = 0x00000001;
#endif
            break;
        default:
            cpu.reg32[EAX] = 0;
            cpu.reg32[EBX] = 0;
            cpu.reg32[ECX] = 0;
            cpu.reg32[EDX] = 0;
            return;
        }
        break;
    case 5: 
#ifdef CORE_DUO_SUPPORT
        cpu.reg32[EAX] = 0x00000040;
        cpu.reg32[ECX] = 0x00000003;
        cpu.reg32[EDX] = 0x00022220;
        cpu.reg32[EBX] = 0x00000040;
#else
        cpu.reg32[EAX] = 0x00000040;
        cpu.reg32[ECX] = 0x00000003;
        cpu.reg32[EDX] = 0x00020220;
        cpu.reg32[EBX] = 0x00000040;
#endif
        break;
    case 6:
        cpu.reg32[EAX] = 1;
        cpu.reg32[ECX] = 1;
        cpu.reg32[EDX] = 0;
        cpu.reg32[EBX] = 2;
        break;
#ifdef ATOM_N270_SUPPORT
    case 0x40000000:
    case 0x40000001:
#endif
    case 10:
#ifdef CORE_DUO_SUPPORT
        cpu.reg32[EAX] = 0x07280201;
        cpu.reg32[EBX] = 0x00000000;
        cpu.reg32[ECX] = 0x00000000;
        cpu.reg32[EDX] = 0x00000000;
#else
        cpu.reg32[EAX] = 0x07280203;
        cpu.reg32[EBX] = 0x00000000;
        cpu.reg32[ECX] = 0x00000000;
        cpu.reg32[EDX] = 0x00002501;
#endif
        break;
#endif // defined(CORE_DUO_SUPPORT) || defined(ATOM_N270_SUPPORT)
    case 0x80000000:
#ifdef P4_SUPPORT
        cpu.reg32[EAX] = 0x80000004;
        cpu.reg32[ECX] = cpu.reg32[EDX] = cpu.reg32[EBX] = 0;
#else
        cpu.reg32[EAX] = 0x80000008;
        cpu.reg32[ECX] = cpu.reg32[EDX] = cpu.reg32[EBX] = 0;
#endif
        break;
    case 0x80000001:
#ifdef CORE_DUO_SUPPORT
        cpu.reg32[EDX] = 0x00100000;
        cpu.reg32[EBX] = 0;
        cpu.reg32[ECX] = cpu.reg32[EAX] = 0;
#elif defined(ATOM_N270_SUPPORT)
        cpu.reg32[EAX] = 0;
        cpu.reg32[ECX] = 1;
        cpu.reg32[EDX] = cpu.reg32[EBX] = 0;
#else
        cpu.reg32[EBX] = 0;
        cpu.reg32[ECX] = cpu.reg32[EDX] = cpu.reg32[EAX] = 0;
#endif
        break;
    case 0x80000002 ... 0x80000004: {
        static const char* brand_string =
#ifdef P4_SUPPORT
            "              Intel(R) Pentium(R) 4 CPU 1.80GHz"
#elif defined(CORE_DUO_SUPPORT)
            "Intel(R) Core(TM) Duo CPU      T2400  @ 1.83GHz"
#elif defined(ATOM_N270_SUPPORT)
            "         Intel(R) Atom(TM) CPU N270   @ 1.60GHz"
#else
            "Halfix Virtual CPU                             "
#endif
            ;
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
#ifndef CORE_DUO_SUPPORT
        cpu.reg32[EAX] = 0x01ff01ff;
        cpu.reg32[ECX] = 0x40020140;
        cpu.reg32[EBX] = 0x01ff01ff;
        cpu.reg32[EDX] = 0x40020140;
#else
        cpu.reg32[EAX] = 0;
        cpu.reg32[ECX] = 0;
        cpu.reg32[EBX] = 0;
        cpu.reg32[EDX] = 0;
#endif
        break;
    case 0x80000006: // TLB/cache information
#ifndef CORE_DUO_SUPPORT
        cpu.reg32[EAX] = 0;
        cpu.reg32[ECX] = 0x02008140;
        cpu.reg32[EBX] = 0x42004200;
        cpu.reg32[EDX] = 0;
#else
        cpu.reg32[EAX] = 0;
        cpu.reg32[ECX] = 0x08006040;
        cpu.reg32[EBX] = 0;
        cpu.reg32[EDX] = 0;
#endif
        break;
    case 0x80000008:
        cpu.reg32[EAX] = 0x2028; // TODO: 0x2024 for 36-bit address space?
        cpu.reg32[ECX] = cpu.reg32[EDX] = cpu.reg32[EBX] = 0;
        break;
#endif /* ndef I486_SUPPORT */
    default:
        CPU_DEBUG("Unknown CPUID level: 0x%08x\n", cpu.reg32[EAX]);
        goto __annoying_gcc_workaround;
    case 0x80860000 ... 0x80860007: // Transmeta
__annoying_gcc_workaround:
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
        if (!cpu_apic_connected())
            EXCEPTION_GP(0);
        value = cpu.apic_base;
        break;
    case 0x250 ... 0x26F:
        value = cpu.mtrr_fixed[index - 0x250];
        break;
    case 0x200 ... 0x20F:
        value = cpu.mtrr_variable_addr_mask[index ^ 0x200];
        break;
    case 0x277:
        value = cpu.page_attribute_tables;
        break;
    case 0x2FF:
        value = cpu.mtrr_deftype;
        break;
    default:
        CPU_LOG("Unknown MSR read: 0x%x\n", index);
        value = 0;
        break;
    case 0x174 ... 0x176:
        value = cpu.sysenter[index - 0x174];
        break;
    case 0xFE: // MTRR
        value = 0x508;
        break;
    case 0x10:
        value = cpu_get_cycles() - cpu.tsc_fudge;
        break;
    case 0xc0000080:
        value = cpu.ia32_efer;
    }

    *high = value >> 32;
    *low = value & 0xFFFFFFFF;

#ifdef INSTRUMENT
    if (index == 0x10) {
        cpu_instrument_rdtsc(*low, *high);
    }
    cpu_instrument_access_msr(index, *high, *low, 0);
#endif
    return 0;
}

int wrmsr(uint32_t index, uint32_t high, uint32_t low)
{
    CPU_LOG("WRMSR index=%x\n", index);
    uint64_t msr_value = ((uint64_t)high) << 32 | (uint64_t)low;
    switch (index) {
    case 0x1B:
        cpu.apic_base = msr_value;
        break;
    case 0x174 ... 0x176: // SYSENTER
        cpu.sysenter[index - 0x174] = low;
        break;
    case 0x8B: // ??
    case 0x17: // ??
    case 0xC1: // PERFCTR0
    case 0xC2: // PERFCTR1
    case 0x179: // MCG_CAP
    case 0x17A: // MCG_STATUS
    case 0x17B: // MCG_CTL
    case 0x186: // EVNTSEL0
    case 0x187: // EVNTSEL1
    case 0x19A: // Windows Vista MSR
    case 0x19B: // ??
    case 0xFE:
        CPU_LOG("Unknown MSR: 0x%x\n", index);
        break;
    case 0x250 ... 0x26F:
        cpu.mtrr_fixed[index - 0x250] = msr_value;
        break;
    case 0x200 ... 0x20F:
        cpu.mtrr_variable_addr_mask[index ^ 0x200] = msr_value;
        break;
    case 0x277:
        cpu.page_attribute_tables = msr_value;
        break;
    case 0x2FF:
        cpu.mtrr_deftype = msr_value;
        break;
    case 0x10:
        cpu.tsc_fudge = cpu_get_cycles() - msr_value;
        break;
    case 0xc0000080: // https://wiki.osdev.org/CPU_Registers_x86-64#IA32_EFER
        cpu.ia32_efer = msr_value;
        break;
    default:
        CPU_LOG("Unknown MSR write: 0x%x\n", index);
        //CPU_FATAL("Unknown MSR: 0x%x\n", index);
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
        //CPU_LOG("Disabling LDT (selector=0)\n");
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