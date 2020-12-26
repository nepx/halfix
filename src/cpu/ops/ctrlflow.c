// Control flow opcodes

// Complex control flow opcodes
// Includes jmpf, callf, interrupts, retf, iret, and exceptions

#include "cpu/cpu.h"
#include "cpu/instruction.h"
#include "display.h"
#include "platform.h"

#define EXCEPTION_HANDLER return 1

// When some kind of memory exception occurs, return a non-zero value
#define ACCESS_EXCEPTION_HANDLER return 1

static const int cpl_to_TLB_write[4] = {
    TLB_SYSTEM_WRITE,
    TLB_SYSTEM_WRITE,
    TLB_SYSTEM_WRITE,
    TLB_USER_WRITE
};

#define FAST_STACK_INIT \
    uint32_t STACK_esp, STACK_ss_base, STACK_esp_mask, STACK_mask, STACK_original_esp;

// These are utilities to handle the stack quickly. Note that the values are only used every instruction
// This is useful because typically these instructions push/pop a lot of information on/off the stack
struct
{
    uint32_t esp; // The value of ESP itself
    int esp_aligned; // Is ESP aligned to 2/4-byte boundary?
    uint32_t ss_base, esp_mask; // Note: cannot use cpu.esp_mask and cpu.seg_base[SS] in all cases since some instructions use a different stack
    int mask; // To pass to cpu_access_* functions
    void* phys_ptr;

    uint32_t byteoffset; // The offset we are in on the page
} stack_info;

// Initialize fast push information
// XXX: Will be weird with stack segments with unconventional limit/base addresses (i.e. base=0x123450 limit=0xFFFF)
#define init_fast_push(esp, ss, esp_mask, tlb, is32) \
    STACK_esp = esp & esp_mask;                      \
    STACK_original_esp = esp;                        \
    STACK_ss_base = ss;                              \
    STACK_esp_mask = esp_mask;                       \
    STACK_mask = tlb;

#define fast_push_enabled STACK_esp_aligned

#define push32(value)                                              \
    do {                                                           \
        STACK_esp = (STACK_esp - 4) & STACK_esp_mask;              \
        cpu_write32(STACK_esp + STACK_ss_base, value, STACK_mask); \
    } while (0)
#define push16(value)                                              \
    do {                                                           \
        STACK_esp = (STACK_esp - 2) & STACK_esp_mask;              \
        cpu_write16(STACK_esp + STACK_ss_base, value, STACK_mask); \
    } while (0)
#define pop32(dest)                                              \
    do {                                                         \
        cpu_read32(STACK_esp + STACK_ss_base, dest, STACK_mask); \
        STACK_esp = (STACK_esp + 4) & STACK_esp_mask;            \
    } while (0)
#define pop16(dest)                                              \
    do {                                                         \
        cpu_read16(STACK_esp + STACK_ss_base, dest, STACK_mask); \
        STACK_esp = (STACK_esp + 2) & STACK_esp_mask;            \
    } while (0)
#define modify_esp(a) STACK_esp = (STACK_esp + a) & STACK_esp_mask

#define set_esp() cpu.reg32[ESP] = (STACK_esp_mask & STACK_esp) | (STACK_original_esp & ~STACK_esp_mask)

// The following are the types of task gate jumps
enum {
    TASK_JMP,
    TASK_CALL,
    TASK_INT,
    TASK_IRET
};

static inline int tss_is_16(int type)
{
    return type == BUSY_TSS_286 || type == AVAILABLE_TSS_286;
}

// Used during task gates. It loads a TSS segment from the GDT
static int load_tss_from_task_gate(uint32_t* seg, struct seg_desc* info)
{
    int new_seg = cpu_seg_gate_target_segment(info), offset = new_seg & 0xFFFC;
    // Segment cannot reside in LDT
    if (SELECTOR_LDT(new_seg))
        EXCEPTION_TS(offset);

    // Load descriptor from GDT, raising a #GP if it goes out of bounds
    if (cpu_seg_load_descriptor2(SEG_GDTR, new_seg, info, EX_GP, offset))
        return 1;
    int access = DESC_ACCESS(info), type = ACCESS_TYPE(access);

    // Must be an available TSS segment of any size
    if (type != AVAILABLE_TSS_286 && type != AVAILABLE_TSS_386)
        EXCEPTION_GP(offset);

    // Must be present
    if ((access & ACCESS_P) == 0)
        EXCEPTION_NP(offset);
    *seg = new_seg;
    return 0;
}

// Load ESP for a certain privilege ring from the TSS
static int get_tss_esp(int level, int* dest)
{
    int temp;
    // If this is a 16-bit TSS, then load only SP
    if (tss_is_16(ACCESS_TYPE(cpu.seg_access[SEG_TR]))) {
        int addr = 2 + (level * 4);
        // Must be within TSS limits
        if ((unsigned int)(addr + 2) >= cpu.seg_limit[SEG_TR])
            EXCEPTION_TS(cpu.seg[SEG_TR] & 0xFFFC);

        // Read from kernel memory
        cpu_read16(addr + cpu.seg_base[SEG_TR], temp, TLB_SYSTEM_READ);
    } else {
        int addr = 4 + (level * 8);
        if ((unsigned int)(addr + 4) >= cpu.seg_limit[SEG_TR])
            EXCEPTION_TS(cpu.seg[SEG_TR] & 0xFFFC);
        cpu_read32(addr + cpu.seg_base[SEG_TR], temp, TLB_SYSTEM_READ);
    }
    *dest = temp;
    return 0;
}

// Load SS for a certain privilege ring from the TSS
static int get_tss_ss(int level, int* dest)
{
    int temp;
    // If this is a 16-bit TSS, then load only SP
    if (tss_is_16(ACCESS_TYPE(cpu.seg_access[SEG_TR]))) {
        int addr = 2 + (level * 4) + 2;
        // Must be within TSS limits
        if ((unsigned int)(addr + 2) >= cpu.seg_limit[SEG_TR])
            EXCEPTION_TS(cpu.seg[SEG_TR] & 0xFFFC);

        // Read from kernel memory
        cpu_read16(addr + cpu.seg_base[SEG_TR], temp, TLB_SYSTEM_READ);
    } else {
        int addr = 4 + (level * 8) + 4;
        if ((unsigned int)(addr + 4) >= cpu.seg_limit[SEG_TR])
            EXCEPTION_TS(cpu.seg[SEG_TR] & 0xFFFC);
        cpu_read32(addr + cpu.seg_base[SEG_TR], temp, TLB_SYSTEM_READ);
    }
    *dest = temp & 0xFFFF;
    return 0;
}

