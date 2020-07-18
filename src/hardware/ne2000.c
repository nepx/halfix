// RTL8019AS (NE2000-compatible Ethernet controller) emulation
// http://www.ethernut.de/pdf/8019asds.pdf
// https://wiki.osdev.org/Ne2000
// https://web.archive.org/web/20000229212715/https://www.national.com/pf/DP/DP8390D.html
// https://www.cs.usfca.edu/~cruse/cs326/RTL8139_ProgrammersGuide.pdf
#include "devices.h"
#include "io.h"
#include "net.h"
#include "pc.h"
#include <string.h> // memcpy

#define NE2K_LOG(x, ...) LOG("NE2K", x, ##__VA_ARGS__)
#define NE2K_DEBUG(x, ...) LOG("NE2K", x, ##__VA_ARGS__)
#define NE2K_FATAL(x, ...) FATAL("NE2K", x, ##__VA_ARGS__)

#define CMD_PAGESEL 0xC0
#define CMD_RWMODE 0x38 // 0: not allowed, 1: remote read, 2: remote write, 4+: abort/dma
#define CMD_MODE_READ 1
#define CMD_MODE_WRITE 2
#define CMD_MODE_SEND 3
#define CMD_TXP 0x04 // Bit must be set to transmit a packet, cleared internally afterwards
#define CMD_STA 0x02 // useless
#define CMD_STP 0x01 // disables packet send/recv

#define ISR_PRX 0x01 // Packet received
#define ISR_PTX 0x02 // Packet transmitted
#define ISR_RXE 0x04 // Receive error
#define ISR_TXE 0x08 // Transmit error
#define ISR_OVW 0x10 // Overwrite error
#define ISR_CNT 0x20 // Counter overflow
#define ISR_RDC 0x40 // Remote DMA done
#define ISR_RST 0x80 // Reset state

#define DCR_WTS 0x01 // Word transfer select
#define DCR_BOS 0x02 // Byte order select
#define DCR_LAS 0x04 // Long address select
#define DCR_LS 0x08 // Loopback select
#define DCR_AR 0x10 // Auto-init remote
#define DCR_FIFO_THRESH 0x60 // FIFO threshold

#define NE2K_DEVID 5

#define NE2K_MEMSTART

#define NE2K_MEMSZ (32 << 10)
struct ne2000 {
    uint32_t iobase;
    int irq;

    uint8_t mem[256 * 128]; // 128 chunks of 256 bytes each, or 32K

    // Tally counters -- for when things go wrong
    uint8_t cntr[3];

    // Some more registers:
    //  ISR: Interrupt Status Register
    //  DCR: Data Configuration Register
    //  IMR: Interrupt Mask Register
    //  RCR: Receive Configuration Register
    //  TCR: Transmit Configuration Register
    uint8_t isr, dcr, imr, rcr, tcr;

    // Transmission status register
    uint8_t tsr;

    // Receive status register
    uint8_t rsr;

    // Remote byte count register (RBCR), remote start address register (RSAR)
    uint16_t rbcr, rsar;

    // Transfer page start register
    int tpsr;
    // Transfer count
    int tcnt;

    // Boundary pointer
    int bnry;

    // Current page register
    int curr;

    // Physical address registers
    uint8_t par[6];

    uint8_t multicast[8];
    int pagestart, pagestop;

    int cmd;

    int enabled;
} ne2000;

static void ne2000_reset_internal(int software)
{
    if (software) {
        ne2000.isr = ISR_RST;
        return;
    } else {
        ne2000.pagestart = 0x40 << 8;
        ne2000.pagestop = 0x80 << 8;
        ne2000.bnry = 0x4C << 8;
        ne2000.cmd = CMD_STP;
    }
}
static void ne2000_reset(void)
{
    ne2000_reset_internal(0);
}

static void ne2000_trigger_irq(int flag)
{
    ne2000.isr |= flag;
    if (!((ne2000.isr & ne2000.imr) & flag))
        return;

    NE2K_DEBUG("Triggering IRQ! (isr=%02x imr=%02x &=%02x)\n", ne2000.isr, ne2000.imr, ne2000.isr & ne2000.imr);

    // XXX -- the PIC doesn't support edge/level triggered interrupts yet, so we simulate it this way
    pci_set_irq_line(NE2K_DEVID, 0);
    pci_set_irq_line(NE2K_DEVID, 1);
}
static void ne2000_lower_irq(void)
{
    pci_set_irq_line(NE2K_DEVID, 0);
}

