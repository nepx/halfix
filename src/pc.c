#include "pc.h"
#include "cpuapi.h"
#include "devices.h"
#include "display.h"
#include "io.h"
#include "state.h"
#include "util.h"

// Comment below line to disable automatic loading of savestate
//#define SAVESTATE
#define DISABLE_RESTORE
// Comment below line to disable automatic saving.
#define DISABLE_CONSTANT_SAVING

static inline void pc_cmos_lowhi(int idx, int data)
{
    if (data > 0xFFFF)
        data = 0xFFFF;
    cmos_set(idx, data);
    cmos_set(idx + 1, data >> 8);
}

// seabios firmware configuration
#define FW_CFG_SIGNATURE 0x00
#define FW_CFG_ID 0x01
#define FW_CFG_UUID 0x02
#define FW_CFG_RAM_SIZE 0x03
#define FW_CFG_NOGRAPHIC 0x04
#define FW_CFG_NB_CPUS 0x05
#define FW_CFG_MACHINE_ID 0x06
#define FW_CFG_KERNEL_ADDR 0x07
#define FW_CFG_KERNEL_SIZE 0x08
#define FW_CFG_KERNEL_CMDLINE 0x09
#define FW_CFG_INITRD_ADDR 0x0a
#define FW_CFG_INITRD_SIZE 0x0b
#define FW_CFG_BOOT_DEVICE 0x0c
#define FW_CFG_NUMA 0x0d
#define FW_CFG_BOOT_MENU 0x0e
#define FW_CFG_MAX_CPUS 0x0f
#define FW_CFG_MAX_ENTRY 0x10
static uint32_t bios_firmware_data, firmware_memory_size;

static uint8_t cmos12v = 0;
static void pc_init_cmos_disk(struct drive_info* drv, int id)
{
    if (drv->type == DRIVE_TYPE_DISK) {
        int shift = id << 2; // choose between 0 (first) or 4 (second)
        cmos12v |= 15 << (shift ^ 4);
        cmos_set(0x12, cmos12v);
        cmos_set(0x19 + (shift >> 1), 47); // 19 or 1A

        int base = 0x1B + id * 9;
        cmos_set(base + 0, drv->cylinders_per_head & 0xFF);
        cmos_set(base + 1, drv->cylinders_per_head >> 8 & 0xFF);
        cmos_set(base + 2, drv->heads);
        cmos_set(base + 3, 0xFF);
        cmos_set(base + 4, 0xFF);
        if (id == 0)
            cmos_set(base + 5, 0xC0 | ((drv->heads > 8) << 3));
        else
            cmos_set(base + 5, (drv->heads > 8) << 7);
        cmos_set(base + 6, drv->cylinders_per_head & 0xFF); // note: a mirroring of base + 0 and base + 1
        cmos_set(base + 7, drv->cylinders_per_head >> 8 & 0xFF);
        cmos_set(base + 8, drv->sectors_per_cylinder);

        int translation_id = 0x39 + (id >> 1);

        int translation_type = drv->sectors >= 1032192 ? 2 : 0; // Use LARGE translation mode

        if (id & 1)
            cmos_set(translation_id, (cmos_get(translation_id) & 0x0F) | (translation_type << 4));
        else
            cmos_set(translation_id, (cmos_get(translation_id) & 0xF0) | translation_type);
    }
}

static inline void pc_init_cmos(struct pc_settings* pc)
{
    // Set CMOS defaults
    // http://stanislavs.org/helppc/cmos_ram.html
    // http://bochs.sourceforge.net/doc/docbook/development/cmos-map.html
    // http://www.bioscentral.com/misc/cmosmap.htm

    // Registers 0x10, 0x12, 0x14 are set in their respective files
    cmos_set(0x0F, 0); // shutdown ok
    //cmos_set(0x10, floppy_get_type(0) << 4 | floppy_get_type(1));
    cmos_set(0x11, 0x80);
    cmos_set(0x13, 0x80);
    cmos_set(0x14, 0b00000110); // Bits 6-7 are OR'ed in
    pc_cmos_lowhi(0x15, 640);
    int em = (pc->memory_size - (1 << 20)) / 1024;
    pc_cmos_lowhi(0x17, em);
    pc_cmos_lowhi(0x30, em);
    int em64;
    if (pc->memory_size > (16 << 20))
        em64 = (pc->memory_size / 65536) - ((16 << 20) / 65536);
    else
        em64 = 0;
    pc_cmos_lowhi(0x34, em64);

    cmos_set(0x2D, (pc->boot_sequence[0] == BOOT_FLOPPY) << 5);

    cmos_set(0x32, 0x19); // Century in BCD
    cmos_set(0x37, 0x19); // Century in BCD

    cmos_set(0x38, pc->boot_sequence[2] << 4);
    cmos_set(0x3D, pc->boot_sequence[1] << 4 | pc->boot_sequence[0]);

    //cmos_set(0x39, 0);
    //cmos_set(0x39, 0x55); // LBA for all

    pc_cmos_lowhi(0x5B, 0); // no memory over 4GB
    cmos_set(0x5D, 0);

    pc_init_cmos_disk(&pc->drives[0], 0);
    pc_init_cmos_disk(&pc->drives[1], 1);
}