// Switch tasks
static int do_task_switch(int sel, struct seg_desc* info, int type, int eip)
{
    // TODO: Trap bit
    static const int tss_limits[] = { 43, 103 };
    int offset = sel & 0xFFFC,
        limit = cpu_seg_get_limit(info),
        base = cpu_seg_get_base(info),
        access = DESC_ACCESS(info),
        tss_type = ACCESS_TYPE(access);

    // Make sure selector is valid and has the correct size
    if (SELECTOR_LDT(sel))
        EXCEPTION_TS(offset);
    if (limit <= tss_limits[type >> 3 & 1])
        EXCEPTION_TS(offset);

    int old_tr_type = ACCESS_TYPE(cpu.seg_access[SEG_TR]),
        old_tr_limit = tss_limits[old_tr_type >> 3 & 1];
    uint32_t tr_base = cpu.seg_base[SEG_TR],
             desc_tbl,
             old_eflags = cpu_get_eflags();
    // Pre-translate these addresses to prevent ourselves from faulting
    if (cpu_access_verify(tr_base, tr_base + old_tr_limit, TLB_SYSTEM_READ))
        return 1;
    if (cpu_access_verify(tr_base, tr_base + old_tr_limit, TLB_SYSTEM_WRITE))
        return 1;
    // If instruction is JMP or IRET, then clear busy flag
    if (type == TASK_JMP || type == TASK_IRET) {
        if (tr_base == RESULT_INVALID)
            ABORT(); // should not be invalid

        uint32_t segid = SELECTOR_LDT(sel) ? SEG_LDTR : SEG_GDTR,
                 addr = cpu.seg_base[segid] + ((cpu.seg[SEG_TR] & ~7)) + 5;
        cpu_read8(addr, desc_tbl, TLB_SYSTEM_READ);
        desc_tbl &= ~2;
        cpu_write8(addr, desc_tbl, TLB_SYSTEM_WRITE);
        if (type == TASK_IRET)
            old_eflags &= ~EFLAGS_NT;
    }

    // Write back current state to TSS
    if (tss_type == AVAILABLE_TSS_386 || tss_type == BUSY_TSS_386) {
        cpu_write32(tr_base + 0x20, eip, TLB_SYSTEM_WRITE);
        cpu_write32(tr_base + 0x24, old_eflags, TLB_SYSTEM_WRITE);
        for (int i = 0; i < 8; i++)
            cpu_write32(tr_base + 0x28 + (i * 4), cpu.reg32[i], TLB_SYSTEM_WRITE);
        for (int i = 0; i < 6; i++)
            cpu_write32(tr_base + 0x48 + (i * 4), cpu.seg[i], TLB_SYSTEM_WRITE);
    } else {
        cpu_write16(tr_base + 0x0E, eip, TLB_SYSTEM_WRITE);
        cpu_write16(tr_base + 0x10, old_eflags, TLB_SYSTEM_WRITE);
        for (int i = 0; i < 8; i++)
            cpu_write16(tr_base + 0x12 + (i * 2), cpu.reg32[i], TLB_SYSTEM_WRITE);
        for (int i = 0; i < 4; i++)
            cpu_write16(tr_base + 0x22 + (i * 2), cpu.seg[i], TLB_SYSTEM_WRITE);
    }

    // Write back task link, if needed
    if (type == TASK_INT || type == TASK_CALL)
        cpu_write16(tr_base, cpu.seg[SEG_TR], TLB_SYSTEM_WRITE);

    // Read state from new selector
    uint32_t cr3 = 0, /*eip, */ eflags, reg32[8], seg[6], ldt;
    if (tss_type == AVAILABLE_TSS_386 || tss_type == BUSY_TSS_386) {
        cpu_read32(base + 0x1C, cr3, TLB_SYSTEM_READ);
        cpu_read32(base + 0x20, eip, TLB_SYSTEM_READ);
        cpu_read32(base + 0x24, eflags, TLB_SYSTEM_READ);
        for (int i = 0; i < 8; i++)
            cpu_read32(base + 0x28 + (i * 4), reg32[i], TLB_SYSTEM_READ);
        for (int i = 0; i < 6; i++)
            cpu_read16(base + 0x48 + (i * 4), seg[i], TLB_SYSTEM_READ);
        cpu_read32(base + 0x60, ldt, TLB_SYSTEM_READ);
    } else {
        // Don't set CR3
        cpu_read16(base + 0x0E, eip, TLB_SYSTEM_READ);
        cpu_read16(base + 0x10, eflags, TLB_SYSTEM_READ);
        for (int i = 0; i < 8; i++) {
            cpu_read16(base + 0x12 + (i * 2), reg32[i], TLB_SYSTEM_READ);
            reg32[i] |= 0xFFFF0000;
        }
        for (int i = 0; i < 4; i++)
            cpu_read16(base + 0x22 + (i * 2), seg[i], TLB_SYSTEM_READ);
        cpu_read16(base + 0x2A, ldt, TLB_SYSTEM_READ);
        seg[FS] = 0;
        seg[GS] = 0;
    }

    // Set BSY state if type is not IRET
    tr_base = cpu_seg_descriptor_address(SEG_GDTR, sel);
    if (type == TASK_JMP || type == TASK_IRET) {
        if (tr_base == RESULT_INVALID)
            ABORT(); // should not be invalid
        uint32_t segid = SELECTOR_LDT(sel) ? SEG_LDTR : SEG_GDTR,
                 addr = cpu.seg_base[segid] + ((sel & ~7)) + 5;
        cpu_read8(addr, desc_tbl, TLB_SYSTEM_READ);
        desc_tbl |= 2;
        cpu_write8(addr, desc_tbl, TLB_SYSTEM_WRITE);
    }

    // Write back state
    cpu.cr[0] |= CR0_TS;
    cpu.seg[SEG_TR] = sel;
    cpu.seg_base[SEG_TR] = base;
    cpu.seg_limit[SEG_TR] = limit;
    cpu.seg_access[SEG_TR] = access & ~2; // Mark as busy
    cpu.seg_valid[SEG_TR] = SEG_VALID_READABLE | SEG_VALID_WRITABLE;

    // Update CR3 if it has changed
    if (tss_type == AVAILABLE_TSS_386 || tss_type == BUSY_TSS_386) {
        if (cr3 != cpu.cr[3]) {
            cpu_prot_set_cr(3, cr3);
        }
    }

    SET_VIRT_EIP(eip);
    int eflags_mask = (tss_type == AVAILABLE_TSS_386 || tss_type == BUSY_TSS_386) ? -1 : 0xFFFF;
    cpu_set_eflags((eflags & eflags_mask) | (cpu.eflags & ~eflags_mask));
    for (int i = 0; i < 8; i++)
        cpu.reg32[i] = reg32[i];
    if (eflags & EFLAGS_VM) {
        for (int i = 0; i < 6; i++)
            cpu_seg_load_virtual(i, seg[i]);
        cpu.cpl = 3;
    } else {
        for (int i = 0; i < 6; i++)
            cpu.seg[i] = seg[i];
        cpu.cpl = seg[CS] & 3;
    }
    for (int i = 0; i < 8; i++)
        cpu.reg32[i] = reg32[i];

    // LDT cannot refer to itself
    if (SELECTOR_LDT(ldt))
        EXCEPTION_TS(offset);
    int ldt_offset = ldt & 0xFFFC;
    if (ldt_offset) {
        struct seg_desc ldt_info;
        if (cpu_seg_load_descriptor2(SEG_GDTR, ldt, &ldt_info, EX_TS, ldt_offset))
            return 1;
        int ldt_access = DESC_ACCESS(&ldt_info);
        if (ACCESS_TYPE(ldt_access) != 2)
            EXCEPTION_TS(ldt_offset);
        if ((ldt_access & ACCESS_P) == 0)
            EXCEPTION_TS(ldt_offset);
        // Reload LDT cache
        cpu.seg[SEG_LDTR] = ldt;
        cpu.seg_base[SEG_LDTR] = cpu_seg_get_base(&ldt_info);
        cpu.seg_limit[SEG_LDTR] = cpu_seg_get_limit(&ldt_info);
        cpu.seg_access[SEG_LDTR] = ldt_access;
    }

    // Load segments

    for (int i = 0; i < 6; i++) {
        int sel = seg[i], sel_offs = seg[i] & 0xFFFC, seg_access;
        struct seg_desc seg_info;
        switch (i) {
        case CS:
        case SS:
            // Segment cannot be NULL
            if (!sel_offs)
                EXCEPTION_TS(0);

            // Load information
            if (cpu_seg_load_descriptor(sel, &seg_info, EX_TS, sel_offs))
                return 1;
            seg_access = DESC_ACCESS(&seg_info);

            // Must be present
            if (!(seg_access & ACCESS_P))
                EXCEPTION_TS(sel_offs);

            switch (ACCESS_TYPE(seg_access)) {
            case 0x12:
            case 0x13:
            case 0x16:
            case 0x17: // Writable Data Segment
                if (i != SS)
                    goto error;
                // RPL and DPL = CPL
                if (cpu.cpl != SELECTOR_RPL(sel) && cpu.cpl != ACCESS_DPL(seg_access))
                    EXCEPTION_TS(sel_offs);
                break;
            case 0x18 ... 0x1B:
                if (i != CS)
                    goto error;
                // Non-Conforming: DPL=CPL or else #TS
                if (ACCESS_DPL(seg_access) != SELECTOR_RPL(sel))
                    EXCEPTION_TS(sel_offs);
                break;
            case 0x1C ... 0x1F:
                if (i != CS)
                    goto error;
                // Non-Conforming: DPL<=CPL or else #TS
                if (ACCESS_DPL(seg_access) > SELECTOR_RPL(sel))
                    EXCEPTION_TS(sel_offs);
                break;
            default:
            error:
                EXCEPTION_TS(sel_offs);
            }
            if (cpu_seg_load_protected(i, sel, &seg_info))
                return 1;
            break;
        default:
            if (!sel_offs) {
                // If selector is null, then ignore and invalidate
                cpu.seg_base[i] = 0;
                cpu.seg_limit[i] = 0;
                cpu.seg_access[i] = 0;
                continue;
            }
            if (cpu_seg_load_descriptor(sel, &seg_info, EX_TS, sel_offs))
                return 1;
            seg_access = DESC_ACCESS(&seg_info);
            // Must be present
            if (!(seg_access & ACCESS_P))
                EXCEPTION_TS(sel_offs);
            switch (ACCESS_TYPE(seg_access)) {
            case 0x10 ... 0x17: // Data Segment
            case 0x1A:
            case 0x1B: { // Readable Code Segment, non-conforming
                // RPL and CPL must be less than or equal to DPL
                int dpl = ACCESS_DPL(seg_access);
                if (dpl < SELECTOR_RPL(sel) || dpl < cpu.cpl)
                    EXCEPTION_TS(sel_offs);
                break;
            }
            case 0x1E:
            case 0x1F: // Readable Code Segments, conforming
                break;
            default:
                EXCEPTION_TS(sel_offs);
            }
            if (cpu_seg_load_protected(i, sel, &seg_info))
                return 1;
            break;
        }
    }
    return 0;
}

