#include "io.h"
#include "cpuapi.h"
#include "util.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef SO_BUILD
uint32_t ioport_in;
#endif

//#define LOG_ALL_IO

#define IO_LOG(x, ...) LOG("I/O", x, ##__VA_ARGS__)
//#define IO_LOG(x, ...) printf(x, ##__VA_ARGS__)

static io_read read[0x10000][3];
static io_write write[0x10000][3];

// Default I/O handlers
uint32_t io_default_readb(uint32_t port)
{
    UNUSED(port);
    IO_LOG("readb: port=0x%04x\n", port);
    //abort();
    return-1;
}
uint32_t io_default_readw(uint32_t port)
{
    uint16_t res = io_readb(port);
    return res | io_readb(port + 1) << 8;
}
uint32_t io_default_readd(uint32_t port)
{
    uint32_t res = io_readb(port);
    res |= io_readb(port + 1) << 8;
    res |= io_readb(port + 2) << 16;
    return res | io_readb(port + 3) << 24;
}
void io_default_writeb(uint32_t port, uint32_t data)
{
    UNUSED(port);
    UNUSED(data);
    IO_LOG("default writeb: port=0x%04x, data=0x%02x\n", port, data);
    //abort();
    return;
}
void io_default_writew(uint32_t port, uint32_t data)
{
    IO_LOG("Using default writew: port=0x%04x, data=0x%04x\n", port, data);
    io_writeb(port, data);
    io_writeb(port + 1, data >> 8);
}
void io_default_writed(uint32_t port, uint32_t data)
{
    IO_LOG("Using default writed: port=0x%04x, data=0x%08x\n", port, data);
    io_writeb(port, data);
    io_writeb(port + 1, data >> 8);
    io_writeb(port + 2, data >> 16);
    io_writeb(port + 3, data >> 24);
}

void io_register_read(int port, int length, io_read b, io_read w, io_read d)
{
    b = b ? b : io_default_readb;
    w = w ? w : io_default_readw;
    d = d ? d : io_default_readd;
    for (int i = 0; i < length; i++) {
        read[(port + i) & 65535][0] = b;
        read[(port + i) & 65535][1] = w;
        read[(port + i) & 65535][2] = d;
    }
}
void io_register_write(int port, int length, io_write b, io_write w, io_write d)
{
    b = b ? b : io_default_writeb;
    w = w ? w : io_default_writew;
    d = d ? d : io_default_writed;
    for (int i = 0; i < length; i++) {
        write[(port + i) & 65535][0] = b;
        write[(port + i) & 65535][1] = w;
        write[(port + i) & 65535][2] = d;
    }
}

void io_unregister_read(int port, int length){
    for (int i = 0; i < length; i++) {
        read[(port + i) & 65535][0] = io_default_readb;
        read[(port + i) & 65535][1] = io_default_readw;
        read[(port + i) & 65535][2] = io_default_readd;
    }
}
void io_unregister_write(int port, int length){
    for (int i = 0; i < length; i++) {
        write[(port + i) & 65535][0] = io_default_writeb;
        write[(port + i) & 65535][1] = io_default_writew;
        write[(port + i) & 65535][2] = io_default_writed;
    }
}

#define MAX_RESETS 15
static io_reset resets[MAX_RESETS];
static int io_reset_ptr = 0;
void io_register_reset(io_reset cb)
{
    if (io_reset_ptr == MAX_RESETS) {
        IO_LOG("Too many I/O reset callbacks registered\n");
        abort();
    }
    resets[io_reset_ptr++] = cb;
}
void io_trigger_reset(void)
{
    for (int i = 0; i < io_reset_ptr; i++) {
        resets[i]();
    }
}