// Some BIOS-specific stuff
static int a20 = 2;
static char bios_data[2][101];
static int bios_ptr[2];

static void bios_writeb(uint32_t port, uint32_t data)
{
    int id;
    switch (port) {
    case 0x510: // BIOS firmware configuration port
        // bios_config_data
        switch (data) {
        case FW_CFG_SIGNATURE:
            bios_firmware_data = 0xFAB0FAB0;
            break;
        case FW_CFG_RAM_SIZE:
            bios_firmware_data = firmware_memory_size;
            break;
        case FW_CFG_NB_CPUS:
            bios_firmware_data = 1;
            break;
        }
        break;
    case 0x8900: {
        static const unsigned char shutdown[8] = "Shutdown";
        static int idx = 0;
        if (data == shutdown[idx++]) {
            if (idx == 8) {
                LOG("PC", "Shutdown requested\n");
                pc_hlt_if_0();
            } else {
                idx = 0;
            }
        }
        break;
    }
    case 0x92:
        a20 = data;
        cpu_set_a20(a20 >> 1 & 1);
        break;
    case 0x500:
    case 0x400:
    case 0x402:
    case 0x403:
    case 0x401:
        if (data == 0)
            return;
        id = port >> 8 & 1;
        bios_data[id][bios_ptr[id]++] = data;
        if (bios_ptr[id] == 100 || data == '\n') {
            bios_data[id][bios_ptr[id]] = 0;
            fprintf(stderr, "%sBIOS says: '%s'\n", id ? "VGA" : "", bios_data[id]);
            printf("%sBIOS says: '%s'\n", id ? "VGA" : "", bios_data[id]);
            bios_ptr[id] = 0;
        }
        break;
    }
}

void pc_set_a20(int state)
{
    a20 = state << 1;
}
uint8_t p61_data;
static uint32_t bios_readb(uint32_t port)
{
    switch (port) {
    case 0xB3:
        return 0;
    case 0x92:
        return a20;
    case 0x511: {
        int temp = bios_firmware_data & 0xFF;
        bios_firmware_data >>= 8;
        return temp;
    }
    default:
        return -1;
    }
}
static void default_mmio_writeb(uint32_t a, uint32_t b)
{
    UNUSED(a);
    UNUSED(b);
    LOG("PC", "Writing 0x%x to address 0x%x\n", b, a);
}
static uint32_t default_mmio_readb(uint32_t a)
{
    UNUSED(a);
    LOG("PC", "Reading from address 0x%x\n", a);
    return -1;
}

#ifdef EMSCRIPTEN
#undef SAVESTATE
#define DISABLE_CONSTANT_SAVING
#endif

// XXX Very very bad hack to make timing work (see util.c)
void util_state(void);

