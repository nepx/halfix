// I/O Advanced Programmable Interrupt Controller emulation
//  Can be simulated optionally.
// https://pdos.csail.mit.edu/6.828/2018/readings/ia32/ioapic.pdf

#include "io.h"
#include "devices.h"
#include "pc.h"

#define IOAPIC_LOG(x, ...) LOG("IOAPIC", x, ##__VA_ARGS__)
#define IOAPIC_FATAL(x, ...)          \
    do {                              \
        IOAPIC_LOG(x, ##__VA_ARGS__); \
        ABORT();                      \
    } while (0)

#define MAXIMUM_IRQS 24

#define IOAPICID 0
#define IOAPICVER 1
#define IOAPICARB 2
#define IORED_TBL 0x10

#define INTERRUPT_MASKED (1 << 16)
#define TRIGGER_MODE (1 << 15) // 1: level 0: edge
#define REMOTE_IRR (1 << 14)
#define PIN_POLARITY (1 << 13) // aka whether interrupt line is HIGH or LOW
#define DELIVERY_STATUS (1 << 12)
#define DESTINATION_MODE (1 << 11) // 0: Physical (send to particular APIC), 1: logical (set of processors)

enum {
    DELIVERY_FIXED = 0,
    DELIVERY_LOWEST_PRIORITY = 1,
    DELIVERY_SMI = 2,
    DELIVERY_NMI = 4,
    DELIVERY_INIT = 5,
    DELIVERY_EXTINT = 7
};

static struct ioapic_info {
    // <<< BEGIN STRUCT "struct" >>>
    uint32_t base;
    uint32_t register_selected;
    uint32_t id;

    uint32_t pin_state;
    uint32_t irr;

    uint32_t arbitration_id; // Defaults to zero

    uint32_t redtbl[48];

    int enabled;

    uint32_t temp_data;
    // <<< END STRUCT "struct" >>>
} ioapic;

static void ioapic_state(void)
{
    // <<< BEGIN AUTOGENERATE "state" >>>
    struct bjson_object* obj = state_obj("ioapic", 9 + 0);
    state_field(obj, 4, "ioapic.base", &ioapic.base);
    state_field(obj, 4, "ioapic.register_selected", &ioapic.register_selected);
    state_field(obj, 4, "ioapic.id", &ioapic.id);
    state_field(obj, 4, "ioapic.pin_state", &ioapic.pin_state);
    state_field(obj, 4, "ioapic.irr", &ioapic.irr);
    state_field(obj, 4, "ioapic.arbitration_id", &ioapic.arbitration_id);
    state_field(obj, 192, "ioapic.redtbl", &ioapic.redtbl);
    state_field(obj, 4, "ioapic.enabled", &ioapic.enabled);
    state_field(obj, 4, "ioapic.temp_data", &ioapic.temp_data);
// <<< END AUTOGENERATE "state" >>>
}

static void ioapic_update(void)
{
    uint32_t bit = 1;
    for (int i = 0; i < 24; i++) {
        uint32_t lo = ioapic.redtbl[i << 1];
        uint32_t hi = ioapic.redtbl[(i << 1) | 1];

        uint8_t irq_number = lo & 0xFF;

        if (!(lo & INTERRUPT_MASKED) && (ioapic.irr & bit)) {
            if (!(lo & TRIGGER_MODE))
                ioapic.irr &= ~bit;

            int type = lo >> 8 & 7;
            switch (type) {
            case DELIVERY_LOWEST_PRIORITY:
                type = 3; // Match it up with the APIC's LVT lowest priority thing
                goto done;
            case DELIVERY_EXTINT:
                irq_number = pic_get_interrupt(); // Is this right? IDK
            // INTENTIONAL FALLTHROUGH
            default:
                UNUSED(hi);
            done:
                apic_receive_bus_message(irq_number, type, (lo & TRIGGER_MODE) != 0);
            }
        }

        bit <<= 1;
    }
}

static uint32_t ioapic_read(uint32_t addr)
{
    addr -= ioapic.base;
    switch (addr >> 4) {
    case 0:
        return ioapic.register_selected;
    case 1:
        IOAPIC_LOG("Reading from register addr=%02x\n", ioapic.register_selected);
        switch (ioapic.register_selected & 0xFF) {
        case IOAPICID:
            return ioapic.id << 24;
        case IOAPICVER:
            return (0x11 << 0) | // IOAPIC version
                (0x17 << 16); // Maximum Redirection Entry
        case IOAPICARB:
            return ioapic.arbitration_id; // Something to do with buses...
        case 0x10 ... 0x3F: {
            // Modify I/O indirection table. Note that each quantity is 64 bits wide. Modifying this is like modifying the PIC's IMR
            return ioapic.redtbl[(ioapic.register_selected & 0xFF) - 0x10];
            break;
        }
        default:
            IOAPIC_LOG("Unknown register read: %02x\n", ioapic.register_selected & 0xFF);
            return 0;
        }
        break;
    default:
        IOAPIC_LOG("Unknown read from %08x\n", addr);
        return 0;
    }
}
static void ioapic_write(uint32_t addr, uint32_t data)
{
    addr -= ioapic.base;
    switch (addr >> 4) {
    case 0:
        ioapic.register_selected = data;
        break;
    case 1:
        IOAPIC_LOG("Write id=%02x data=%08x\n", ioapic.register_selected, data);
        switch (ioapic.register_selected & 0xFF) {
        case IOAPICID:
            ioapic.id = data & 0x0F000000;
            break;
        case 0x10 ... 0x3F: {
            // Modify I/O indirection table. Note that each quantity is 64 bits wide. Modifying this is like modifying the PIC's IMR
            uint32_t idx = (ioapic.register_selected & 0xFF) - 0x10;
            if (data != ioapic.redtbl[idx]) {
                ioapic.redtbl[idx] = data;
                ioapic_update();
            }
            break;
        }
        default:
            IOAPIC_LOG("Unknown register write: %02x\n", ioapic.register_selected & 0xFF);
        }
        break;
    default:
        IOAPIC_FATAL("TODO: ioapic write %08x data=%08x\n", addr, data);
    }
}
// See corresponding comments in apic.c for details

static uint32_t ioapic_readb(uint32_t addr)
{
    //IOAPIC_LOG("8-bit read from %08x\n", addr);
    return ioapic_read(addr & ~3) >> ((addr & 3) * 8) & 0xFF;
}
static void ioapic_writeb(uint32_t addr, uint32_t data)
{
    int offset = addr & 3, byte_offset = offset << 3;
    ioapic.temp_data &= ~(0xFF << byte_offset);
    ioapic.temp_data |= data << byte_offset;
    if (offset == 3) {
        ioapic_write(addr & ~3, ioapic.temp_data);
    }
}

static void ioapic_reset(void)
{
    ioapic.base = 0xFEC00000;
    ioapic.id = 0;
    ioapic.arbitration_id = 0;

    for (int i = 0; i < 48; i += 2) {
        ioapic.redtbl[i] = 0;
        ioapic.redtbl[i] = INTERRUPT_MASKED;
    }
}

void ioapic_raise_irq(int line)
{
    if (!ioapic.enabled)
        return;

    if (line == 0)
        line = 2; // Not mentioned in documentation, but IRQ0 is redirected to line 2

    int redtbl_idx = line << 1, pin = 1 << line;
    uint32_t lower_32bits = ioapic.redtbl[redtbl_idx] |= PIN_POLARITY;
    if (lower_32bits & TRIGGER_MODE) {
        // Level triggered
        if (lower_32bits & (1 << 14)) { // Remote IRR bit still set...
            IOAPIC_LOG("Remote IRR set, discarding interrupt\n");
            return;
        }
        ioapic.redtbl[redtbl_idx] |= 1 << 14; // Set "Remote IRR" bit
        ioapic.pin_state |= pin;
        ioapic.irr |= pin;
        ioapic_update();
    } else {
        // Edge triggered
        if (!(ioapic.pin_state & pin)) {
            // Signal goes from low to high -- trigger
            ioapic.irr |= pin;
            ioapic.pin_state |= pin;
            ioapic_update();
        }
    }
}

void ioapic_remote_eoi(int irq)
{
    for (int i = 0; i < 24; i++) {
        int index = i << 1, value = ioapic.redtbl[index];
        if ((value & 0xFF) != irq)
            continue;
        if (value & (1 << 14)) {
            // Remote IRR bit has been set, now clear it
            value &= ~(1 << 14);
            ioapic.redtbl[index] = value;
        }
    }
}

void ioapic_lower_irq(int line)
{
    if (!ioapic.enabled)
        return;

    if (line == 0)
        line = 2;

    int redtbl_idx = line << 1, pin = 1 << line;
    ioapic.pin_state &= ~pin;

    ioapic.redtbl[redtbl_idx] &= ~PIN_POLARITY;

    if (ioapic.redtbl[redtbl_idx] & TRIGGER_MODE) // Lower interrupt if level triggered
        ioapic.irr &= ~pin;
}

void ioapic_init(struct pc_settings* pc)
{
    if (!pc->apic_enabled)
        return;
    ioapic.enabled = 1;

    io_register_reset(ioapic_reset);
    ioapic.base = 0xFEC00000;

    state_register(ioapic_state);

    // Map one page of MMIO at the specified address.
    io_register_mmio_read(ioapic.base, 4096, ioapic_readb, NULL, ioapic_read);
    io_register_mmio_write(ioapic.base, 4096, ioapic_writeb, NULL, ioapic_write);
}