int cpu_interrupt(int vector, int error_code, int type, int eip_to_push)
{
    FAST_STACK_INIT;
    if (cpu.cr[0] & CR0_PE) {
        if (cpu.eflags & EFLAGS_VM && type == INTERRUPT_TYPE_SOFTWARE) {
            // Vrtual 8086 Mode interrupt
            if (cpu.cr[4] & CR4_VME) {
                // VME enabled - check interrupt redirection bit in TSS
                uint16_t redirection_map_index, new_eip, new_cs;
                uint8_t redirection_map_entry;
                uint32_t idt_entry;

                // Check if TSS is large enough
                if (cpu.seg_limit[SEG_TR] < 0x67)
                    EXCEPTION_GP(0);

                // Read redirection map index, and get the byte containing the redirection map entry
                cpu_read16(cpu.seg_base[SEG_TR] + 0x66, redirection_map_index, TLB_SYSTEM_READ);
                cpu_read8(redirection_map_index - 1 - ((~vector & 0xFF) >> 3) + cpu.seg_base[SEG_TR], redirection_map_entry, TLB_SYSTEM_READ);

                if (!(redirection_map_entry & (1 << (vector & 7)))) {
                    uint32_t flags_image = cpu_get_eflags();
                    if (get_iopl() < 3) {
                        // Replace IF with VIF
                        flags_image &= ~EFLAGS_IF;
                        flags_image |= (flags_image & EFLAGS_VIF) ? EFLAGS_IF : 0;
                        // Set IOPL to 3
                        flags_image |= EFLAGS_IOPL;
                    }

                    // Read CS:IP from linear address 0.
                    cpu_read32(vector << 2, idt_entry, TLB_SYSTEM_READ);

                    // TODO: Endianness?
                    new_eip = idt_entry & 0xFFFF;
                    new_cs = idt_entry >> 16 & 0xFFFF;

                    // Since we are in VM8086 mode, CPL is always 3 (user mode)
                    init_fast_push(cpu.reg32[ESP], cpu.seg_base[SS], cpu.esp_mask, TLB_USER_WRITE, 0);
                    push16(flags_image);
                    push16(cpu.seg[CS]);
                    push16(eip_to_push);

                    int bit_to_mask_out = get_iopl() == 3 ? EFLAGS_IF : EFLAGS_VIF;
                    cpu.eflags &= ~(bit_to_mask_out | EFLAGS_TF);
                    cpu_load_csip_virtual(new_cs, new_eip);
                    set_esp();
                    return 0;
                }
            }
            // VME disabled or interrupt bitmask not in table
            else if (get_iopl() < 3) {
                EXCEPTION_GP(0);
            }
            // Otherwise, interrupt is serviced using the IDT, convieniently breaking out of vm86 mode too.
        }

        // Check if interrupt is within IDT limits
        int offset = vector << 3, is_hardware = type == INTERRUPT_TYPE_HARDWARE;
#define error_code(o, s) (o) | (s << 1) | (is_hardware)

        // Load IDT entry
        struct seg_desc idt_entry;
        if (cpu_seg_load_descriptor2(SEG_IDTR, offset, &idt_entry, EX_GP, error_code(offset, 1)))
            return 1;
        int idt_access = DESC_ACCESS(&idt_entry);
        int idt_entry_type = ACCESS_TYPE(idt_access);

        switch (idt_entry_type) {
        case TASK_GATE: {
            // Check if entry is present
            if (!(idt_access & ACCESS_P))
                EXCEPTION_NP(error_code(offset, 1));

            // Load task descriptor
            uint16_t tss_selector = cpu_seg_gate_target_segment(&idt_entry),
                     tss_offset = tss_selector & 0xFFFC;
            struct seg_desc tss_entry;
            int tss_access;

            // Must be referencing LDT
            if (SELECTOR_LDT(tss_selector))
                EXCEPTION_TS(error_code(tss_offset, 0));

            // Load TSS from GDT
            if (cpu_seg_load_descriptor(tss_selector, &tss_entry, EX_GP, error_code(tss_offset, 0)))
                return 1;
            tss_access = DESC_ACCESS(&tss_entry);

            // Check if it's busy or not
            if (ACCESS_TYPE(tss_access) & ACCESS_S)
                EXCEPTION_GP(error_code(tss_offset, 0));

            // Check if it's present
            if (!(tss_access & ACCESS_P))
                EXCEPTION_NP(error_code(tss_offset, 0));

            if (do_task_switch(tss_selector, &tss_entry, TASK_INT, eip_to_push))
                return 1;
            if (error_code & EXCEPTION_HAS_ERROR_CODE) {
                error_code &= 0xFFFF;
                init_fast_push(cpu.reg32[ESP], cpu.seg_base[SS], cpu.esp_mask, TLB_SYSTEM_WRITE, -1); // XXX: Is it TLB system write?
                if (ACCESS_TYPE(tss_access) == AVAILABLE_TSS_286 || ACCESS_TYPE(tss_access) == BUSY_TSS_286)
                    push16(error_code);
                else
                    push16(error_code);
            }
            break;
        }
        case INTERRUPT_GATE_286:
        case INTERRUPT_GATE_386:
        case TRAP_GATE_286:
        case TRAP_GATE_386: {
            int dpl = ACCESS_DPL(idt_access);

            // If "int n," then idt.dpl >= cpl or else #GP
            if (type == INTERRUPT_TYPE_SOFTWARE && dpl < cpu.cpl)
                EXCEPTION_GP(error_code(offset, 1));

            // Check if it's present
            if (!(idt_access & ACCESS_P))
                EXCEPTION_NP(error_code(offset, 0));

            // Read gate target segment/offset
            uint32_t cs = cpu_seg_gate_target_segment(&idt_entry), cs_offset = cs & 0xFFFC;
            uint32_t eip = cpu_seg_gate_target_offset(&idt_entry);
            struct seg_desc cs_info;
            int cs_access;

            // Check if CS is a NULL selector
            if (!cs_offset)
                EXCEPTION_GP(error_code(0, 0));

            // Load CS segment information
            if (cpu_seg_load_descriptor(cs, &cs_info, EX_GP, error_code(cs_offset, 0)))
                return 1;

            cs_access = DESC_ACCESS(&cs_info);
            int type = ACCESS_TYPE(cs_access);

            // Check if it's a code segment
            if (!(type >= 0x18 && type <= 0x1F))
                EXCEPTION_GP(error_code(cs_offset, 0));

            dpl = ACCESS_DPL(cs_access);

            // dpl <= cpl or #GP
            if (dpl > cpu.cpl)
                EXCEPTION_GP(error_code(cs_offset, 0));

            // Check if it's present
            if (!(idt_access & ACCESS_P))
                EXCEPTION_NP(error_code(cs_offset, 0));

            int esp, ss, ss_offset;
            struct seg_desc ss_info;

            uint32_t old_esp = cpu.reg32[ESP], old_ss = cpu.seg[SS], esp_mask, ss_base;

            int changed_privilege_level = 0;
            switch (type) {
            case 0x18 ... 0x1B: // Non-conforming
                if (dpl == cpu.cpl)
                    goto conforming; // whoopsie

                // If code segment is non-conforming, then dpl >= cpl or else #GP
                if (!(dpl < cpu.cpl))
                    goto error;

                // ============================
                // INTERRUPT TO INNER PRIVILEGE
                // ============================

                // DPL must be zero in v8086 mode
                if (dpl != 0 && cpu.eflags & EFLAGS_VM)
                    EXCEPTION_GP(error_code(cs_offset, 0));

                // Read stack stuff from TSS
                if (get_tss_esp(dpl, &esp))
                    return 1;
                if (get_tss_ss(dpl, &ss))
                    return 1;
                ss_offset = ss & 0xFFFC;
                changed_privilege_level = 1;

                // SS cannot be null
                if (!ss_offset)
                    EXCEPTION_TS(error_code(ss_offset, 0));

                // Load SS
                if (cpu_seg_load_descriptor(ss, &ss_info, EX_TS, error_code(ss_offset, 0)))
                    return 1;
                int ss_access = DESC_ACCESS(&ss_info);

                // SS.rpl and SS.dpl must be equal to CS.dpl
                if (SELECTOR_RPL(ss) != (unsigned int)dpl || ACCESS_DPL(ss_access) != (unsigned int)dpl)
                    EXCEPTION_TS(error_code(ss_offset, 0));

                int type = ACCESS_TYPE(ss_access);

                // Check if it's writable
                if (!(type == 0x12 || type == 0x13 || type == 0x16 || type == 0x17))
                    EXCEPTION_TS(error_code(ss_offset, 0));

                // Check if it's present
                if (!(ss_access & ACCESS_P))
                    EXCEPTION_SS(error_code(ss_offset, 0)); // Note: Not #NP, like most of the other non-present segments

                // Some important stack variables
                esp_mask = ss_access & ACCESS_B ? -1 : 0xFFFF;
                ss_base = cpu_seg_get_base(&ss_info);

                // Hack to get OS/2 to work
                esp = (esp & esp_mask) | (cpu.reg32[ESP] & ~esp_mask);

                init_fast_push(esp, ss_base, esp_mask, cpl_to_TLB_write[dpl], -1); // Note: dpl is from cs_info

                // Push some important information onto the stack
                if (idt_entry_type & 8) {
                    // 32-bit gate
                    if (cpu.eflags & EFLAGS_VM) {
                        push32(cpu.seg[GS]);
                        push32(cpu.seg[FS]);
                        push32(cpu.seg[DS]);
                        push32(cpu.seg[ES]);

                        //cpu_invalidate_seg(GS);
                        //cpu_invalidate_seg(FS);
                        //cpu_invalidate_seg(DS);
                        //cpu_invalidate_seg(ES);
                        cpu.seg[GS] = 0;
                        cpu.seg_limit[GS] = 0;
                        cpu.seg_base[GS] = 0;
                        cpu.seg_access[GS] = 0;

                        cpu.seg[FS] = 0;
                        cpu.seg_limit[FS] = 0;
                        cpu.seg_base[FS] = 0;
                        cpu.seg_access[FS] = 0;

                        cpu.seg[DS] = 0;
                        cpu.seg_limit[DS] = 0;
                        cpu.seg_base[DS] = 0;
                        cpu.seg_access[DS] = 0;

                        cpu.seg[ES] = 0;
                        cpu.seg_limit[ES] = 0;
                        cpu.seg_base[ES] = 0;
                        cpu.seg_access[ES] = 0;
                    }
                    push32(old_ss);
                    push32(old_esp);
                } else {
                    // 16-bit gate
                    if (cpu.eflags & EFLAGS_VM) {
                        push16(cpu.seg[GS]);
                        push16(cpu.seg[FS]);
                        push16(cpu.seg[DS]);
                        push16(cpu.seg[ES]);

                        //cpu_invalidate_seg(GS);
                        //cpu_invalidate_seg(FS);
                        //cpu_invalidate_seg(DS);
                        //cpu_invalidate_seg(ES);
                        cpu.seg[GS] = 0;
                        cpu.seg_limit[GS] = 0;
                        cpu.seg_base[GS] = 0;
                        cpu.seg_access[GS] = 0;

                        cpu.seg[FS] = 0;
                        cpu.seg_limit[FS] = 0;
                        cpu.seg_base[FS] = 0;
                        cpu.seg_access[FS] = 0;

                        cpu.seg[DS] = 0;
                        cpu.seg_limit[DS] = 0;
                        cpu.seg_base[DS] = 0;
                        cpu.seg_access[DS] = 0;

                        cpu.seg[ES] = 0;
                        cpu.seg_limit[ES] = 0;
                        cpu.seg_base[ES] = 0;
                        cpu.seg_access[ES] = 0;
                    }
                    push16(old_ss);
                    push16(old_esp);
                }
                break;
            case 0x1C ... 0x1F: // Conforming code segment
            conforming:
                // DPL must equal CPL
                if (dpl != cpu.cpl && cpu.eflags & EFLAGS_VM)
                    goto error;
                ss = cpu.seg[SS];
                ss_base = cpu.seg_base[SS];
                esp = cpu.reg32[ESP];
                esp_mask = cpu.esp_mask;
                init_fast_push(esp, ss_base, esp_mask, cpl_to_TLB_write[dpl], -1);
                break;
            default:
            error:
                EXCEPTION_GP(error_code(cs_offset, 0));
                return 1; // so compiler doesn't complain
            }

            // Push the important things to the stack
            if (idt_entry_type & 8) { // 32-bit interrupt gate
                push32(cpu_get_eflags());
                push32(cpu.seg[CS]);
                push32(eip_to_push);
                if (error_code & EXCEPTION_HAS_ERROR_CODE)
                    push32(error_code & 0xFFFF);
            } else { // 16-bit interrupt gate
                push16(cpu_get_eflags());
                push16(cpu.seg[CS]);
                push16(eip_to_push);
                if (error_code & EXCEPTION_HAS_ERROR_CODE)
                    push16(error_code & 0xFFFF);
            }

            set_esp();

            if (changed_privilege_level) {
                // Since we transitioned from privilege levels, update CS/SS to reflect that
                if (cpu_seg_load_protected(SS, (ss & ~3) | dpl, &ss_info))
                    return 1;
                if (cpu_load_csip_protected((cs & ~3) | dpl, &cs_info, eip))
                    return 1;
            } else // Only CS was modified
                if (cpu_load_csip_protected((cs & ~3) | cpu.cpl, &cs_info, eip))
                return 1;

            cpu.eflags &= ~(EFLAGS_TF | EFLAGS_VM | EFLAGS_RF | EFLAGS_NT);
            cpu_prot_update_cpl();

            if (!(idt_entry_type & 1))
                cpu.eflags &= ~EFLAGS_IF;
            break;
        }
        default:
            EXCEPTION_GP(error_code(offset, 1));
        }

        return 0;
    } else {
        init_fast_push(cpu.reg32[ESP], cpu.seg_base[SS], cpu.esp_mask, TLB_SYSTEM_WRITE, 0);
        push16(cpu_get_eflags() & 0xFFFF);
        push16(cpu.seg[CS]);
        push16(eip_to_push);
        set_esp();

        // Now read CS/EIP from IDT
        vector <<= 2;
        uint16_t cs = *(uint16_t *)(cpu.mem + vector + 2),
                 eip = *(uint16_t *)(cpu.mem + vector);
        cpu_load_csip_real(cs, eip);
        cpu.eflags &= ~(EFLAGS_IF | EFLAGS_TF | EFLAGS_AC);
        return 0;
    }
}

