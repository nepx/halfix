#ifndef LIBCPU_H
#define LIBCPU_H
// Standalone header that can be included to control the Halfix CPU emulator
#include <stdint.h>

typedef uint32_t (*io_read_handler)(uint32_t address);
typedef void (*io_write_handler)(uint32_t address, uint32_t data);
typedef uint32_t (*mmio_read_handler)(uint32_t address, int size);
typedef void (*mmio_write_handler)(uint32_t address, uint32_t data, int size);
typedef void (*abort_handler)(void);
typedef void* (*mem_refill_handler)(uint32_t address, int write);
typedef uint32_t (*ptr_to_phys_handler)(void* ptr);

// Register a handler that will be invoked every time the CPU tries to read from a memory-mapped area.
// Memory mapped areas are defined as any memory regions above the physical memory range or between 0xA0000 <= x < 0x100000
void cpu_register_mmio_read_cb(mmio_read_handler h);

// Register a handler that will be invoked every time the CPU tries to write to a memory-mapped area.
void cpu_register_mmio_write_cb(mmio_write_handler h);

// Register a handler that will be invoked every time the CPU tries to read from an I/O port
// The "size" parameter can be one of three values -- 8, 16, or 32, for byte, word, and doubleword accesses, respectively
void cpu_register_io_read_cb(io_read_handler h, int size);

// Register a handler that will be invoked every time the CPU tries to write to an I/O port
void cpu_register_io_write_cb(io_write_handler h, int size);

// Register onabort handler
void cpu_register_onabort(abort_handler h);
// Register a handler that will be invoked when the CPU acknowledges an IRQ from the PIC/APIC
void cpu_register_pic_ack(abort_handler h);
// Register a handler that will be invoked when the FPU wishes to send IRQ13 to the CPU.
void cpu_register_fpu_irq(abort_handler h);
// Register a handler to provide the emulator with pages of physical memory
void cpu_register_mem_refill_handler(mem_refill_handler h);
// Register a handler to provide the emulator with a way to convert pointers to physical addresses
void cpu_register_ptr_to_phys(ptr_to_phys_handler h);
// Register a handler to provide the emulator with pages of linear memory (useful for simulation of mmap in user mode)
void cpu_register_lin_refill_handler(mem_refill_handler h);

// Enable/disable APIC. Note that you will still have to provide the implementation; all it does is report that the CPU has an APIC in CPUID.
void cpu_enable_apic(int enabled);

// Raise the CPU IRQ line and put the following IRQ on the bus
void cpu_raise_irq_line(int irq);
// Lower the CPU IRQ line
void cpu_lower_irq_line(void);

// Run the CPU
int cpu_core_run(int cycles);

// Get a pointer to a bit of CPU state
void* cpu_get_state_ptr(int id);
// Get CPU state
uint32_t cpu_get_state(int id);
// Set a bit of CPU state. Returns a non-zero value if an exception occurred.
int cpu_set_state(int id, uint32_t data);

// Initialize CPU
void libcpu_init(void);

// Shortcut to initialize the CPU to 32-bit flat protected mode.
void cpu_init_32bit(void);

// Shortcut to enable and initialize the FPU
void cpu_init_fpu(void);

enum {
    // Registers
    CPUPTR_GPR,
    CPUPTR_XMM,
    CPUPTR_MXCSR,

    // EFLAGS, EIP are not here since they must be accessed specially
    // CPL is not here since state_hash must be updated

    // Segment descriptor caches. These should read-only, but if you're daring enough then you can write them
    // If you want to set the registers, than run cpu_set_seg()
    CPUPTR_SEG_DESC,
    CPUPTR_SEG_LIMIT,
    CPUPTR_SEG_BASE,
    CPUPTR_SEG_ACCESS,

    // Memory type range registers (if you ever need them)
    CPUPTR_MTRR_FIXED,
    CPUPTR_MTRR_VARIABLE,
    CPUPTR_MTRR_DEFTYPE,
    CPUPTR_PAT,

    // APIC base MSR
    CPUPTR_APIC_BASE,
    CPUPTR_SYSENTER_INFO
};

enum {
    CPU_EFLAGS,
    CPU_EIP,
    CPU_LINEIP,
    CPU_PHYSEIP,
    CPU_CPL,
    CPU_SEG,
    CPU_STATE_HASH,

#define MAKE_CR_ID(id) (id << 4) | (CPU_CR)
    CPU_CR,

// Use this macro to pass a descriptor to cpu_set_state
#define MAKE_SEG_ID(id) (id << 4) | (CPU_SEG)
    CPU_A20
};

#endif