static uint32_t ne2000_asic_mem_read(void)
{
    if (!(ne2000.dcr & DCR_WTS)) { // Byte-sized DMA transfers
        if (ne2000.rsar >= NE2K_MEMSZ) // Ignore out of bounds accesses
            return 0xFF;
        uint8_t data = ne2000.mem[ne2000.rsar++];
        ne2000.rbcr--;
        if (!ne2000.rbcr)
            ne2000_trigger_irq(ISR_RDC);
        return data;
    } else { // Word-sized DMA transfers
        if (ne2000.rsar & 1) {
            NE2K_LOG("Unaligned RSAR -- forcing alignment\n");
            // In the manual, it says that A0 is forced to zero
            ne2000.rsar &= ~1;
        }
        if (ne2000.rsar >= NE2K_MEMSZ) // Ignore out of bounds accesses
            return 0xFFFF;
        uint16_t data = ne2000.mem[ne2000.rsar] | (ne2000.mem[ne2000.rsar + 1] << 8);

        ne2000.rsar += 2;
        ne2000.rbcr -= 2;

        if (!ne2000.rbcr)
            ne2000_trigger_irq(ISR_RDC);
        return data;
    }
}

static void ne2000_asic_mem_write(uint32_t data)
{
    if (!(ne2000.dcr & DCR_WTS)) { // Byte-sized DMA transfers
        if (ne2000.rsar >= NE2K_MEMSZ) // Ignore out of bounds accesses
            return;
        ne2000.mem[ne2000.rsar++] = data;
        ne2000.rbcr--;
        if (!ne2000.rbcr)
            ne2000_trigger_irq(ISR_RDC);
    } else { // Word-sized DMA transfers
        if (ne2000.rsar & 1) {
            NE2K_LOG("Unaligned RSAR -- forcing alignment\n");
            ne2000.rsar &= ~1;
        }
        if (ne2000.rsar >= NE2K_MEMSZ) // Ignore out of bounds accesses
            return;
        ne2000.mem[ne2000.rsar] = data;
        ne2000.mem[ne2000.rsar + 1] = data >> 8;

        ne2000.rsar += 2;
        ne2000.rbcr -= 2;

        if (!ne2000.rbcr)
            ne2000_trigger_irq(ISR_RDC);
    }
}

static uint32_t ne2000_read0(uint32_t port)
{
    uint8_t retv;
    switch (port) {
    case 3: // Boundary pointer
        retv = ne2000.bnry;
        NE2K_DEBUG("Boundary read: %02x\n", retv);
        break;
    case 4:
        retv = ne2000.tsr;
        NE2K_DEBUG("Trans. status: %02x\n", retv);
        break;
    case 7:
        retv = ne2000.isr;
        NE2K_DEBUG("ISR read: %02x\n", retv);
        break;
    case 13:
    case 14:
    case 15:
        retv = ne2000.cntr[port - 13];
        NE2K_DEBUG("CNTR%d: read %02x\n", port - 13, retv);
        break;
    default:
        NE2K_FATAL("PAGE0 read %02x\n", port);
    }
    return retv;
}

static uint32_t ne2000_read1(uint32_t port)
{
    uint8_t retv;
    switch (port) {
    case 1 ... 6:
        retv = ne2000.par[port - 1];
        NE2K_DEBUG("PAR%d: read %02x\n", port - 1, retv);
        break;
    case 7:
        retv = ne2000.curr >> 8;
        NE2K_DEBUG("CURR: read %02x\n", retv);
        break;
    case 8 ... 15:
        retv = ne2000.multicast[port & 7];
        NE2K_DEBUG("MULTI%d: read %02x\n", port & 7, retv);
        break;
    }
    return retv;
}

static uint32_t ne2000_read(uint32_t port)
{
    switch (port & 31) {
    case 0:
        NE2K_DEBUG("CMD: read %02x\n", ne2000.cmd);
        return ne2000.cmd;
    case 1 ... 15:
        // Select correct register page
        switch (ne2000.cmd >> 6 & 3) {
        case 0:
            return ne2000_read0(port & 31);
        case 1:
            return ne2000_read1(port & 31);
        default:
            NE2K_FATAL("todo: (offs %02x) implement page %d\n", port & 31, ne2000.cmd >> 6 & 3);
        }
        break;
    case 16: // asic data read port
        return ne2000_asic_mem_read();
    case 31:
        return 0;
    default:
        NE2K_FATAL("TODO: read port=%08x\n", port);
    }
}