static int current_exception = -1;
void cpu_exception(int vec, int code)
{
    while (1) {
        if (current_exception >= 0) {
            if (current_exception == 8) {
                // Triple fault
                CPU_FATAL("TODO: Triple Fault\n");
            }
            current_exception = 8;
            vec = 8;
            code = 0 | EXCEPTION_HAS_ERROR_CODE; // Error code of double fault is always zero
        }
#ifndef EMSCRIPTEN
        CPU_LOG("HALFIX EXCEPTION: %02x(%04x) @ EIP=%08x lin=%08x\n", vec, code, VIRT_EIP(), LIN_EIP());
//if(vec==6)__asm__("int3");
//if(LIN_EIP() == 0x00010063) __asm__("int3");
#endif
        current_exception = vec;
        if (cpu_interrupt(vec, code, INTERRUPT_TYPE_EXCEPTION, VIRT_EIP()))
            current_exception = vec; // Error while handling exception
        else
            break;
    }
    current_exception = -1;
}

int jmpf(uint32_t eip, uint32_t cs, uint32_t eip_after)
{
    if ((cpu.cr[0] & CR0_PE) == 0 || cpu.eflags & EFLAGS_VM) {
        // ==========================
        // Real/Virtual mode far jump
        // ==========================

        // XXX: Differentiate between real mode CS and virtual mode CS?
        cpu_load_csip_real(cs, eip);
    } else {
        // =======================
        // Protected mode far jump
        // =======================
        uint32_t offset = cs & ~3, access;
        int dpl, type, rpl = cs & 3;
        struct seg_desc info;

        // CS cannot be NULL
        if (offset == 0)
            EXCEPTION_GP(0);

        // Load descriptor information, raise #GP(offset) if out of bounds
        if (cpu_seg_load_descriptor(cs, &info, EX_GP, offset))
            return 1;

        access = DESC_ACCESS(&info);

        // Descriptor must be present
        if ((access & ACCESS_P) == 0)
            EXCEPTION_NP(offset);

        dpl = ACCESS_DPL(access);
        type = ACCESS_TYPE(access);
        //printf("%08x %d %x %08x%08x\n", cpu.phys_eip, cpu.state_hash, type, info.raw[1], info.raw[0]);
        switch (type) {
        case 0x18 ... 0x1B: // Non-conforming code segment
            // RPL <= CPL and DPL == CPL, or else #GP
            if (rpl > cpu.cpl || dpl != cpu.cpl)
                EXCEPTION_GP(offset);

            // Jump succeded, load in CS (same privilege) and EIP
            if (cpu_load_csip_protected(offset | cpu.cpl, &info, eip))
                return 1;
            break;
        case 0x1C ... 0x1F: // Conforming code segment
            // DPL <= CPL or else #GP
            if (dpl > cpu.cpl)
                EXCEPTION_GP(offset);

            // Jump succeded, load in CS (same privilege) and EIP
            if (cpu_load_csip_protected(offset | cpu.cpl, &info, eip))
                return 1;
            break;
        case CALL_GATE_286:
        case CALL_GATE_386: { // I like to call these "jump gates"
            uint32_t gate_cs, gate_eip, gate_cs_offset;
            struct seg_desc gate_info;

            // DPL >= CPL and DPL >= RPL, or else #GP
            if (dpl < cpu.cpl || dpl < rpl)
                EXCEPTION_GP(offset);

            // Read gate selector/offset from the descriptor.
            // Note that EIP passed in by the instruction is discarded
            gate_cs = cpu_seg_gate_target_segment(&info);
            gate_eip = cpu_seg_gate_target_offset(&info);
            gate_cs_offset = gate_cs & ~3;

            // Load gate information
            if (cpu_seg_load_descriptor(gate_cs, &gate_info, EX_GP, gate_cs_offset))
                return 1;
            access = DESC_ACCESS(&gate_info);
            dpl = ACCESS_DPL(access);
            switch (ACCESS_TYPE(access)) {
            case 0x1C ... 0x1F: // Conforming code segment
                // DPL <= CPL or else #GP
                if (dpl > cpu.cpl)
                    EXCEPTION_GP(gate_cs_offset);
                break;
            case 0x18 ... 0x1B: // Non-conforming code segment
                // DPL == CPL or else #GP
                if (dpl != cpu.cpl)
                    EXCEPTION_GP(gate_cs_offset);
                break;
            default:
                // Unknown gate type
                CPU_LOG("Unknown desciptor type for 'jump gate': %02x\n", ACCESS_TYPE(access));
                EXCEPTION_GP(gate_cs_offset);
            }

            // Call gate must be present
            if ((access & ACCESS_P) == 0)
                EXCEPTION_NP(gate_cs_offset);

            // Truncate EIP to 16-bits if 16-bit call gate
            gate_eip &= type == CALL_GATE_386 ? -1 : 0xFFFF;
            //printf("Gate EIP: %08x\n", gate_eip);

            // Jump succeded, load in CS (same privilege) and EIP
            if (cpu_load_csip_protected(gate_cs_offset | cpu.cpl, &gate_info, gate_eip))
                return 1;
            break;
        }
        case AVAILABLE_TSS_286:
        case AVAILABLE_TSS_386:
            // DPL >= CPL and DPL > RPL or else #GP
            if (dpl < cpu.cpl || dpl < rpl)
                EXCEPTION_GP(offset);

            // Must be present
            if (!(access & ACCESS_P))
                EXCEPTION_NP(offset);

            if (do_task_switch(cs, &info, TASK_JMP, eip_after))
                return 1;
            break;
        case TASK_GATE: {
            // DPL >= CPL and DPL > RPL or else #GP
            if (dpl < cpu.cpl || dpl < rpl)
                EXCEPTION_GP(offset);

            // Load the TSS from the task gate
            if (load_tss_from_task_gate(&cs, &info))
                return 1;
            if (do_task_switch(cs, &info, TASK_JMP, eip_after))
                return 1;
            break;
        }
        }
    }
    return 0;
}

