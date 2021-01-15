// Intel 8237 DMA controller with some limited 82374 compatibility
// https://pdos.csail.mit.edu/6.828/2014/readings/hardware/8237A.pdf
// https://docs.freebsd.org/doc/3.4-RELEASE/usr/share/doc/handbook/dma.html
// http://www.brokenthorn.com/Resources/OSDev21.html

// TODO: Timing?

#include "cpuapi.h"
#include "devices.h"
#include "io.h"
#include "state.h"
#include "util.h"
#include <stdint.h>
#include <stdlib.h>

#define DMA_LOG(x, ...) LOG("DMA", x, ##__VA_ARGS__)
#define DMA_FATAL(x, ...)          \
    do {                           \
        DMA_LOG(x, ##__VA_ARGS__); \
        ABORT();                   \
    } while (0)

#define DMA_FLIPFLOP_HIBYTE 1
#define DMA_FLIPFLOP_LOBYTE 0

enum { // See: Transfer Type
    DMA_MODE_INVALID = 0,
    DMA_MODE_SINGLE = 1,
    DMA_MODE_MULTIPLE = 2,
    DMA_MODE_ALSO_INVALID = 3
};

struct dma_controller {
    // <<< BEGIN STRUCT "struct" >>>
    // Per channel registers
    uint32_t addr_high[8]; // High part of physical address for DMA controller
    uint16_t start_addr[8];
    uint16_t current_addr[8];
    uint16_t start_count[8];
    uint16_t current_count[8];
    uint8_t mode[8];

    // Per controller registers
    uint8_t command[2];
    uint8_t status[2];
    uint8_t request[2];
    uint8_t mask[2];

    uint8_t flipflop[2];
    //dma_transfer transfer[8];
    // <<< END STRUCT "struct" >>>
};
static struct dma_controller dma;

static void dma_state(void)
{
    // <<< BEGIN AUTOGENERATE "state" >>>
    struct bjson_object* obj = state_obj("dma", 11);
    state_field(obj, 32, "dma.addr_high", &dma.addr_high);
    state_field(obj, 16, "dma.start_addr", &dma.start_addr);
    state_field(obj, 16, "dma.current_addr", &dma.current_addr);
    state_field(obj, 16, "dma.start_count", &dma.start_count);
    state_field(obj, 16, "dma.current_count", &dma.current_count);
    state_field(obj, 8, "dma.mode", &dma.mode);
    state_field(obj, 2, "dma.command", &dma.command);
    state_field(obj, 2, "dma.status", &dma.status);
    state_field(obj, 2, "dma.request", &dma.request);
    state_field(obj, 2, "dma.mask", &dma.mask);
    state_field(obj, 2, "dma.flipflop", &dma.flipflop);
// <<< END AUTOGENERATE "state" >>>
}

static void dma_run_transfers(void);

// Write a flipflop state. Returns new value
static inline void dma_flipflop_write(uint16_t* orig, uint8_t data, uint8_t* flipflop, int modify)
{
    int shift = *flipflop << 3;

    *orig &= 0xFF << (shift ^ 8);
    *orig |= data << shift;
    *flipflop ^= modify;
}
// Reads a flipflop state
static inline uint8_t dma_flipflop_read(uint16_t data, uint8_t* flipflop, int modify)
{
    uint8_t result = data >> (*flipflop << 3);
    *flipflop ^= modify;
    return result;
}

static int page_register_offsets[] = {
    /*  0  1  2  3  4  5  6  7 */
    8, 2, 3, 1, 8, 8, 8, 0,
    /*  8  9  A  B  C  D  E  F */
    8, 6, 7, 5, 8, 8, 8, 4
};

static uint32_t dma_io_readb(uint32_t port)
{
    int offset = port >= 0xC0;
    switch (port) {
    case 0x00:
    case 0xC0: // Read Channel 0/4 current address
    case 0x02:
    case 0xC4: // Read Channel 1/5 current address
    case 0x04:
    case 0xC8: // Read Channel 2/6 current address
    case 0x06:
    case 0xCC: // Read Channel 3/7 current address
        return dma_flipflop_read(dma.current_addr[((port >> offset & 3) | (offset << 2)) & 7], dma.flipflop + offset, 1);
    case 0x01:
    case 0xC2: // Read Channel 0/4 remaining count
    case 0x03:
    case 0xC6: // Read Channel 1/5 remaining count
    case 0x05:
    case 0xCA: // Read Channel 2/6 remaining count
    case 0x07:
    case 0xCE: // Read Channel 3/7 remaining count
        return dma_flipflop_read(dma.current_count[((port >> offset & 3) | (offset << 2)) & 7], dma.flipflop + offset, 1);
    case 0x08:
    case 0xD0: // Read Status Register
        return dma.status[offset];
    case 0x09:
    case 0xD2:
    case 0x0A:
    case 0xD4:
    case 0x0B:
    case 0xD6:
    case 0x0C:
    case 0xD8:
    case 0x0E:
    case 0xDC: // Unknown
        DMA_LOG("Unknown DMA command: %02x\n", port);
        break;
    case 0xDA:
        DMA_LOG("Read temporary register command not supported in 82374\n");
        break;
    case 0x0F:
    case 0xDE: // Read all mask register bits
        return dma.mask[offset];
    case 0x80 ... 0x8F: {
        int id = page_register_offsets[port & 15];
        switch (id) {
        case 0 ... 7:
            return dma.addr_high[id] >> 16 & 255;
        default:
            if (port != 0x80)
                DMA_LOG("Unknown DMA read pagelo: %02x\n", port);
            return -1;
        }
    }
    case 0x480 ... 0x48F: {
        int id = page_register_offsets[port & 15];
        switch (id) {
        case 0 ... 7:
            return dma.addr_high[id] >> 24 & 255;
        default:
            DMA_LOG("Unknown DMA read pagehi: %02x\n", port);
            return -1;
        }
    }
    default:
        DMA_LOG("Unknown DMA writeb: %04x\n", port);
    }
    return -1;
}

static void dma_io_writeb(uint32_t port, uint32_t data)
{
    int offset = port >= 0xC0;
    switch (port) {
    case 0x00:
    case 0xC0: // Write Channel 0/4 starting address
    case 0x02:
    case 0xC4: // Write Channel 1/5 starting address
    case 0x04:
    case 0xC8: // Write Channel 2/6 starting address
    case 0x06:
    case 0xCC: // Write Channel 3/7 starting address
        dma_flipflop_write(&dma.start_addr[((port >> (offset + 1) & 3) | (offset << 2)) & 7], data, dma.flipflop + offset, 0);
        dma_flipflop_write(&dma.current_addr[((port >> (offset + 1) & 3) | (offset << 2)) & 7], data, dma.flipflop + offset, 1);
        break;
    case 0x01:
    case 0xC2: // Write Channel 0/4 starting count
    case 0x03:
    case 0xC6: // Write Channel 1/5 starting count
    case 0x05:
    case 0xCA: // Write Channel 2/6 starting count
    case 0x07:
    case 0xCE: // Write Channel 3/7 starting count
        dma_flipflop_write(&dma.start_count[((port >> (offset + 1) & 3) | (offset << 2)) & 7], data, dma.flipflop + offset, 0);
        dma_flipflop_write(&dma.current_count[((port >> (offset + 1) & 3) | (offset << 2)) & 7], data, dma.flipflop + offset, 1);
        break;
    case 0x08:
    case 0xD0: // Write Command Register
        dma.command[offset] = data;
        break;
    case 0x09:
    case 0xD2: { // Write Request Register. Note: Only used for memory-to-memory transfers
        int channel = data & 3;
        dma.status[offset] &= ~(1 << channel);
        if (data & 4)
            dma.status[offset] |= 1 << (channel + 4);
        else
            dma.status[offset] &= ~(1 << (channel + 4));
        dma_run_transfers();
        break;
    }
    case 0x0A:
    case 0xD4: { // Write Single Mask
        int channel = data & 3;
        if (data & 4)
            dma.mask[offset] |= 1 << channel;
        else
            dma.mask[offset] &= ~(1 << channel);
        dma_run_transfers();
        break;
    }
    case 0x0B:
    case 0xD6: { // Write Mode Register
        int channel = data & 3;
        if ((data >> 2 & 3) == 3)
            DMA_LOG("Unsupported DMA transfer mode 3\n");
        dma.mode[channel | (offset << 2)] = data;
        dma_run_transfers();
        break;
    }
    case 0x0C:
    case 0xD8: // Clear Flipflop
        dma.flipflop[offset] = 0;
        break;
    case 0x0D:
    case 0xDA: // Reset
        dma.flipflop[offset] = 0;
        dma.command[offset] = 0;
        dma.status[offset] = 0;
        dma.mask[offset] = 15; // Enable all
        dma_run_transfers();
        break;
    case 0x0E:
    case 0xDC: // Clear Mask Register
        dma.mask[offset] = 0;
        dma_run_transfers();
        break;
    case 0x0F:
    case 0xDE: // Set Mask Register
        dma.mask[offset] = data;
        dma_run_transfers();
        break;
    case 0x80 ... 0x8F: {
        int id = page_register_offsets[port & 15];
        switch (id) {
        case 0 ... 7:
            dma.addr_high[id] &= ~0x00FF0000;
            dma.addr_high[id] |= data << 16;
            break;
        default:
            if (port != 0x80)
                DMA_LOG("Unknown DMA write pagelo: %02x\n", port);
        }
        break;
    }
    case 0x480 ... 0x48F: {
        int id = page_register_offsets[port & 15];
        switch (id) {
        case 0 ... 7:
            dma.addr_high[id] &= ~0xFF000000;
            dma.addr_high[id] |= data << 24;
            break;
        default:
            if (port != 0x80)
                DMA_LOG("Unknown DMA write pagehi: %02x\n", port);
        }
        break;
    }
    }
}

static void dma_reset(void)
{
    for (int offset = 0; offset < 2; offset++) {
        dma.flipflop[offset] = 0;
        dma.command[offset] = 0;
        dma.status[offset] = 0;
        dma.mask[offset] = 15;
    }
}

void dma_raise_dreq(int line)
{
    dma.status[line >> 2] |= 16 << (line & 3);

    dma_run_transfers();
}

static void* dma_get_buf(int line)
{
    switch (line) {
    case 2:
        return fdc_dma_buf();
    default:
        DMA_FATAL("Unknown line: %d\n", line);
    }
    abort();
}
static void dma_done(int line)
{
    switch (line) {
    case 2:
        fdc_dma_complete();
        break;
    default:
        DMA_FATAL("Unknown line: %d\n", line);
    }
}

static void dma_run_transfers(void)
{
    void* mem = cpu_get_ram_ptr();
    for (int i = 0; i < 8; i++) {
        if (dma.status[i >> 2] & (16 << (i & 3))) {
            if (!(dma.mask[i >> 2] & (1 << (i & 3)))) {
                uint32_t ccount = dma.current_count[i] + 1,
                         is_write = (dma.mode[i] & 0x0C) == 4,
                         incdec = dma.mode[i] & 0x20 ? -1 : 1,
                         addr = dma.current_addr[i],
                         is16 = i >= 4,
                         size = is16 + 1,
                         page = ((addr << is16) & 0xFFFF) | dma.addr_high[i];
                void* buf = dma_get_buf(i);
                while (ccount) {
                    uint32_t current_addr = ((addr << is16) & 0xFFFF) | dma.addr_high[i];
                    if ((current_addr ^ page) > 4095) {
                        page = current_addr;
                        cpu_init_dma(current_addr);
                    }
                    if (is_write) // Peripheral writing to memory
                        cpu_write_mem(current_addr, buf, size);
                    else {
                        if (is16)
                            *(uint16_t*)buf = *(uint16_t*)(mem + current_addr);
                        else
                            *(uint8_t*)buf = *(uint8_t*)(mem + current_addr);
                    }
                    buf += size;
                    addr += incdec;
                    ccount--;
                }
                if (dma.mode[i] & 0x10) {
                    dma.current_addr[i] = dma.start_addr[i];
                    dma.current_count[i] = dma.start_count[i];
                } else {
                    dma.current_addr[i] = addr;
                    dma.current_count[i] = ccount;
                }
                dma.status[i >> 2] ^= 16 << (i & 3);
                dma.status[i >> 2] |= 1 << (i & 3);
                dma_done(i);
            }
        }
    }
}

void dma_init(void)
{
    io_register_read(0, 16, dma_io_readb, NULL, NULL);
    io_register_read(0xC0, 32, dma_io_readb, NULL, NULL);
    io_register_write(0, 16, dma_io_writeb, NULL, NULL);
    io_register_write(0xC0, 32, dma_io_writeb, NULL, NULL);

    io_register_write(0x480, 8, dma_io_writeb, NULL, NULL);
    io_register_read(0x480, 8, dma_io_readb, NULL, NULL);
    io_register_write(0x80, 16, dma_io_writeb, NULL, NULL);
    io_register_read(0x80, 16, dma_io_readb, NULL, NULL);
    io_register_reset(dma_reset);

    state_register(dma_state);
}