int pc_init(struct pc_settings* pc)
{
    if (cpu_init() == -1)
        return -1;
    cpu_set_cpuid(&pc->cpu);
    io_init();
    dma_init();
    cmos_init(pc->current_time);
    pc_init_cmos(pc); // must come before floppy initalization b/c reg 0x14
    fdc_init(pc);
    pit_init();
    pic_init(pc);
    kbd_init();
    vga_init(pc);
    ide_init(pc);

    // If pc.enable_<component> is set to zero, then the function calls will do nothing.
    pci_init(pc);
    apic_init(pc);
    ioapic_init(pc);
    acpi_init(pc);

    //cpu_set_a20(0); // causes code to be prefetched from 0xFFEFxxxx at boot
    cpu_set_a20(1);

    io_trigger_reset();

    display_init();

    //io_register_read(0x61, 1, bios_readb, NULL, NULL);
    io_register_read(0xB3, 1, bios_readb, NULL, NULL);
    io_register_read(0x511, 1, bios_readb, NULL, NULL);
    io_register_write(0x510, 1, NULL, bios_writeb, NULL);
    io_register_write(0x8900, 1, bios_writeb, NULL, NULL);

    // Random ports
    io_register_write(0x400, 4, bios_writeb, NULL, NULL);
    io_register_write(0x500, 1, bios_writeb, NULL, NULL);
    //io_register_write(0x80, 1, bios_writeb, NULL, NULL); // Linux/Bochs BIOS uses this as a delay port

    io_register_read(0x378, 8, bios_readb, NULL, NULL);
    io_register_write(0x378, 8, bios_writeb, NULL, NULL);
    io_register_read(0x278, 8, bios_readb, NULL, NULL);
    io_register_write(0x278, 8, bios_writeb, NULL, NULL);
    io_register_read(0x3f8, 8, bios_readb, NULL, NULL);
    io_register_write(0x3f8, 8, bios_writeb, NULL, NULL);
    io_register_read(0x2f8, 8, bios_readb, NULL, NULL);
    io_register_write(0x2f8, 8, bios_writeb, NULL, NULL);
    io_register_read(0x3e8, 8, bios_readb, NULL, NULL);
    io_register_write(0x3e8, 8, bios_writeb, NULL, NULL);
    io_register_read(0x2e8, 8, bios_readb, NULL, NULL);
    io_register_write(0x2e8, 8, bios_writeb, NULL, NULL);
    io_register_read(0x92, 1, bios_readb, NULL, NULL);
    io_register_write(0x92, 1, bios_writeb, NULL, NULL);
    io_register_read(0x510, 2, bios_readb, NULL, NULL);
    io_register_write(0x510, 2, bios_writeb, NULL, NULL);

    // Bochs BIOS writes to these after IDE initialization
    io_register_read(0x3e0, 8, bios_readb, NULL, NULL);
    io_register_write(0x3e0, 8, bios_writeb, NULL, NULL);
    io_register_read(0x360, 16, bios_readb, NULL, NULL);
    io_register_write(0x360, 16, bios_writeb, NULL, NULL);
    io_register_read(0x1e0, 16, bios_readb, NULL, NULL);
    io_register_write(0x1e0, 16, bios_writeb, NULL, NULL);
    io_register_read(0x160, 16, bios_readb, NULL, NULL);
    io_register_write(0x160, 16, bios_writeb, NULL, NULL);

    // Windows 95
    io_register_write(0x2f0, 8, bios_writeb, NULL, NULL);
    io_register_read(0x270, 16, bios_readb, NULL, NULL);
    io_register_write(0x6f0, 8, bios_writeb, NULL, NULL);
    io_register_read(0x420, 2, bios_readb, NULL, NULL);
    io_register_read(0x4a0, 2, bios_readb, NULL, NULL);
    io_register_write(0xa78, 2, bios_writeb, NULL, NULL);

    // OS/2
    io_register_write(0x22, 4, bios_writeb, NULL, NULL);
    io_register_read(0x22, 4, bios_readb, NULL, NULL);
#if 0
    // ET4000 uses this port
    // https://stuff.mit.edu/afs/athena/astaff/project/x11r6/src/xc/programs/Xserver/hw/xfree86/SuperProbe/Probe.h
    io_register_write(0x46E8, 8, bios_writeb, NULL, NULL);
    io_register_write(0x42E8, 8, bios_writeb, NULL, NULL);
    io_register_write(0x92E8, 8, bios_writeb, NULL, NULL);
    io_register_read(0x92E8, 8, bios_readb, NULL, NULL);
    io_register_write(0x4AE8, 8, bios_writeb, NULL, NULL);
#endif

    if (!pc->pci_enabled) {
        io_register_write(0xCF8, 8, bios_writeb, bios_writeb, bios_writeb);
        io_register_read(0xCF8, 8, bios_readb, bios_readb, bios_readb);
    }

    if (!pc->apic_enabled) {
        io_register_mmio_write(0xFEE00000, 1 << 20, default_mmio_writeb, NULL, NULL);
        io_register_mmio_read(0xFEE00000, 1 << 20, default_mmio_readb, NULL, NULL);
    }

    // Check writes to ROM
    if (!pc->pci_enabled) {
        // PCI can control ROM areas using the Programmable Attribute Map Registers, so this will be handled there
        io_register_mmio_write(0xC0000, 0x40000, default_mmio_writeb, NULL, NULL);
    }

    // The rest is just CPU initialization
    firmware_memory_size = pc->memory_size;
    if (cpu_init_mem(pc->memory_size) == -1)
        return -1;
    if (pc->pci_enabled)
        pci_init_mem(cpu_get_ram_ptr());

    // Check if BIOS and VGABIOS have sane values
    if (((uintptr_t)pc->bios.data | (uintptr_t)pc->vgabios.data) & 0xFFF) {
        fprintf(stderr, "BIOS and VGABIOS need to be aligned on a 4k boundary\n");
        return -1;
    }
    if (!pc->bios.length || !pc->vgabios.length) {
        fprintf(stderr, "BIOS/VGABIOS length is zero\n");
        return 0;
    }
    int v = cpu_add_rom(0x100000 - pc->bios.length, pc->bios.length, pc->bios.data);
    v |= cpu_add_rom(-pc->bios.length, pc->bios.length, pc->bios.data);
    if (!pc->pci_vga_enabled)
        v |= cpu_add_rom(0xC0000, pc->vgabios.length, pc->vgabios.data);
    if (v == -1) {
        fprintf(stderr, "Unable to register ROM areas\n");
        return -1;
    }

    state_register(util_state);

    cpu_set_break();
#ifdef SAVESTATE
    state_read_from_file("savestates/halfix_state/");
#endif

    return 0;
}
static uint32_t devices_get_next_raw(itick_t now)
{
    uint32_t next[4], min = -1;
    next[0] = cmos_next(now);
    next[1] = pit_next(now);
    next[2] = apic_next(now);
    next[3] = acpi_next(now);
    for (int i = 0; i < 4; i++) {
        if (next[i] < min)
            min = next[i];
    }
    return min;
}