static uint32_t call_gate_read_param32(uint32_t addr, uint32_t* dest, int mask)
{
    if ((addr + 3) > cpu.seg_limit[SS])
        EXCEPTION_SS(0);
    cpu_read32(addr + cpu.seg_base[SS], *dest, mask);
    return 0;
}
static uint16_t call_gate_read_param16(uint32_t addr, uint32_t* dest, int mask)
{
    if ((addr + 1) > cpu.seg_limit[SS])
        EXCEPTION_SS(0);
    cpu_read16(addr + cpu.seg_base[SS], *dest, mask);
    return 0;
}

int callf(uint32_t eip, uint32_t cs, uint32_t oldeip, int is32)
{
    FAST_STACK_INIT;
    if ((cpu.cr[0] & CR0_PE) && !(cpu.eflags & EFLAGS_VM)) {
        cs &= 0xFFFF;
        int cs_offset = cs & 0xFFFC, cs_access, cs_type, cs_dpl, cs_rpl;

        // CS must be non-null
        if (!cs_offset)
            EXCEPTION_GP(0);

        // Load descriptor and validate
        struct seg_desc cs_info;
        if (cpu_seg_load_descriptor(cs, &cs_info, EX_GP, cs_offset))
            return 1;
        cs_access = DESC_ACCESS(&cs_info);

        // Must be present
        if ((cs_access & ACCESS_P) == 0)
            EXCEPTION_NP(cs_offset);

        cs_type = ACCESS_TYPE(cs_access);
        cs_dpl = ACCESS_DPL(cs_access);
        cs_rpl = SELECTOR_RPL(cs);

        switch (cs_type) {
        case 0x1C ... 0x1F: // Conforming code segment
            // CS.dpl must be <= cpl
            if (cs_dpl > cpu.cpl)
                EXCEPTION_GP(cs_offset);
            break;
        case 0x18 ... 0x1B: // Conforming code segment
            // RPL must be <= CPL and CPL must be equal to dpl
            if (cs_rpl > cpu.cpl || cs_dpl != cpu.cpl)
                EXCEPTION_GP(cs_offset);
            break;
        case CALL_GATE_286:
        case CALL_GATE_386: { // Call gate
            // DPL must be >= CPL and >= RPL
            if (cs_dpl < cpu.cpl || cs_dpl < cs_rpl)
                EXCEPTION_GP(cs_offset);

            uint32_t gate_cs = cpu_seg_gate_target_segment(&cs_info), gate_cs_offset = gate_cs & 0xFFFC,
                     gate_eip = cpu_seg_gate_target_offset(&cs_info);
            int gate_dpl, dpldiff, gate_type, gate_access;
            struct seg_desc gate_info;

            // Gate cannot be NULL
            if (!gate_cs_offset)
                EXCEPTION_GP(0);

            // Load and parse descriptor
            if (cpu_seg_load_descriptor(gate_cs, &gate_info, EX_GP, gate_cs_offset))
                return 1;

            gate_access = DESC_ACCESS(&gate_info);
            gate_dpl = ACCESS_DPL(gate_access);
            gate_type = ACCESS_TYPE(gate_access);
            dpldiff = gate_dpl - cpu.cpl; // Will be < 0 if DPL<CPL, =0 if DPL=CPL, or >0 if DPL>CPL
            switch (gate_type) {
            case 0x18 ... 0x1B: // Non-conforming code segment
                if (dpldiff < 0) {
                    // Call gate to more privilege
                    int ss, esp, ss_offset, ss_access, ss_type, ss_base, ss_mask;
                    struct seg_desc ss_info;

                    // Load SS:ESP from TSS
                    if (get_tss_ss(gate_dpl, &ss))
                        return 1;
                    if (get_tss_esp(gate_dpl, &esp))
                        return 1;
                    ss_offset = ss & 0xFFFC;

                    // SS cannot be NULL
                    if (!ss_offset)
                        EXCEPTION_TS(0);
                    if (cpu_seg_load_descriptor(ss, &ss_info, EX_TS, ss_offset))
                        return 1;
                    ss_access = DESC_ACCESS(&ss_info);
                    ss_type = ACCESS_TYPE(ss_access);

                    // SS.dpl must be equal to gate.dpl
                    if ((unsigned int)gate_dpl != ACCESS_DPL(ss_access))
                        EXCEPTION_TS(ss_offset);

                    // Must be a writable data segment
                    if (!(ss_type == 0x12 || ss_type == 0x13 || ss_type == 0x16 || ss_type == 0x17))
                        EXCEPTION_TS(ss_offset);

                    // Must be present
                    if ((ss_access & ACCESS_P) == 0)
                        EXCEPTION_TS(ss_offset);

                    // Finished validating SS, all ok
                    // Now push all required values to new stack
                    // We need to be careful here because gate_dpl might be different than cpu.cpl (OS/2 uses this feature)
                    int parameter_count = cpu_seg_gate_parameter_count(&cs_info);
                    uint32_t* params = alloca(parameter_count * 4);

                    ss_base = cpu_seg_get_base(&ss_info);
                    ss_mask = ss_access & ACCESS_B ? -1 : 0xFFFF;

                    // ESP must be masked according to the stack mask, or else OS/2 doesn't boot.
                    uint32_t old_esp = cpu.reg32[ESP] & cpu.esp_mask;

                    // Load params
                    for (int i = parameter_count - 1, j = 0; i >= 0; i--, j++) {
                        if (cs_type == CALL_GATE_386) {
                            if (call_gate_read_param32(((old_esp + (i << 2)) & cpu.esp_mask), &params[j], gate_dpl))
                                return 1;
                        } else {
                            if (call_gate_read_param16(((old_esp + (i << 1)) & cpu.esp_mask), &params[j], gate_dpl))
                                return 1;
                        }
                    }

                    // Push old SS, old ESP, parameters, old CS, and old EIP

                    // Hack to get OS/2 to work
                    esp = (esp & ss_mask) | (cpu.reg32[ESP] & ~ss_mask);

                    init_fast_push(esp, ss_base, ss_mask, cpl_to_TLB_write[gate_dpl], is32);
                    if (cs_type == CALL_GATE_386) {
                        push32(cpu.seg[SS]);
                        push32(old_esp);
                        for (int i = 0; i < parameter_count; i++) {
                            push32(params[i]);
                        }
                        push32(cpu.seg[CS]);
                        push32(oldeip);
                    } else {
                        push16(cpu.seg[SS]);
                        push16(old_esp);
                        for (int i = 0; i < parameter_count; i++) {
                            push16(params[i]);
                        }
                        push16(cpu.seg[CS]);
                        push16(oldeip);
                    }
                    // Load segments and set EIP
                    if (cpu_seg_load_protected(SS, (ss & ~3) | gate_dpl, &ss_info))
                        return 1;
                    set_esp();
                    if (cpu_load_csip_protected((gate_cs & ~3) | gate_dpl, &gate_info, gate_eip))
                        return 1;
                    return 0;
                } else if (dpldiff > 0) // DPL > CPL
                    EXCEPTION_GP(gate_cs_offset);
                else // Work around GCC warning about fallthrough cases
                    goto __workaround_gcc;
            // Otherwise, DPL == CPL -- fallthrough to conforming code segment
            case 0x1C ... 0x1F: // Conforming code segment
__workaround_gcc:
                init_fast_push(cpu.reg32[ESP], cpu.seg_base[SS], cpu.esp_mask, cpu.tlb_shift_write, is32);
                if (cs_type == CALL_GATE_386) {
                    push32(cpu.seg[CS]);
                    push32(oldeip);
                } else {
                    push16(cpu.seg[CS]);
                    push16(oldeip);
                }
                if (cpu_load_csip_protected((gate_cs & ~3) | cpu.cpl, &gate_info, gate_eip))
                    return 1;
                set_esp();
                return 0;
            default:
                EXCEPTION_GP(gate_cs_offset);
            }
            ABORT(); // Unreachable
            break; // even more unreachable, but keep gcc happy
        }
        case AVAILABLE_TSS_286:
        case AVAILABLE_TSS_386:
            // dpl must be >= cpl and rpl
            if (cs_dpl < cpu.cpl || cs_dpl < cs_rpl)
                EXCEPTION_GP(cs_offset);
            do_task_switch(cs, &cs_info, TASK_CALL, eip);
            return 0;
        case TASK_GATE:
            // DPL >= CPL and DPL > RPL or else #GP
            if (cs_dpl < cpu.cpl || cs_dpl < cs_rpl)
                EXCEPTION_GP(cs_offset);

            // Load the TSS from the task gate
            if (load_tss_from_task_gate(&cs, &cs_info))
                return 1;
            if (do_task_switch(cs, &cs_info, TASK_JMP, eip))
                return 1;
            return 0;
        default:
            EXCEPTION_GP(cs_offset);
        }
        // Conforming/non conforming code segment handled here
        init_fast_push(cpu.reg32[ESP], cpu.seg_base[SS], cpu.esp_mask, cpu.tlb_shift_write, is32);
        if (is32) {
            push32(cpu.seg[CS]);
            push32(oldeip);
        } else {
            push16(cpu.seg[CS]);
            push16(oldeip);
        }
        if (cpu_load_csip_protected((cs & ~3) | cpu.cpl, &cs_info, eip))
            return 1;
        set_esp();
        return 0;
    } else {
        // Real mode far call
        init_fast_push(cpu.reg32[ESP], cpu.seg_base[SS], cpu.esp_mask, cpu.tlb_shift_write, is32);
        // Push CS and EIP
        if (is32) {
            push32(cpu.seg[CS]);
            push32(oldeip);
        } else {
            push16(cpu.seg[CS]);
            push16(oldeip);
        }
        set_esp();
        if (cpu.cr[0] & CR0_PE) // VM86 mode
            cpu_load_csip_virtual(cs, eip);
        else // Real mode
            cpu_load_csip_real(cs, eip);
        return 0;
    }
}

