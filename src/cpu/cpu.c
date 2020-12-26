// Main CPU emulator entry point

#include "cpu/cpu.h"
#include "cpu/fpu.h"
#include "cpu/instrument.h"
#include "cpuapi.h"
#include "devices.h"
#include <string.h>

struct cpu cpu;

void cpu_set_a20(int a20_enabled)
{
    uint32_t old_a20_mask = cpu.a20_mask;

    cpu.a20_mask = -1 ^ (!a20_enabled << 20);
    if (old_a20_mask != cpu.a20_mask)
        cpu_mmu_tlb_flush(); // Only clear TLB if A20 gate has changed

#ifdef INSTRUMENT
    cpu_instrument_set_a20(a20_enabled);
#endif
}

int cpu_init_mem(int size)
{
    cpu.mem = calloc(1, size);
    memset(cpu.mem + 0xC0000, -1, 0x40000);
    cpu.memory_size = size;

    cpu.smc_has_code_length = (size + 4095) >> 12;
    cpu.smc_has_code = calloc(4, cpu.smc_has_code_length);

// It's possible that instrumentation callbacks will need a physical pointer to RAM
#ifdef INSTRUMENT
    cpu_instrument_init_mem();
#endif
    return 0;
}
int cpu_interrupts_masked(void)
{
    return cpu.eflags & EFLAGS_IF;
}

itick_t cpu_get_cycles(void)
{
    return cpu.cycles + (cpu.cycle_offset - cpu.cycles_to_run);
}

// Execute main CPU interpreter
int cpu_run(int cycles)
{
    // Reset state
    cpu.cycle_offset = cycles;
    cpu.cycles_to_run = cycles;
    cpu.refill_counter = 0;
    cpu.hlt_counter = 0;

    uint64_t begin = cpu_get_cycles();

    while (1) {
        // Check for interrupts
        if (cpu.intr_line_state) {
            // Check for validity
            if (cpu.eflags & EFLAGS_IF && !cpu.interrupts_blocked) {
                int interrupt_id = pic_get_interrupt();
                cpu_interrupt(interrupt_id, 0, INTERRUPT_TYPE_HARDWARE, VIRT_EIP());
#ifdef INSTRUMENT
                cpu_instrument_hardware_interrupt(interrupt_id);
#endif
                cpu.exit_reason = EXIT_STATUS_NORMAL;
            }
        }

        // Don't continue executing if we are in a hlt state
        if (cpu.exit_reason == EXIT_STATUS_HLT)
            return 0;

        if (cpu.interrupts_blocked) {
            // Run one instruction
            cpu.refill_counter = cycles;
            cpu.cycles += cpu_get_cycles() - cpu.cycles;
            cpu.cycles_to_run = 1;
            cpu.cycle_offset = 1;
            cpu.interrupts_blocked = 0;
        }

        // Reset state as needed
        cpu_execute();

        // Move cycles forward
        cpu.cycles += cpu_get_cycles() - cpu.cycles;

        cpu.cycles_to_run = cpu.refill_counter;
        cpu.refill_counter = 0;
        cpu.cycle_offset = cpu.cycles_to_run;

        if (!cpu.cycles_to_run)
            break;
    }

    // We are here for the following three reasons:
    //  - HLT raised
    //  - A device requested a fast return
    //  - We have run "cycles" operations.
    // In the case of the former, cpu.hlt_counter will contain the number of cycles still in cpu.cycles_to_run
    int cycles_run = cpu_get_cycles() - begin;
    cpu.cycle_offset = 0;
    return cycles_run;
}

void cpu_raise_intr_line(void)
{
    cpu.intr_line_state = 1;
#ifdef INSTRUMENT
    cpu_instrument_set_intr_line(1, 0);
#endif
}
void cpu_lower_intr_line(void)
{
    cpu.intr_line_state = 0;
#ifdef INSTRUMENT
    cpu_instrument_set_intr_line(0, 0);
#endif
}
void cpu_request_fast_return(int reason)
{
    UNUSED(reason);
    INTERNAL_CPU_LOOP_EXIT();
}
void cpu_cancel_execution_cycle(int reason)
{
    // We want to exit out of the loop entirely
    cpu.exit_reason = reason;
    cpu.cycles += cpu_get_cycles() - cpu.cycles;
    cpu.cycles_to_run = 1;
    cpu.cycle_offset = 1;
    cpu.refill_counter = 0;
}

void* cpu_get_ram_ptr(void)
{
    return cpu.mem;
}

