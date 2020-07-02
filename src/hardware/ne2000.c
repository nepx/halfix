// RTL8019AS (NE2000-compatible Ethernet controller) emulation
// http://www.ethernut.de/pdf/8019asds.pdf
// https://wiki.osdev.org/Ne2000
// https://web.archive.org/web/20000229212715/https://www.national.com/pf/DP/DP8390D.html
// https://www.cs.usfca.edu/~cruse/cs326/RTL8139_ProgrammersGuide.pdf
#include "devices.h"
#include "pc.h"

#define NE2K_LOG(x, ...) LOG("NE2K", x, ##__VA_ARGS__)
#define NE2K_FATAL(x, ...) FATAL("NE2K", x, ##__VA_ARGS__)

struct ne2000 {
    uint32_t iobase;
    int irq;
} ne2000;

static uint32_t ne2000_read(uint32_t port)
{
    NE2K_FATAL("TODO: read port=%08x\n", port);
}

static void ne2000_write(uint32_t port, uint32_t data)
{
    NE2K_FATAL("TODO: write port=%08x data=%08x\n", port, data);
}

static void ne2000_pci_remap(uint8_t* dev, unsigned int newbase)
{
    if (newbase != ne2000.iobase) {
        dev[0x10] = newbase | 1;
        dev[0x11] = newbase >> 8;

        io_unregister_read(ne2000.iobase, 32);
        io_unregister_write(ne2000.iobase, 32);
        io_register_read(newbase, 16, ne2000_read, NULL, NULL);
        io_register_write(newbase, 16, ne2000_write, NULL, NULL);

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
    case 0x30 ... 0x33: // mem io bars
        return 0;
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
    uint8_t* dev = pci_create_device(0, 5, 0, ne2000_pci_write);
    pci_copy_default_configuration(dev, (void*)ne2000_config_space, 16);

    dev[0x3D] = 1;

    ne2000_pci_remap(dev, conf->port_base);
}

void ne2000_init(struct ne2000_settings* conf)
{
    if (!conf->enabled)
        return;

    if (conf->irq == 0)
        conf->irq = 3;

    if (conf->pci) {
        ne2000_pci_init(conf);
    } else {
        ne2000.iobase = conf->port_base & ~31;
        ne2000.irq = conf->irq & 15;
        io_register_read(0x300, 32, ne2000_read, NULL, NULL);
        io_register_write(0x300, 32, ne2000_write, NULL, NULL);
    }
}