static void iret_handle_seg(int x)
{
    uint16_t access = cpu.seg_access[x];
    int invalid = 0, type = ACCESS_TYPE(access);
    if ((cpu.seg[x] & 0xFFFC) == 0)
        invalid = 1;
    else if (cpu.cpl > ACCESS_DPL(access)) {
        switch (type) {
        case 0x1C ... 0x1F: // Conforming code
        case 0x10 ... 0x17: // Data
            invalid = 1;
            break;
        }
    }
    if (invalid) {
        // Mark as NULL and invalid
        cpu.seg[x] = 0;
        cpu.seg_access[x] = 0;
        cpu.seg_base[x] = 0;
        cpu.seg_limit[x] = 0;
        cpu.seg_valid[x] = 0;
    }
}

// Handles all interrupt returns
int iret(uint32_t tss_eip, int is32)
{
    FAST_STACK_INIT;
    uint32_t eip = 0, cs = 0, eflags = 0;

    if (cpu.cr[0] & CR0_PE) {
        init_fast_push(cpu.reg32[ESP], cpu.seg_base[SS], cpu.esp_mask, cpu.tlb_shift_write, is32);
        if (cpu.eflags & EFLAGS_VM) {
            // Virtual 8086 Mode iret
            if (get_iopl() == 3) {
                // All of the EFLAGS bits that won't be modified.
                int eflags_mask;
                if (is32) {
                    pop32(eip);
                    pop32(cs);
                    pop32(eflags);
                    eflags_mask = EFLAGS_VM | EFLAGS_IOPL | EFLAGS_VIP | EFLAGS_VIF;
                } else {
                    pop16(eip);
                    pop16(cs);
                    pop16(eflags);
                    eflags_mask = EFLAGS_IOPL | 0xFFFF0000;
                }
                set_esp();

                cpu_load_csip_virtual(cs, eip);
                cpu_set_eflags((eflags & ~eflags_mask) | (cpu.eflags & eflags_mask));
            } else { // iopl < 3
                if (cpu.cr[4] & CR4_VME) {
                    if (is32)
                        CPU_FATAL("TOFO: 32-bit VME IRET\n");
                    pop16(eip);
                    pop16(cs);
                    pop16(eflags);
                    if ((cpu.eflags & EFLAGS_VIP && eflags & (1 << 9)) || eflags & (1 << 8))
                        EXCEPTION_GP(0);

                    set_esp();
                    cpu_load_csip_virtual(cs, eip);

                    const uint32_t mask = 0xFFFF ^ (EFLAGS_IOPL | EFLAGS_IF);
                    if (eflags & EFLAGS_IF) // Replace IF with VIF
                        cpu.eflags |= EFLAGS_VIF;
                    else
                        cpu.eflags &= ~EFLAGS_VIF;
                    cpu_set_eflags((eflags & mask) | (cpu.eflags & ~mask));
                } else // VME disabled
                    EXCEPTION_GP(0);
            }
        } else {
            if (cpu.eflags & EFLAGS_NT) { // Nested task bit set in EFLAGS
                // Read back-link to TSS and use that to switch TSS
                uint16_t tss_back_link, tss_offset;
                struct seg_desc tss_info;

                // Read back link from TSS
                cpu_read16(cpu.seg_base[SEG_TR], tss_back_link, TLB_SYSTEM_READ);
                tss_offset = tss_back_link & 0xFFFC;

                // Must be in GDT
                if (SELECTOR_LDT(tss_back_link))
                    EXCEPTION_TS(tss_back_link);

                // Load TSS information
                if (cpu_seg_load_descriptor2(SEG_GDTR, tss_back_link, &tss_info, EX_TS, tss_offset))
                    return 1;

                int access = DESC_ACCESS(&tss_info), type = ACCESS_TYPE(access);

                // Cannot be busy
                if (type == BUSY_TSS_286 || type == BUSY_TSS_386)
                    EXCEPTION_TS(tss_offset);

                return do_task_switch(tss_offset, &tss_info, TASK_IRET, tss_eip);
            } else {
                int old_cpl = cpu.cpl; // We use the old CPL to determine which flags to load.

                uint32_t cs_offset;
                struct seg_desc cs_info;
                uint32_t eflags_mask = is32 ? -1 : 0xFFFF;

                init_fast_push(cpu.reg32[ESP], cpu.seg_base[SS], cpu.esp_mask, cpu.tlb_shift_write, is32);
                if (is32) {
                    pop32(eip);
                    pop32(cs);
                    pop32(eflags);
                    cs &= 0xFFFF;

                    if (eflags & EFLAGS_VM && cpu.cpl == 0) {
                        // IRET to Virtual 8086 Mode
                        uint32_t esp, ss, es, ds, fs, gs;
                        pop32(esp);
                        pop32(ss);
                        pop32(es);
                        pop32(ds);
                        pop32(fs);
                        pop32(gs);

                        //set_esp();
                        cpu_seg_load_virtual(ES, es);
                        cpu_seg_load_virtual(DS, ds);
                        cpu_seg_load_virtual(FS, fs);
                        cpu_seg_load_virtual(GS, gs);
                        //cpu_seg_load_virtual(CS, cs);
                        cpu_seg_load_virtual(SS, ss);
                        cpu_load_csip_virtual(cs, eip & 0xFFFF);
                        cpu.reg32[ESP] = esp;

                        // Modify VM flag
                        cpu_set_eflags((eflags & eflags_mask) | (cpu.eflags & ~eflags_mask)); // ??? is this right ???

                        cpu.cpl = 3;
                        cpu_prot_update_cpl();
                        return 0;
                    }
                } else {
                    pop16(eip);
                    pop16(cs);
                    pop16(eflags);
                }
                cs_offset = cs & 0xFFFC;

                // CS cannot be NULL
                if (!cs_offset)
                    EXCEPTION_GP(0);

                if (cpu_seg_load_descriptor(cs, &cs_info, EX_GP, cs_offset))
                    return 1;

                int access = DESC_ACCESS(&cs_info), dpl = ACCESS_DPL(access), rpl = SELECTOR_RPL(cs);

                // RPL >= CPL
                if (rpl < cpu.cpl)
                    EXCEPTION_GP(cs_offset);

                switch (ACCESS_TYPE(access)) {
                case 0x18 ... 0x1B: // Non-conforming
                    if (dpl != rpl)
                        EXCEPTION_GP(cs_offset);
                    break;
                case 0x1C ... 0x1F: // Conforming
                    if (dpl > rpl)
                        EXCEPTION_GP(cs_offset);
                    break;
                default:
                    EXCEPTION_GP(cs_offset);
                }

                // Check if present
                if ((access & ACCESS_P) == 0)
                    EXCEPTION_NP(cs_offset);

                if (rpl != cpu.cpl) {
                    // IRET to outer level
                    uint32_t esp = 0, ss = 0, ss_offset, esp_mask;
                    int ss_access, ss_type, ss_dpl;
                    struct seg_desc ss_info;

                    if (is32) {
                        pop32(esp);
                        pop32(ss);
                        ss &= 0xFFFF;
                    } else {
                        pop16(esp);
                        pop16(ss);
                    }
                    ss_offset = ss & 0xFFFC;

                    // SS cannot be NULL
                    if (!ss_offset)
                        EXCEPTION_GP(0);

                    // Load selector
                    if (cpu_seg_load_descriptor(ss, &ss_info, EX_GP, ss_offset))
                        return 1;

                    // SS.rpl == CS.rpl
                    if (SELECTOR_RPL(ss) != (unsigned int)rpl)
                        EXCEPTION_GP(ss_offset);

                    // SS must be writable data segment
                    ss_access = DESC_ACCESS(&ss_info);
                    ss_type = ACCESS_TYPE(ss_access);
                    ss_dpl = ACCESS_DPL(ss_access);
                    esp_mask = ss_access & ACCESS_B ? -1 : 0xFFFF;
                    if (!(ss_type == 0x12 || ss_type == 0x13 || ss_type == 0x16 || ss_type == 0x17))
                        EXCEPTION_GP(ss_offset);

                    // SS.dpl == CS.rpl
                    if (ss_dpl != rpl)
                        EXCEPTION_GP(ss_offset);

                    // Must be present
                    if ((ss_access & ACCESS_P) == 0)
                        EXCEPTION_NP(cs_offset);

                    // Load segment
                    if (cpu_seg_load_protected(SS, ss, &ss_info))
                        return 1;
                    if (cpu_load_csip_protected(cs, &cs_info, eip))
                        return 1;
                    cpu.reg32[ESP] = (esp & esp_mask) | (cpu.reg32[ESP] & ~esp_mask);

                    iret_handle_seg(ES);
                    iret_handle_seg(FS);
                    iret_handle_seg(GS);
                    iret_handle_seg(DS);
                } else { // Return to same privilege
                    if (cpu_load_csip_protected(cs, &cs_info, eip))
                        return 1;
                    set_esp();
                }
                // No more exceptions after this point, all flags have been loaded

                uint32_t flag_mask = EFLAGS_CF | EFLAGS_PF | EFLAGS_AF | EFLAGS_ZF | EFLAGS_SF | EFLAGS_TF | EFLAGS_DF | EFLAGS_OF | EFLAGS_NT | EFLAGS_RF | EFLAGS_AC | EFLAGS_ID;
                // Adjust flag mask as needed
                if ((unsigned int)old_cpl <= get_iopl())
                    flag_mask |= EFLAGS_IF;
                if (old_cpl == 0)
                    flag_mask |= EFLAGS_IOPL | EFLAGS_VIF | EFLAGS_VIP;
                //printf("%d v %d [new flags: %08x current: %08x mask: %08x]\n", cpu.cpl, cpu.seg[CS], eflags, cpu.eflags, flag_mask);

                if (!is32)
                    flag_mask &= 0xFFFF; // Limit to 16 bits
                cpu_set_eflags((eflags & flag_mask) | (cpu.eflags & ~flag_mask));
            }
        }
        return 0;
    } else { // Real mode
        init_fast_push(cpu.reg32[ESP], cpu.seg_base[SS], cpu.esp_mask, cpu.tlb_shift_write, is32);
        if (is32) {
            pop32(eip);
            pop32(cs);
            pop32(eflags);
        } else {
            pop16(eip);
            pop16(cs);
            pop16(eflags);
        }
        set_esp();

        cpu_load_csip_real(cs, eip);
        if (is32)
            cpu_set_eflags((eflags & 0x257FD5) | (cpu.eflags & 0x1A0000));
        else
            cpu_set_eflags(eflags | (cpu.eflags & ~0xFFFF));
        return 0;
    }
}