uint8_t io_readb(uint32_t port)
{
#ifdef SO_BUILD
    ioport_in = port;
#endif
    uint8_t data = read[port & 0xFFFF][0](port);
    //cpu_io_read(port, data, 1);
#ifndef LOG_ALL_IO
    if(port != 0x1F7 && port != 0x92 && port != 0x3c9 && (port & ~1) != 0x70 && port != 0x1F0)
#endif 
    IO_LOG("readb: port=0x%04x res=0x%02x\n", port, data);
    return data;
}
uint16_t io_readw(uint32_t port)
{
#ifdef SO_BUILD
    ioport_in = port;
#endif
    uint16_t data = read[port & 0xFFFF][1](port);
    //cpu_io_read(port, data, 2);
#ifndef LOG_ALL_IO
    if(port != 0x1F0)
#endif
    IO_LOG("readw: port=0x%04x res=0x%04x\n", port, data);
    return data;
}
uint32_t io_readd(uint32_t port)
{
#ifdef SO_BUILD
    ioport_in = port;
#endif
    uint32_t data = read[port & 0xFFFF][2](port);
    //cpu_io_read(port, data, 4);
#ifndef LOG_ALL_IO
    if(port != 0x1F0)
#endif
    IO_LOG("readd: port=0x%04x res=0x%08x\n", port, data);
    return data;
}
void io_writeb(uint32_t port, uint8_t data)
{
#ifndef LOG_ALL_IO
    if(/*((port & ~1) != 0x3D4) && */(port != 0x3C9) && (port != 0x3C7) && (port != 0x3C8) && port != 0x500 && (port & ~1) != 0x70 && port != 0x80) // Ignore accesses to CRT registers because programs do too much of this
#endif
        IO_LOG("writeb: port=0x%04x data=0x%02x\n", port, data);
    //cpu_io_write(port, 1);
    write[port & 0xFFFF][0](port, data);
}
void io_writew(uint32_t port, uint16_t data)
{
    if(port != 0x1F0)
    IO_LOG("writew: port=0x%04x data=0x%04x\n", port, data);
    //cpu_io_write(port, 2);
    write[port & 0xFFFF][1](port, data);
}
void io_writed(uint32_t port, uint32_t data)
{
    //cpu_io_write(port, 4);
#ifndef LOG_ALL_IO
    if(port != 0x1F0)
    IO_LOG("writed: port=0x%04x data=0x%08x\n", port, data);
#endif
    write[port & 0xFFFF][2](port, data);
}

static void io_default_mmio_writeb(uint32_t addr, uint32_t data)
{
    UNUSED(data | addr);
    IO_LOG("Unhandled MMIO writeb: %08x\n", addr);
    //abort();
    return;
}
// XXX: Increase performance
static void io_default_mmio_writew(uint32_t addr, uint32_t data)
{
    //IO_LOG("Default writew to 0x%x: %04x\n", addr, data);
    io_handle_mmio_write(addr, data & 0xFF, 0);
    io_handle_mmio_write(addr + 1, data >> 8 & 0xFF, 0);
}
static void io_default_mmio_writed(uint32_t addr, uint32_t data)
{
    //IO_LOG("Default writed to 0x%x: %08x\n", addr, data);
    io_handle_mmio_write(addr, data & 0xFF, 0);
    io_handle_mmio_write(addr + 1, data >> 8 & 0xFF, 0);
    io_handle_mmio_write(addr + 2, data >> 16 & 0xFF, 0);
    io_handle_mmio_write(addr + 3, data >> 24 & 0xFF, 0);
}
static uint32_t io_default_mmio_readb(uint32_t addr)
{
    //IO_LOG("Unhandled MMIO readb: %08x\n", addr);
    //abort();
    UNUSED(addr);
    return -1;
}
// XXX: Increase performance
static uint32_t io_default_mmio_readw(uint32_t addr)
{
    uint16_t result = io_handle_mmio_read(addr, 0);
    return result | io_handle_mmio_read(addr + 1, 0) << 8;
}
static uint32_t io_default_mmio_readd(uint32_t addr)
{
    uint32_t result = io_handle_mmio_read(addr, 0);
    result |= io_handle_mmio_read(addr + 1, 0) << 8;
    result |= io_handle_mmio_read(addr + 2, 0) << 16;
    return result | io_handle_mmio_read(addr + 3, 0) << 24;
}

