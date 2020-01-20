// Advanced Configuration and Power Interface
// Based off of the PIIX4 interface.
// https://www.intel.com/Assets/PDF/datasheet/290562.pdf
// https://www.intel.com/assets/pdf/specupdate/297738.pdf

#include "devices.h"
#include "io.h"

#define ACPI_LOG(x, ...) LOG("ACPI", x, ##__VA_ARGS__)
#define ACPI_FATAL(x, ...) FATAL("ACPI", x, ##__VA_ARGS__)

#define ACPI_CLOCK_SPEED 3579545

struct acpi_state {
    // <<< BEGIN STRUCT "struct" >>>
    int enabled;

    uint32_t pmba; // Power management base address
    int pmiose; // Power management I/O Space Enable
    uint32_t pmsts_en; // Power management status register and enable
    uint32_t pmcntrl; // Power management control
    uint32_t last_pm_clock; // The value of the power management clock the last time acpi_timer was called

    uint32_t smba; // System management base address
    int smiose; // System management base address
    // <<< END STRUCT "struct" >>>
} acpi;

static const uint8_t acpi_configuration_space[256] = {
    134, 128, 19, 113, 0, 0, 128, 2, 0, 0, 128, 6, 0, 0, 0, 0, // 0x00
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x10
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x20
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, // 0x30
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x40
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 144, // 0x50
    0, 0, 0, 96, 0, 0, 0, 152, 0, 0, 0, 0, 0, 0, 0, 0, // 0x60
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x70
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x80
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x90
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0xa0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0xb0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0xc0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0xd0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0xe0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 // 0xf0
};

static void acpi_reset(void)
{
    acpi.pmcntrl = 1;
}

static inline uint32_t read32le(uint8_t* x, uint32_t offset)
{
    uint32_t res = x[offset];
    res |= x[offset + 1] << 8;
    res |= x[offset + 2] << 16;
    res |= x[offset + 3] << 24;
    return res;
}

static uint32_t acpi_get_clock(itick_t now)
{
    return (double)now * (double)ACPI_CLOCK_SPEED / (double)ticks_per_second;
}

static uint32_t acpi_pm_read(uint32_t addr)
{
    int offset = addr & 3;
    uint32_t result = 0;
    switch (addr & 0x3C) {
    case 0:
        result = acpi.pmsts_en;
        break;
    case 4:
        result = acpi.pmcntrl;
        break;
    case 8:
        // Timer
        result = acpi_get_clock(get_now());
        break;
    default:
        ACPI_FATAL("TODO: power management read: %04x\n", addr);
    }
    return result >> (offset * 8);
}
static void acpi_pm_write(uint32_t addr, uint32_t data)
{
    int shift = (addr & 3) * 8;
    switch (addr & 0x3C) {
    case 0:
        if ((addr & 2) == 0) {
            // Writing to this register clears some bits in the status register
            data = ~data;
            acpi.pmsts_en &= (data << shift) | (0xFF << (shift ^ 8));
        } else {
            // Set
            acpi.pmsts_en &= 0xFF << (shift ^ 8);
            acpi.pmsts_en |= data << shift;
        }
        break;
    case 4: // PM Control
        acpi.pmcntrl &= ~(0xFF << shift);
        acpi.pmcntrl |= data << shift;
        if (acpi.pmsts_en & (1 << 13)) { // SUS_EN
            // Suspend
            if ((acpi.pmiose >> 10 & 7) != 5) // SYS_TYP
                ACPI_FATAL("Unimplemented: Suspend state %d\n", acpi.pmiose >> 10 & 7);
            acpi.pmsts_en ^= 1 << 13; // Toggle bit off
        }
        break;
    default:
        ACPI_FATAL("TODO: power management write: %04x data %04x\n", addr, data);
    }
}
static uint32_t acpi_sm_read(uint32_t addr)
{
    switch(addr & 0xF){
        case 0:
            // TODO: status register
            return 0;
        case 2:
            // TODO: control
            return 0;
        case 3:
            // TODO: command
            return 0;
        case 4:
            // TODO: address
            return 0;
        case 5 ... 6:
            //return acpi.smbus_data[~addr & 1];
            // TODO: data
            return 0;
        default:
            ACPI_FATAL("TODO: system management read: %04x\n", addr);
    }
}
static void acpi_sm_write(uint32_t addr, uint32_t data)
{
    switch(addr & 0xF){
        case 0:
            break;
        case 2:
            break;
        case 3:
            break;
        case 4:
            break;
        case 5 ... 6:
            break;
        default:
            ACPI_FATAL("TODO: system management read: %04x data %04x\n", addr, data);
    }
}