// Far return handler
int retf(int adjust, int is32)
{
    FAST_STACK_INIT;
    uint32_t eip = 0, cs = 0;
    if ((cpu.cr[0] & CR0_PE) == 0 || (cpu.eflags & EFLAGS_VM)) {
        // Real mode far return
        init_fast_push(cpu.reg32[ESP], cpu.seg_base[SS], cpu.esp_mask, cpu.tlb_shift_write, is32);
        // Pop CS and EIP
        if (is32) {
            pop32(eip);
            pop32(cs);
        } else {
            pop16(eip);
            pop16(cs);
        }
        if (VIRT_EIP() >= cpu.seg_limit[CS])
            EXCEPTION_GP(0);
        modify_esp(adjust);
        set_esp();
        //if(cs == 0xa82) __asm__("int3");
        if (cpu.cr[0] & CR0_PE) // VM86 mode
            cpu_load_csip_virtual(cs, eip);
        else // Real mode
            cpu_load_csip_real(cs, eip);
        return 0;
    } else {
        init_fast_push(cpu.reg32[ESP], cpu.seg_base[SS], cpu.esp_mask, cpu.tlb_shift_write, is32);
        if (is32) {
            pop32(eip);
            pop32(cs);
        } else {
            pop16(eip);
            pop16(cs);
        }
        cs &= 0xFFFF;
        uint32_t cs_offset = cs & 0xFFFC;
        int access, rpl, dpl;
        struct seg_desc cs_info;

        // New CS cannot be NULL
        if (!cs_offset)
            EXCEPTION_GP(0);

        // Load descriptor
        if (cpu_seg_load_descriptor(cs, &cs_info, EX_GP, cs_offset))
            return 1;
        access = DESC_ACCESS(&cs_info);
        rpl = SELECTOR_RPL(cs);
        dpl = ACCESS_DPL(access);

        // CS.rpl must be >= CPL
        if (rpl < cpu.cpl)
            EXCEPTION_GP(cs_offset);

        switch (ACCESS_TYPE(access)) {
        case 0x18 ... 0x1B: // Non-comforning
            if (dpl != rpl)
                EXCEPTION_GP(cs_offset);
            break;
        case 0x1C ... 0x1F: // Non-comforning
            if (dpl > rpl)
                EXCEPTION_GP(cs_offset);
            break;
        default:
            EXCEPTION_GP(cs_offset);
        }

        // Must be present
        if ((access & ACCESS_P) == 0)
            EXCEPTION_NP(cs_offset);
        if (rpl > cpu.cpl) {
            int ss_access, ss_rpl, ss_dpl, ss_type;
            // Return to outer privilege level
            uint32_t new_ss = 0, new_esp = 0, new_ss_offset, esp_mask;

            // Add stack offsets
            modify_esp(adjust);

            if (is32) {
                pop32(new_esp);
                pop32(new_ss);
                new_ss &= 0xFFFF;
            } else {
                pop16(new_esp);
                pop16(new_ss);
            }
            new_ss_offset = new_ss & 0xFFFC;

            // New SS cannot be null
            if (!new_ss_offset)
                EXCEPTION_GP(new_ss_offset);

            // Load segment
            struct seg_desc ss_info;
            if (cpu_seg_load_descriptor(new_ss, &ss_info, EX_GP, new_ss_offset))
                return 1;
            ss_access = DESC_ACCESS(&ss_info);
            ss_dpl = ACCESS_DPL(ss_access);
            ss_rpl = SELECTOR_RPL(new_ss);
            ss_type = ACCESS_TYPE(ss_access);

            // Must be writable data segment, and SS.rpl must be equal to CS.rpl and SS.dpl must be equal to CS.rpl
            if (!(ss_type == 0x12 || ss_type == 0x13 || ss_type == 0x16 || ss_type == 0x17) || ss_rpl != rpl || ss_dpl != rpl) {
                EXCEPTION_GP(new_ss_offset);
            }

            // Must be present
            if ((ss_access & ACCESS_P) == 0)
                EXCEPTION_NP(new_ss_offset);

            // Finalize everything
            if (cpu_seg_load_protected(SS, new_ss, &ss_info))
                return 1;
            if (cpu_load_csip_protected(cs, &cs_info, eip))
                return 1;

            // At this point, we cannot fault
            esp_mask = ss_access & ACCESS_B ? -1 : 0xFFFF;
            //modify_esp(adjust);
            //set_esp();
            cpu.reg32[ESP] = ((new_esp + adjust) & esp_mask) | (cpu.reg32[ESP] & ~esp_mask);
        } else {
            // Return to same privilege
            if (cpu_load_csip_protected(cs, &cs_info, eip))
                return 1;
            modify_esp(adjust);
            set_esp();
        }
        return 0;
    }
}