static uint32_t devices_get_next(itick_t now, int* devices_need_servicing)
{
    int min = devices_get_next_raw(now);
    if (cpu_get_exit_reason() == EXIT_STATUS_HLT)
        return min;
    if ((unsigned int)min > 200000) {
        if(devices_need_servicing)
            *devices_need_servicing = min - 200000;
        return 200000;
    } else {
        if(devices_need_servicing)
            *devices_need_servicing = 0;
        return min;
    }
}

void pc_hlt_if_0(void)
{
    // Called when HLT with IF=0 was called
    return;
}

//#define INSNS_PER_FRAME 100000000 // Windows 7, Vista
#define INSNS_PER_FRAME 50000000
static int sync = 0;
static uint64_t last = 0;

#ifdef EMSCRIPTEN
// Don't feel like wasting your time while waiting for HLT loops to complete? solution is below
static int fast = 0;

void pc_set_fast(int yes){
    fast = yes;
}
#endif
int pc_execute(void)
{
    // This function is called repeatedly.
    int frames = 10, cycles_to_run, cycles_run, exit_reason, devices_need_servicing = 0;
    itick_t now;

#ifdef EMSCRIPTEN
    uint64_t cur_now;
    if (fast)
        cur_now = cpu_get_cycles();
#endif

    // Call the callback if needed, for async drive cases
    drive_check_complete();

    sync++;
    if (!drive_async_event_in_progress() && (cpu_get_cycles() - last) > INSNS_PER_FRAME) {
// Verify that timing is identical
#ifndef DISABLE_CONSTANT_SAVING
        state_store_to_file("savestates/halfix_state");
#ifndef DISABLE_RESTORE
        state_read_from_file("savestates/halfix_state");
#endif
#endif
        sync = 0;
        last = cpu_get_cycles();
    }
    do {
        now = get_now();
        cycles_to_run = devices_get_next(now, &devices_need_servicing);
// Run a number of cycles.

#if 0
        uint64_t before = get_now();
#endif
        cycles_run = cpu_run(cycles_to_run);
//LOG("PC", "Exited from loop (cycles to run: %d, extra: %d)\n", cycles_to_run, devices_need_servicing);
#if 0
        if ((before + cycles_run) != get_now()) {
            printf("Before: %ld Ideal: %ld Current: %ld [diff: %ld] total insn should be run: %d dev need serv %d\n", before, cycles_run + before, get_now(), cycles_run + before - get_now(), cycles_run, devices_need_servicing);
            //abort();
        }
#endif
        if ((exit_reason = cpu_get_exit_reason())) {
            // We exited the loop because of a HLT instruction or an async function needs to be called.
            // Now skip forward a number of cycles, and determine how many ms we should sleep for
            int cycles_to_move_forward, wait_time, moveforward;
            cycles_to_move_forward = cycles_to_run - cycles_run;

            if (exit_reason == EXIT_STATUS_HLT) {
                // The below line should prevent the browser version from locking up
                if(!cpu_interrupts_masked()) return 0;
                moveforward = devices_get_next(get_now(), NULL);
                cycles_to_move_forward += moveforward;
            }
            add_now(cycles_to_move_forward);
            wait_time = (cycles_to_move_forward * 1000) / ticks_per_second;
#ifdef EMSCRIPTEN
            if (!fast) {
                if (wait_time != 0)
                    return wait_time;
            }
#else
            if(wait_time != 0) return 0; // try to match with emscripten
            UNUSED(wait_time);
#endif
            // Just continue since wait time is negligable
        }
#ifdef EMSCRIPTEN
        if (fast) {
            if ((cpu_get_cycles() - cur_now) > 2000000)
                return 0;
            else
                frames = -1;
        }
#endif
    } while (frames--);
    return 0;
}