// Memory mapped locations are so few and far in between that it's ok to do it the simple way:
#define MAX_MMIO 10
struct mmio {
    io_read r[3];
    io_write w[3];
    uint32_t begin, end;
};

static struct mmio mmio[MAX_MMIO + 1];
static int mmio_pos[2] = { 0, 0 },
           tf = 0; // Ugly hack, but necessary
void io_register_mmio_read(uint32_t start, uint32_t length, io_read b, io_read w, io_read d)
{
    if (tf && mmio_pos[0] == MAX_MMIO) {
        IO_LOG("Too many read areas\n");
        abort();
    }
    mmio[mmio_pos[0]].begin = start;
    mmio[mmio_pos[0]].end = start + length;
    mmio[mmio_pos[0]].r[0] = b ? b : io_default_mmio_readb;
    mmio[mmio_pos[0]].r[1] = w ? w : io_default_mmio_readw;
    mmio[mmio_pos[0]].r[2] = d ? d : io_default_mmio_readd;

    mmio_pos[0]++;
}
void io_register_mmio_write(uint32_t start, uint32_t length, io_write b, io_write w, io_write d)
{
    if (tf && mmio_pos[1] == MAX_MMIO) {
        IO_LOG("Too many write areas\n");
        abort();
    }

    mmio[mmio_pos[1]].begin = start;
    mmio[mmio_pos[1]].end = start + length;
    mmio[mmio_pos[1]].w[0] = b ? b : io_default_mmio_writeb;
    mmio[mmio_pos[1]].w[1] = w ? w : io_default_mmio_writew;
    mmio[mmio_pos[1]].w[2] = d ? d : io_default_mmio_writed;

    mmio_pos[1]++;
}
void io_remap_mmio_read(uint32_t oldstart, uint32_t newstart){
    for(int i=0;i<MAX_MMIO;i++){
        if(mmio[i].begin == oldstart){
            mmio[i].begin = newstart;
            mmio[i].end = (mmio[i].end - oldstart) + newstart;
            return;
        }
    }
    IO_LOG("Unable to remap MMIO range at %08x to %08x\n", oldstart, newstart);
}

void io_handle_mmio_write(uint32_t addr, uint32_t data, int size)
{
    //if(addr == 0x004abc95) __asm__("int3");
    for (int i = 0; i <= MAX_MMIO; i++) {
        if (addr >= mmio[i].begin && mmio[i].end >= addr) {
            //printf("'%c' %08x %08x %08x %d %d size: %d\n", data, mmio[i].begin, addr, mmio[i].end, addr >= mmio[i].begin, addr < mmio[i].end, size);
            mmio[i].w[size](addr, data);
            return;
        }
    }
    abort();
}
uint32_t io_handle_mmio_read(uint32_t addr, int size)
{
    for (int i = 0; i <= MAX_MMIO; i++) {
        if (addr >= mmio[i].begin && mmio[i].end >= addr) {
            uint32_t res = mmio[i].r[size](addr); 
            //printf("%08x\n", res);
            return res;
        }
    }
    IO_LOG(" ??? should not be here ??? Unknown mmio read: %08x\n", addr);
    abort();
}

// Checks if address is mmapped for reading
int io_addr_mmio_read(uint32_t addr){
    for (int i = 0; i <= MAX_MMIO; i++) {
        if (addr >= mmio[i].begin && mmio[i].end >= addr) {
            return mmio[i].begin != 0;
        }
    }
    return 0;
}

void io_init(void)
{
    io_register_read(0, 65536, NULL, NULL, NULL);
    io_register_write(0, 65536, NULL, NULL, NULL);
    tf = 0;
    for (int i = 0; i < (MAX_MMIO + 1); i++) {
        io_register_mmio_read(0, -1, NULL, NULL, NULL); // Cover an address space from 0 ... 0xFFFFFFFF
        io_register_mmio_write(0, -1, NULL, NULL, NULL);
    }
    mmio_pos[0] = 0;
    mmio_pos[1] = 0;
    tf = 1;
}