// Intended for port +0x10
static uint32_t ne2000_read_mem16(uint32_t port)
{
    UNUSED(port);
    if (ne2000.dcr & DCR_WTS)
        return ne2000_asic_mem_read();
    else {
        int retval = ne2000_asic_mem_read();
        return retval | (ne2000_asic_mem_read() << 8);
    }
}
static uint32_t ne2000_read_mem32(uint32_t port)
{
    UNUSED(port);
    if (ne2000.dcr & DCR_WTS) {
        int retval = ne2000_asic_mem_read();
        retval |= ne2000_asic_mem_read() << 16;
        return retval;
    } else {
        int retval = ne2000_asic_mem_read();
        retval |= ne2000_asic_mem_read() << 8;
        retval |= ne2000_asic_mem_read() << 16;
        retval |= ne2000_asic_mem_read() << 24;
        return retval;
    }
}

static void ne2000_write0(uint32_t port, uint32_t data)
{
    switch (port) {
    case 1: // Page start
        NE2K_DEBUG("PageStart write: %02x\n", data);
        ne2000.pagestart = (data << 8) & 0xFF00;
        break;
    case 2: // Page stop
        NE2K_DEBUG("PageStop write: %02x\n", data);
        ne2000.pagestop = (data << 8) & 0xFF00;
        break;
    case 3: // Boundary pointer
        NE2K_DEBUG("Boundary write: %02x\n", data);
        ne2000.bnry = data;
        break;
    case 4:
        NE2K_DEBUG("TPSR: %02x\n", data);
        ne2000.tpsr = (data << 8) & 0xFF00;
        break;
    case 5: // TCNT low
    case 6: { // TCNT hi
        int b0 = (port & 1) << 3;
        // Replace the (upper|lower) byte based on the I/O port
        ne2000.tcnt = (ne2000.tcnt & (0xFF << b0)) | (data << (b0 ^ 8));
        NE2K_DEBUG("TCNT: %04x\n", ne2000.tcnt);
        break;
    }
    case 7: // ISR
        ne2000.isr &= ~data;
        if (!data)
            ne2000_lower_irq();
        NE2K_DEBUG("ISR ack: %02x\n", ne2000.isr);
        ne2000_trigger_irq(0);
        break;
    case 8: // Remote start address register
        NE2K_DEBUG("RSAR low: %02x\n", data);
        ne2000.rsar = (ne2000.rsar & 0xFF00) | data;
        break;
    case 9: // Remote start address register
        NE2K_DEBUG("RSAR high: %02x\n", data);
        ne2000.rsar = (ne2000.rsar & 0x00FF) | (data << 8);
        break;
    case 10: // Remote byte count register
        NE2K_DEBUG("RBCR low: %02x\n", data);
        ne2000.rbcr = (ne2000.rbcr & 0xFF00) | data;
        break;
    case 11:
        NE2K_DEBUG("RBCR high: %02x\n", data);
        ne2000.rbcr = (ne2000.rbcr & 0x00FF) | (data << 8);
        break;
    case 12: // Receive configuration register
        NE2K_DEBUG("RCR: %02x\n", data);
        ne2000.rcr = data;
        break;
    case 13: // Transmit configuration register
        NE2K_DEBUG("TCR: %02x\n", data);
        ne2000.tcr = data;
        break;
    case 14: // Data configuration register
        NE2K_DEBUG("DCR write: %02x\n", data);
        ne2000.dcr = data;
        break;
    case 15: // Interrupt mask register
        NE2K_DEBUG("IMR write: %02x\n", data);
        ne2000.imr = data;
        break;
    case 16:
        ne2000_asic_mem_write(data);
        break;
    default:
        NE2K_FATAL("todo: page0 implement write %d\n", port & 31);
    }
}
static void ne2000_write1(uint32_t port, uint32_t data)
{
    switch (port) {
    case 1 ... 6:
        ne2000.par[port - 1] = data;
        NE2K_DEBUG("PAR%d: %02x\n", port - 1, data);
        break;
    case 7:
        ne2000.curr = data << 8;
        NE2K_DEBUG("CURR: read %02x\n", data);
        break;
    case 8 ... 15:
        ne2000.multicast[port & 7] = data;
        NE2K_DEBUG("Multicast%d: %02x\n", port & 7, data);
        break;
    default:
        NE2K_FATAL("todo: page1 implement port %d\n", port & 31);
    }
}
static void ne2000_write(uint32_t port, uint32_t data)
{
    switch (port & 31) {
    case 0: {
        NE2K_DEBUG("CMD: write %02x\n", data);
        int stop = data & CMD_STP,
            start = data & CMD_STA,
            transmit_packet = data & CMD_TXP,
            rdma_cmd = data >> 3 & 7,
            psel = data >> 6 & 3;
        ne2000.cmd = data;
        UNUSED(psel); // psel is decoded elsewhere
        UNUSED(start); // ?
        if (!stop) {
            if ((rdma_cmd == CMD_MODE_READ) && !ne2000.rbcr) {
                ne2000_trigger_irq(ISR_RDC);
            }
            if (transmit_packet) {
                // Check for coherency -- make sure that we don't start past the end of our memory
                ne2000.tpsr &= NE2K_MEMSZ - 1;
                // Check if read goes past memory end
                if ((ne2000.tpsr + ne2000.tcnt) >= NE2K_MEMSZ) {
                    NE2K_LOG("TRANSMIT ERROR: read past end of memory\n");
                    ne2000_trigger_irq(ISR_TXE); // is this what we do here? Better than crashing
                }
                net_send(&ne2000.mem[ne2000.tpsr], ne2000.tcnt);
                ne2000.tsr |= 1;
                ne2000_trigger_irq(ISR_PTX); // TODO: timing
            }
            break; // TODO
        }
        break;
    }
    case 1 ... 15:
        switch (ne2000.cmd >> 6 & 3) {
        case 0:
            ne2000_write0(port & 31, data);
            break;
        case 1:
            ne2000_write1(port & 31, data);
            break;
        default:
            NE2K_FATAL("todo: (offs %d/data %02x) implement page %d\n", port & 31, data, ne2000.cmd >> 6 & 3);
        }
        break;
    case 31: // Reset port
        NE2K_LOG("Software reset\n");
        ne2000_reset_internal(1);
        break;
    default:
        NE2K_FATAL("TODO: write port=%08x data=%02x\n", port, data);
    }
}
static void ne2000_write_mem16(uint32_t port, uint32_t data)
{
    UNUSED(port);
    if (ne2000.dcr & DCR_WTS)
        ne2000_asic_mem_write(data);
    else {
        ne2000_asic_mem_write(data);
        ne2000_asic_mem_write(data >> 8);
    }
}
static void ne2000_write_mem32(uint32_t port, uint32_t data)
{
    UNUSED(port);
    if (ne2000.dcr & DCR_WTS) {
        ne2000_asic_mem_write(data);
        ne2000_asic_mem_write(data >> 16);
    } else {
        ne2000_asic_mem_write(data);
        ne2000_asic_mem_write(data >> 8);
        ne2000_asic_mem_write(data >> 16);
        ne2000_asic_mem_write(data >> 24);
    }
}

