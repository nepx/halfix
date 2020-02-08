#ifndef INSTRUMENT_H
#define INSTRUMENT_H

#include <stdint.h>

// Called when a region of ROM has its permissions changed from readonly to read/write or vice versa.  
void cpu_instrument_memory_permissions_changed(uint32_t addr, int access_bits);

// According to Intel, the updating of the dirty/accessed bits on page tables is "implementation defined."
// This function is called whenever the page directory/table entries are modified
void cpu_instrument_paging_modified(uint32_t page_directory_entry_addr);

// Called when memory has been initialized. This is for when you need to get the physical address of the memory
void cpu_instrument_init_mem(void);

// Called when CPU has been initialized. Dynamically allocated fields in cpu may not be initialized yet. 
void cpu_instrument_init(void);

// Called when the CPU runs another instruction
void cpu_instrument_execute(void);

// Called when the CPU writes a data to an I/O port
void cpu_instrument_io_write(uint32_t addr, uint32_t data, int size);

// Called when the CPU reads a data from an I/O port
void cpu_instrument_io_read(uint32_t addr, uint32_t data, int size);

// Called when the CPU reads/writes a MSR.
void cpu_instrument_access_msr(int index, uint32_t high, uint32_t low, int writing);

// Called when the CPU raises a hardware interrupt. "vector" holds value of interrupt raised, not IRQ
void cpu_instrument_hardware_interrupt(int vector);

// Called when value of A20 
void cpu_instrument_set_a20(int newvalue);

// Called when the CPU's interrupt line changes
void cpu_instrument_set_intr_line(int value, int _internal);

// Called when RDTSC is executed
void cpu_instrument_rdtsc(uint32_t eax, uint32_t edx);

// Called before a FPU operation is called.
void cpu_instrument_pre_fpu(void);

// Called when a DMA operation modifies memory
void cpu_instrument_dma(uint32_t addr, void* data, uint32_t length);

// Called when the TLB gets full and needs to be refilled
void cpu_instrument_tlb_full(void);

// Called when a SSE instruction approximates the result (RCPPS, etc.). 
// This is because this result is not exactly specified in the Intel documentation, so it's a potential source of non-determinism.
void cpu_instrument_approximate_sse(int dest, int dwords);

#endif