#define SYSENTER_CS 0
#define SYSENTER_ESP 1
#define SYSENTER_EIP 2

static void reload_cs_base(void)
{
    // For sysenter/sysexit, virt_eip == lin_eip
    uint32_t virt_eip = VIRT_EIP();
    uint32_t lin_page = virt_eip >> 12,
             shift = cpu.tlb_shift_read,
             tag = cpu.tlb_tags[virt_eip >> 12] >> shift;
    if (tag & 2) {
        cpu.last_phys_eip = cpu.phys_eip + 0x1000;
        return;
    }
    cpu.phys_eip = PTR_TO_PHYS(cpu.tlb[lin_page] + virt_eip);
    cpu.last_phys_eip = cpu.phys_eip & ~0xFFF;
    cpu.eip_phys_bias = virt_eip - cpu.phys_eip;
}

// Sysenter
int sysenter(void)
{
    uint32_t cs = cpu.sysenter[SYSENTER_CS],
             cs_offset = cs & 0xFFFC;
    if ((cpu.cr[0] & CR0_PE) == 0 || cs_offset == 0)
        EXCEPTION_GP(0);

    cpu.eflags &= ~(EFLAGS_IF | EFLAGS_VM);

    SET_VIRT_EIP(cpu.sysenter[SYSENTER_EIP]);
    cpu.reg32[ESP] = cpu.sysenter[SYSENTER_ESP];
    cpu.seg[CS] = cs_offset;
    cpu.seg_base[CS] = 0;
    cpu.seg_limit[CS] = -1;
    cpu.seg_access[CS] = ACCESS_S | 0x0B | ACCESS_P | ACCESS_G; // 32-bit, r/x code, accessed, present, 4kb granularity
    cpu.cpl = 0;
    cpu_prot_update_cpl();
    cpu.state_hash = 0; // 32-bit code/data

    cpu.seg[SS] = (cs_offset + 8) & 0xFFFC;
    cpu.seg_base[SS] = 0;
    cpu.seg_limit[SS] = -1;
    cpu.seg_access[SS] = ACCESS_S | 0x03 | ACCESS_P | ACCESS_G | ACCESS_B; // 32-bit, r/x data, accessed, present, 4kb granularity, 32-bit
    cpu.esp_mask = -1;

    reload_cs_base();
    return 0;
}

int sysexit(void)
{
    uint32_t cs = cpu.sysenter[SYSENTER_CS],
             cs_offset = cs & 0xFFFC;
    if ((cpu.cr[0] & CR0_PE) == 0 || cs_offset == 0 || cpu.cpl != 0)
        EXCEPTION_GP(0);

    SET_VIRT_EIP(cpu.reg32[EDX]);
    cpu.reg32[ESP] = cpu.reg32[ECX];
    cpu.seg[CS] = (cpu.sysenter[SYSENTER_CS] | 3) + 16;
    cpu.seg_base[CS] = 0;
    cpu.seg_limit[CS] = -1;
    cpu.seg_access[CS] = ACCESS_S | 0x0B | ACCESS_P | ACCESS_G | ACCESS_DPL_MASK; // 32-bit, r/x code, accessed, present, 4kb granularity, dpl=3
    cpu.cpl = 3;
    cpu_prot_update_cpl();
    cpu.state_hash = 0; // 32-bit code/data

    cpu.seg[SS] = (cpu.sysenter[SYSENTER_CS] | 3) + 24;
    cpu.seg_base[SS] = 0;
    cpu.seg_limit[SS] = -1;
    cpu.seg_access[SS] = ACCESS_S | 0x03 | ACCESS_P | ACCESS_G | ACCESS_B | ACCESS_DPL_MASK; // 32-bit, r/x data, accessed, present, 4kb granularity, 32-bit, dpl=3
    cpu.esp_mask = -1;

    reload_cs_base();
    return 0;
}