static void ne2000_pci_remap(uint8_t* dev, unsigned int newbase)
{
    if (newbase != ne2000.iobase) {
        dev[0x10] = newbase | 1;
        dev[0x11] = newbase >> 8;

        if (ne2000.iobase != 0) {
            io_unregister_read(ne2000.iobase, 32);
            io_unregister_write(ne2000.iobase, 32);
        }
        io_register_read(newbase, 32, ne2000_read, NULL, NULL);
        io_register_write(newbase, 32, ne2000_write, NULL, NULL);
        io_register_read(newbase + 16, 1, ne2000_read, ne2000_read_mem16, ne2000_read_mem32);
        io_register_write(newbase + 16, 1, ne2000_write, ne2000_write_mem16, ne2000_write_mem32);
        //io_register_write(newbase + 31, 1, NULL, ne2000_write, NULL);

        ne2000.iobase = newbase;
        NE2K_LOG("Remapped controller to 0x%x\n", ne2000.iobase);
    }
}

static int ne2000_pci_write(uint8_t* ptr, uint8_t addr, uint8_t data)
{
    switch (addr) {
    case 4:
        ptr[0x04] = data & 3;
        return 1;
    case 5 ... 7:
        return 0; // ?
    case 0x10:
        ptr[0x10] = data | 1;
        return 1; // Let the write pass through
    case 0x11: {
        ptr[0x11] = data;
        unsigned int newbase = (ptr[0x10] | (data << 8));
        if (newbase != 0xFFFE) {
            if (newbase & 1)
                ne2000_pci_remap(ptr, newbase & ~31);
        }
        return 1;
    }
    case 0x12 ... 0x13:
        return 0; // simple passthrough
    case 0x14 ... 0x1F: // more io bars
        return 0;
    case 0x20 ... 0x2F: // more io bars
        return 0;
    case 0x30 ... 0x33: // option rom -- ignore since we don't have one (and it messes with non-PCI vga)
        return 1;
    case 0x3C: // interrupt pin
        return 0;
    default:
        NE2K_FATAL("unknown pci value: offs=0x%02x data=%02x\n", addr, data);
    }
}