int cpu_add_rom(int addr, int size, void* data)
{
    if ((uint32_t)addr > cpu.memory_size || (uint32_t)(addr + size) > cpu.memory_size)
        return 0;
    memcpy(cpu.mem + addr, data, size);
    return 0;
}

int cpu_get_exit_reason(void)
{
    return cpu.exit_reason;
}

// Tells the CPU to stop execution after it's been running a little bit. Does nothing in this case.
void cpu_set_break(void) {}

// Resets CPU
void cpu_reset(void)
{
    for (int i = 0; i < 8; i++) {
        // Clear general purpose registers
        cpu.reg32[i] = 0;

        // Set control registers
        if (i == 0)
            cpu.cr[0] = 0x60000010;
        else
            cpu.cr[i] = 0;

        // Set segment registers
        if (i == CS)
            cpu_seg_load_real(CS, 0xF000);
        else
            cpu_seg_load_real(i, 0);

        if (i >= 6)
            cpu.dr[i] = (i == 6) ? 0xFFFF0FF0 : 0x400;
        else
            cpu.dr[i] = 0;
    }
    // Set EIP
    SET_VIRT_EIP(0xFFF0);

    // set CPL
    cpu.cpl = 0;
    cpu_prot_update_cpl();

    // Clear EFLAGS except for reserved bits
    cpu.eflags = 2;

    cpu.page_attribute_tables = 0x0007040600070406LL;

    // Reset APIC MSR, if APIC is enabled
    if (apic_is_enabled())
        cpu.apic_base = 0xFEE00900; // We are BSP
    else
        cpu.apic_base = 0;

    cpu.mxcsr = 0x1F80;
    cpu_update_mxcsr();

    // Reset TLB
    memset(cpu.tlb, 0, sizeof(void*) * (1 << 20));
    memset(cpu.tlb_tags, 0xFF, 1 << 20);
    memset(cpu.tlb_attrs, 0xFF, 1 << 20);
    cpu_mmu_tlb_flush();
}

int cpu_apic_connected(void)
{
    return apic_is_enabled() && (cpu.apic_base & 0x100);
}

static void cpu_state(void)
{
#ifndef LIBCPU
    // <<< BEGIN AUTOGENERATE "state" >>>
    struct bjson_object* obj = state_obj("cpu", 44);
    state_field(obj, 64, "cpu.reg32", &cpu.reg32);
    state_field(obj, 128, "cpu.xmm32", &cpu.xmm32);
    state_field(obj, 4, "cpu.mxcsr", &cpu.mxcsr);
    state_field(obj, 4, "cpu.esp_mask", &cpu.esp_mask);
    state_field(obj, 4, "cpu.memory_size", &cpu.memory_size);
    state_field(obj, 4, "cpu.eflags", &cpu.eflags);
    state_field(obj, 4, "cpu.laux", &cpu.laux);
    state_field(obj, 4, "cpu.lop1", &cpu.lop1);
    state_field(obj, 4, "cpu.lop2", &cpu.lop2);
    state_field(obj, 4, "cpu.lr", &cpu.lr);
    state_field(obj, 4, "cpu.phys_eip", &cpu.phys_eip);
    state_field(obj, 4, "cpu.last_phys_eip", &cpu.last_phys_eip);
    state_field(obj, 4, "cpu.eip_phys_bias", &cpu.eip_phys_bias);
    state_field(obj, 4, "cpu.state_hash", &cpu.state_hash);
    state_field(obj, 8, "cpu.cycles", &cpu.cycles);
    state_field(obj, 8, "cpu.cycle_frame_end", &cpu.cycle_frame_end);
    state_field(obj, 4, "cpu.cycles_to_run", &cpu.cycles_to_run);
    state_field(obj, 4, "cpu.refill_counter", &cpu.refill_counter);
    state_field(obj, 4, "cpu.hlt_counter", &cpu.hlt_counter);
    state_field(obj, 4, "cpu.cycle_offset", &cpu.cycle_offset);
    state_field(obj, 32, "cpu.cr", &cpu.cr);
    state_field(obj, 32, "cpu.dr", &cpu.dr);
    state_field(obj, 4, "cpu.cpl", &cpu.cpl);
    state_field(obj, 32, "cpu.seg", &cpu.seg);
    state_field(obj, 64, "cpu.seg_base", &cpu.seg_base);
    state_field(obj, 64, "cpu.seg_limit", &cpu.seg_limit);
    state_field(obj, 64, "cpu.seg_access", &cpu.seg_access);
    state_field(obj, 64, "cpu.seg_valid", &cpu.seg_valid);
    state_field(obj, 4, "cpu.trace_cache_usage", &cpu.trace_cache_usage);
    state_field(obj, 4, "cpu.tlb_shift_read", &cpu.tlb_shift_read);
    state_field(obj, 4, "cpu.tlb_shift_write", &cpu.tlb_shift_write);
    state_field(obj, 256, "cpu.mtrr_fixed", &cpu.mtrr_fixed);
    state_field(obj, 128, "cpu.mtrr_variable_addr_mask", &cpu.mtrr_variable_addr_mask);
    state_field(obj, 8, "cpu.mtrr_deftype", &cpu.mtrr_deftype);
    state_field(obj, 8, "cpu.page_attribute_tables", &cpu.page_attribute_tables);
    state_field(obj, 4, "cpu.a20_mask", &cpu.a20_mask);
    state_field(obj, 8, "cpu.apic_base", &cpu.apic_base);
    state_field(obj, 8, "cpu.tsc_fudge", &cpu.tsc_fudge);
    state_field(obj, 4, "cpu.read_result", &cpu.read_result);
    state_field(obj, 4, "cpu.intr_line_state", &cpu.intr_line_state);
    state_field(obj, 4, "cpu.interrupts_blocked", &cpu.interrupts_blocked);
    state_field(obj, 4, "cpu.exit_reason", &cpu.exit_reason);
    state_field(obj, 8, "cpu.ia32_efer", &cpu.ia32_efer);
    state_field(obj, 12, "cpu.sysenter", &cpu.sysenter);
    // <<< END AUTOGENERATE "state" >>>
    state_file(cpu.memory_size, "ram", cpu.mem);

    if (state_is_reading()) {
        cpu_trace_flush(); // Remove all residual code traces
        cpu_mmu_tlb_flush(); // Remove all stale TLB entries
        cpu_prot_update_cpl(); // Update cpu.tlb_shift_*
        cpu_update_mxcsr();

        // The following line doesn't work with OS/2, which assumes that the base/limit/access of all segmentation registers are stored in the cache.
        // In many cases, OS/2 loads the base/limit/access into the segment register (updating the cache) and then modifies descriptors in memorys.
        //for(int i=0;i<6;i++) cpu_load_seg_value_mov(i, cpu.seg[i]);
    }
#endif
}

