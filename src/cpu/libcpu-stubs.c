// Stubs for libcpu to use
#include <stdint.h>
#include <stdlib.h>

#define UNUSED(x) (void)(x)

// You may want to do other things before aborting.
// In Halfix, it releases the mouse from SDL before aborting, which makes debugging in GDB infinitely easier.
void util_abort(void)
{
    abort();
}

// Convert a pointer p to a physical address that the code cache can work with
uint32_t cpulib_ptr_to_phys(void* p)
{
    UNUSED(p);
    abort();
}

// Set to 1 if the APIC is enabled.
int apic_is_enabled(void)
{
    return 1;
}

// Returns the interrupt sitting on the PIC.
uint8_t pic_get_interrupt(void)
{
    return -1;
}

// You probably want to build your own state serializer.
void state_register(void* s)
{
    UNUSED(s);
}

// Since you're only using the CPU, you can call cpu_reset directly
void io_register_reset(void* cb)
{
    UNUSED(cb);
}

// Convert a physical RAM address to a pointer. The result is strongly recommended but not required to be 4,096 byte aligned.
// The minimum alignment is 16 bytes for various SSE operations.
void* get_phys_ram_ptr(uint32_t addr, int write)
{
    UNUSED(addr | write);
    abort();
}

// Given a linear address, convert to a physical address (useful for process emulators and mmap).
// This lets you emulate paging without actually setting up CR3 and page tables.
// The pipeline goes like this:
/*
void* ptr = get_lin_ram_ptr();
if (ptr != NULL)
    // Address is valid and will be cached in the TLB
else {
    if (fault)
        // Trigger page fault
    else 
        // Do physical address translation, with CR3 yadda yadda
}
*/
// Essentially, if this routine returns non-null, then it's assumed that paging succeeded
// If the routine returns null, then *fault is checked.
// You MUST set *fault to a value if returning NULL. It isn't initialized.
void* get_lin_ram_ptr(uint32_t addr, int flags, int* fault)
{
    *fault = 0;
    UNUSED(addr | flags);
    UNUSED(fault);
    abort();
}

// Handle a MMIO read. Possible sizes are 0 (byte), 1 (word), and 2 (dword).
// This function is called every time the following conditions are met:
/*
 - The access is out of bounds (i.e. you write to to address 0x400001 when you only have 0x400000 bytes of memory).
   This handily captures all higher-half MMIO accesses
 - The access is a READ between 0xA0000 and 0xBFFFF, inclusive
 - The access is a WRITE between 0xA0000 and 0xFFFFF, inclusive
*/
// addr will contain a physical address. Data may or may not be truncated to the proper size. Possible values of sizes will be 0, 1 and 2
void io_handle_mmio_write(uint32_t addr, uint32_t data, int size)
{
    UNUSED(addr | data | size);
}

// Remember to truncate this value before returning it. So if the emulator requests size=1 (a word), don't give it 0xFFFF1234, for instance.
uint32_t io_handle_mmio_read(uint32_t addr, int size)
{
    UNUSED(addr | size);
    return 0;
}

// Returns an 8-bit value from a port. Serves as the backend of the IN instruction.
// This function is only triggered when the I/O port is actually read. If it doesn't succeed (i.e. Virtual 8086 Mode, IOPL, etc.), then this function isn't called.
// If you want something called every time the IN instruction is called (regardless of success), either use instrumentation or play around with IOPL.
// For INSB/INSW/INSD, the appropriate io_readb function will be called over and over again.
uint8_t io_readb(uint32_t port)
{
    return port & 0;
}
uint16_t io_readw(uint32_t port)
{
    return port & 0;
}
uint32_t io_readd(uint32_t port)
{
    return port & 0;
}

// Writes a value to a port, the backend of the OUT instruction. This value is likely truncated properly, but don't take my word for it.
// Again, it's only called on success.
void io_writeb(uint32_t port, uint8_t data)
{
    UNUSED(port | data);
}
void io_writew(uint32_t port, uint16_t data)
{
    UNUSED(port | data);
}
void io_writed(uint32_t port, uint32_t data)
{
    UNUSED(port | data);
}

// Raises an IRQ line. Only used by the FPU in case of a legacy exception.
// If the FPU handles an exception the normal way, this routine WILL NOT be called
void pic_raise_irq(int line)
{
    UNUSED(line);
}
void pic_lower_irq(int line)
{
    UNUSED(line);
}

#ifdef NO_LIBM

// Used purely for debugging purposes only. It's useful when poking through FPU internals but not in production code
double pow(double a, double b)
{
    return a + b;
}

#endif