static const uint8_t ne2000_config_space[16] = {
    0xec, 0x10, 0x29, 0x80, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00
};

static void ne2000_pci_init(struct ne2000_settings* conf)
{
    uint8_t* dev = pci_create_device(0, NE2K_DEVID, 0, ne2000_pci_write);
    pci_copy_default_configuration(dev, (void*)ne2000_config_space, 16);

    dev[0x3D] = 1;

    ne2000_pci_remap(dev, conf->port_base);
}

static void ne2000_receive(void* data, int len)
{
    // Don't acknowledge if stop bit set
    // PCap gets some spurious packets before it's initialized
    if (ne2000.cmd & CMD_STP)
        return;

    // Format of a packet (as received by the emulated system):
    //  [0] : Status
    //  [1] : Next page address
    //  [2 ... 3]: Size of packet

    int length_plus_header = 4 + len;
    int total_pages = (length_plus_header + 255) >> 8;

    // Determine the start, end, etc.
    int start = ne2000.curr;
    // Determine the next page
    int nextpg = ne2000.curr + ((255 + length_plus_header) & ~0xFF);
    if (nextpg >= ne2000.pagestop) {
        nextpg += ne2000.pagestart - ne2000.pagestop;
    }

    ne2000.rsr = 1; // properly received

    uint8_t *memstart = ne2000.mem + start,
            *data8 = data;
    if (data8[0] & 1)
        ne2000.rsr |= 0x20; // physical/multicast addr

    memstart[0] = ne2000.rsr;
    memstart[1] = nextpg >> 8;
    memstart[2] = length_plus_header;
    memstart[3] = length_plus_header >> 8;

    if ( // If our starting page is less than the next page...
        (ne2000.curr < nextpg) ||
        // Or if we fit perfectly into the pagestart-pageend area
        (ne2000.curr + total_pages == ne2000.pagestop)) {
        memcpy(memstart + 4, data, len); // We've already written the packet address
    } else {
        // What if the packet is too big?
        int len1 = ne2000.pagestop - ne2000.curr;
        memcpy(memstart + 4, data, len1 - 4);

        memstart = ne2000.mem + ne2000.pagestart;
        // Copy the rest of the bytes to the beginning of pagestart
        printf("addr=%p len=%d\n", data + (len1 - 4), len - (len1 + 4));
        if ((len - (len1 + 4)) < 0)
            __asm__("int3");
        memcpy(memstart, data + (len1 - 4), len - (len1 + 4));
    }
    ne2000.curr = nextpg;
    ne2000_trigger_irq(ISR_PRX);
}

void ne2000_poll(void)
{
    if (!ne2000.enabled)
        return;
    net_poll(ne2000_receive);
}

void ne2000_init(struct ne2000_settings* conf)
{
    if (!conf->enabled)
        return;
    ne2000.enabled = 1;

    if (conf->irq == 0)
        conf->irq = 3;

    // If our mac address is all zeros, then create a new one
    int macsum = 0;
    for (int i = 0; i < 6; i++)
        macsum |= conf->mac_address[i];

    if (macsum == 0) {
        // XXX - we hard-code this now to make our code output deterministic.
        conf->mac_address[0] = 0x12;
        conf->mac_address[1] = 0x34;
        conf->mac_address[2] = 0x56;
        conf->mac_address[3] = 0x78;
        conf->mac_address[4] = 0x9A;
        conf->mac_address[5] = 0xBC;
    }

    for (int i = 0; i < 8; i++) {
        int val;
        if (i == 7)
            val = 0x57;
        else
            val = conf->mac_address[i];
        ne2000.mem[i << 1] = ne2000.mem[(i << 1) | 1] = val;
    }

    if (conf->pci) {
        ne2000_pci_init(conf);
    } else {
        ne2000.iobase = conf->port_base & ~31;
        ne2000.irq = conf->irq & 15;
        io_register_read(0x300, 32, ne2000_read, NULL, NULL);
        io_register_write(0x300, 32, ne2000_write, NULL, NULL);
        io_register_read(0x300 + 16, 1, ne2000_read, ne2000_read_mem16, ne2000_read_mem32);
        io_register_write(0x300 + 16, 1, ne2000_write, ne2000_write_mem16, ne2000_write_mem32);
        //io_register_write(0x300 + 31, 1, NULL, ne2000_write, NULL);
    }

    io_register_reset(ne2000_reset);
}