// Initializes CPU
int cpu_init(void)
{
    state_register(cpu_state);
    io_register_reset(cpu_reset);
    fpu_init();
#ifdef INSTRUMENT
    cpu_instrument_init();
#endif
    return 0;
}

void cpu_init_dma(uint32_t page)
{
    cpu_smc_invalidate_page(page);
}

void cpu_write_mem(uint32_t addr, void* data, uint32_t length)
{
    if (length <= 4) {
        switch (length) {
        case 1:
            cpu.mem8[addr] = *(uint8_t*)data;
#ifdef INSTRUMENT
            cpu_instrument_dma(addr, data, 1);
#endif
            return;
        case 2:
            cpu.mem16[addr >> 1] = *(uint16_t*)data;
#ifdef INSTRUMENT
            cpu_instrument_dma(addr, data, 2);
#endif
            return;
        case 4:
            cpu.mem32[addr >> 2] = *(uint32_t*)data;
#ifdef INSTRUMENT
            cpu_instrument_dma(addr, data, 4);
#endif
            return;
        }
    }
    memcpy(cpu.mem + addr, data, length);
#ifdef INSTRUMENT
    cpu_instrument_dma(addr, data, length);
#endif
}

void cpu_debug(void)
{
    printf("EAX: %08x ECX: %08x EDX: %08x EBX: %08x\n", cpu.reg32[EAX], cpu.reg32[ECX], cpu.reg32[EDX], cpu.reg32[EBX]);
    printf("ESP: %08x EBP: %08x ESI: %08x EDI: %08x\n", cpu.reg32[ESP], cpu.reg32[EBP], cpu.reg32[ESI], cpu.reg32[EDI]);
    printf("EFLAGS: %08x\n", cpu_get_eflags());
    printf("CS:EIP: %04x:%08x (lin: %08x) Physical EIP: %08x\n", cpu.seg[CS], VIRT_EIP(), LIN_EIP(), cpu.phys_eip);
    printf("Translation mode: %d-bit\n", cpu.state_hash ? 16 : 32);
    printf("Physical RAM base: %p Cycles to run: %d Cycles executed: %d\n", cpu.mem, cpu.cycles_to_run, (uint32_t)cpu_get_cycles());
}