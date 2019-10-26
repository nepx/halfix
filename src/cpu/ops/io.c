// I/O port routines. These are the bare I/O functions; instructions in interpreter.c and string.c will call these functions
#include "io.h"
#include "cpu/cpu.h"
#ifdef INSTRUMENT
#include "cpu/instrument.h"
#endif

#define EXCEPTION_HANDLER return 1

int cpu_io_check_access(uint32_t port, int size)
{
    if ((cpu.cr[0] & CR0_PE) && ((cpu.eflags & EFLAGS_VM) || (unsigned int)cpu.cpl > get_iopl())) {
        uint16_t tss = cpu.seg[SEG_TR];
        struct seg_desc tss_info;
        int tss_access, tss_type;
        if (cpu_seg_load_descriptor(tss, &tss_info, EX_GP, 0))
            return 1;

        tss_access = DESC_ACCESS(&tss_info);
        tss_type = ACCESS_TYPE(tss_access);

        // TSS must be 16-bit
        if (tss_type != AVAILABLE_TSS_386 && tss_type != BUSY_TSS_386)
            EXCEPTION_GP(0);

        // Find base/limit of TR and find the I/O offset
        uint32_t base = cpu.seg_base[SEG_TR], limit = cpu.seg_limit[SEG_TR];
        if (limit < 0x67)
            EXCEPTION_GP(0); // Cannot access IO permissions bitmap

        uint32_t io_offset;
        cpu_read16(base + 0x66, io_offset, TLB_SYSTEM_READ);

        // Check if we are able to read this.
        if (limit < (io_offset + ((port + size) >> 3)))
            EXCEPTION_GP(0);

        int mask = ((size << 1) - 1) << (port & 7);
        uint16_t bitmask;
        cpu_read16(base + io_offset + (port >> 3), bitmask, TLB_SYSTEM_READ);
        int valid = (bitmask & mask) == 0;
        if (!valid)
            EXCEPTION_GP(0);
    }
    return 0;
}

void cpu_outb(uint32_t port, uint32_t data)
{
#ifdef INSTRUMENT
    cpu_instrument_io_write(port, data, 1);
#endif
    io_writeb(port, data);
}
void cpu_outw(uint32_t port, uint32_t data)
{
#ifdef INSTRUMENT
    cpu_instrument_io_write(port, data, 2);
#endif
    io_writew(port, data);
}
void cpu_outd(uint32_t port, uint32_t data)
{
#ifdef INSTRUMENT
    cpu_instrument_io_write(port, data, 4);
#endif
    io_writed(port, data);
}

uint32_t cpu_inb(uint32_t port)
{
    uint8_t result = io_readb(port);
#ifdef INSTRUMENT
    cpu_instrument_io_read(port, result, 1);
#endif
    return result;
}
uint32_t cpu_inw(uint32_t port)
{
    uint16_t result = io_readw(port);
#ifdef INSTRUMENT
    cpu_instrument_io_read(port, result, 2);
#endif
    return result;
}
uint32_t cpu_ind(uint32_t port)
{
    uint32_t result = io_readd(port);
#ifdef INSTRUMENT
    cpu_instrument_io_read(port, result, 4);
#endif
    return result;
}