// Remap power management base addresses
static void acpi_remap_pmba(uint32_t io)
{
    ACPI_LOG("Remapping Power Management I/O ports to %04x\n", io);
    // Try to not conflict with DMA i/o ports
    if (acpi.pmba != 0) {
        io_unregister_read(acpi.pmba, 64);
        io_unregister_write(acpi.pmba, 64);
    }
    acpi.pmba = io & 0xFFC0;
    if (io != 0) {
        io_register_read(acpi.pmba, 64, acpi_pm_read, NULL, NULL);
        io_register_write(acpi.pmba, 64, acpi_pm_write, NULL, NULL);
    }
}
static void acpi_remap_smba(uint32_t io)
{
    ACPI_LOG("Remapping System Management I/O ports to %04x\n", io);
    // Try to not conflict with DMA i/o ports
    if (acpi.smba != 0) {
        io_unregister_read(acpi.smba, 64);
        io_unregister_write(acpi.smba, 64);
    }
    acpi.smba = io & 0xFFC0;
    if (io != 0) {
        io_register_read(acpi.smba, 64, acpi_sm_read, NULL, NULL);
        io_register_write(acpi.smba, 64, acpi_sm_write, NULL, NULL);
    }
}

// Called when PCI configuration space is modified
static int acpi_pci_write(uint8_t* ptr, uint8_t addr, uint8_t data)
{
    switch (addr) {
    case 0 ... 3: // Vendor ID and stuff
    case 4 ... 5:
        ptr[addr] = data;
        acpi.smiose = data & 1; // XXX: Correct? 
        return 0;
    case 6 ... 7: // PCI Device status register, most of the bits are hardwired. 
        return 0;
    case 8 ... 0x3B:
        return 1; // Read only
    case 0x3C: // "Interrupt Line. The value in this register has no affect on PIIX4 hardware operations."
        return 0;
    case 0x40 ... 0x43: // Power Management Base Address
        ptr[addr] = data | (addr == 0x40); // Bit 0 of byte 0x40 must be 1
        if (addr == 0x43)
            acpi_remap_pmba(read32le(ptr, 0x40));
        return 0;
    case 0x58 ... 0x5B: // Device Activity B
        return 0; // Don't know what to do here.
    case 0x80: // Power management base address
        acpi.pmiose = data & 1;
        return 0;
    case 0x90 ... 0x93: // System Management Base Address
        ptr[addr] = data | (addr == 0x90); // Bit 0 of byte 0x90 must be 1
        if (addr == 0x93)
            acpi_remap_smba(read32le(ptr, 0x90));
        return 0;
    case 0xD2: // SMBus host configuration
        acpi.smiose = data & 1;
        if ((data >> 1 & 7) != 4) {
            ACPI_FATAL("Unknown SMBus interrupt delivery mechanism\n");
        }
        return 0;
    }
    ACPI_FATAL("Unknown write: %p addr=%02x data=%02x\n", ptr, addr, data);
}

// ACPI timer
int acpi_next(itick_t now_tick)
{
    if (acpi.enabled == 0)
        return -1;

    // ACPI timer
    uint32_t now = acpi_get_clock(now_tick) & 0x00FFFFFF,
             then = acpi.last_pm_clock & 0x00FFFFFF,
             raise_irq = 0;
    if (now < then) { // overflowed from 0xFFFFFF --> 0
        raise_irq = 1;
    }
    if (acpi.pmsts_en & (1 << 16)) {
        // Find out when we overflow our 23 bit counter
        acpi.pmsts_en |= 1;
        if (raise_irq) {
            pic_raise_irq(9);
        }else
            pic_lower_irq(9);

        acpi.last_pm_clock = acpi_get_clock(now_tick);
        // Now find transition time from now.
        uint32_t ticks_left = 0x1000000 - now;
        // Convert it into ticks
        return (double)ticks_left * (double)ticks_per_second / (double)ACPI_CLOCK_SPEED;
    } else {
        pic_lower_irq(9);
        return -1; // No timer enabled, ignore
    }
}

static void acpi_state(void)
{
    // <<< BEGIN AUTOGENERATE "state" >>>
    struct bjson_object* obj = state_obj("acpi", 8);
    state_field(obj, 4, "acpi.enabled", &acpi.enabled);
    state_field(obj, 4, "acpi.pmba", &acpi.pmba);
    state_field(obj, 4, "acpi.pmiose", &acpi.pmiose);
    state_field(obj, 4, "acpi.pmsts_en", &acpi.pmsts_en);
    state_field(obj, 4, "acpi.pmcntrl", &acpi.pmcntrl);
    state_field(obj, 4, "acpi.last_pm_clock", &acpi.last_pm_clock);
    state_field(obj, 4, "acpi.smba", &acpi.smba);
    state_field(obj, 4, "acpi.smiose", &acpi.smiose);
// <<< END AUTOGENERATE "state" >>>
    // Remap IO
    acpi_remap_pmba(acpi.pmba);
    acpi_remap_smba(acpi.smba);
}

void acpi_init(struct pc_settings* pc)
{
    if (!pc->acpi_enabled)
        return;
    
    acpi.enabled = 1;
    // Make sure that PCI is enabled, too
    if (!pc->pci_enabled)
        ACPI_LOG("Disabling ACPI because PCI is disabled\n");

    // Now register PCI handlers and callbacks
    io_register_reset(acpi_reset);
    state_register(acpi_state);

    // TODO: I randomly selected bus #7. Can we reconfigure this?
    uint8_t* ptr = pci_create_device(0, 7, 0, acpi_pci_write);
    pci_copy_default_configuration(ptr, (void*)acpi_configuration_space, 256);
    // HACK: Pretend system management mode init is already done.
    // We don't actually support SMM yet.
    ptr[0x5B] |= 2;
}