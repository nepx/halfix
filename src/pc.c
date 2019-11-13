#include "pc.h"
#include "cpuapi.h"
#include "devices.h"
#include "display.h"
#include "io.h"
#include "state.h"
#include "util.h"

static struct cpu_config cpu_models[] = {
    [CPU_CLASS_486] = {.vendor_name = "GenuineIntel",
                       .level = 1,
                       .features[FEATURE_EAX_1] = {
                           .level = 1,
                           .eax = 0x402, // Intel 486DX
                           .ebx = 0,
                           .ecx = 0,
                           .edx = 1 // FPU
                       },
                       .features[FEATURE_EAX_80000000] = {.level = 0x80000000}},
    [CPU_CLASS_PENTIUM] = {.vendor_name = "GenuineIntel", .level = 1, .features[FEATURE_EAX_1] = {
                                                                          .level = 1,
                                                                          .eax = 0x513, // Intel Pentium
                                                                          .ebx = 0,
                                                                          .ecx = 0,
                                                                          .edx = 0x11 | (1 << 8) | (1 << 5) // FPU+TSC+RDTSC+CMPXCHG8B+MSR
                                                                      },
                           .features[FEATURE_EAX_80000000] = {.level = 0x80000000}},
    [CPU_CLASS_PENTIUM_PRO] = {.vendor_name = "GenuineIntel", .level = 1, .features[FEATURE_EAX_1] = {
                                                                              .level = 1,
                                                                              .eax = 0x611, // Intel Pentium Pro
                                                                              .ebx = 0,
                                                                              .ecx = 0,
                                                                              .edx = 0x11 | (1 << 8) | (1 << 5) | (1 << 15) | (1 << 13) // FPU+TSC+RDTSC+CMPXCHG8B+MSR+CMOV+PTE
                                                                          },
                               .features[FEATURE_EAX_80000000] = {.level = 0x80000000}},
};

static inline void pc_cmos_lowhi(int idx, int data)
{
    if (data > 0xFFFF)
        data = 0xFFFF;
    cmos_set(idx, data);
    cmos_set(idx + 1, data >> 8);
}

static uint8_t cmos12v = 0;
static void pc_init_cmos_disk(struct drive_info *drv, int id)
{
    if (drv->type == DRIVE_TYPE_DISK)
    {
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

static inline void pc_init_cmos(struct pc_settings *pc)
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
    switch (port)
    {
    case 0x8900:
    {
        static const unsigned char shutdown[8] = "Shutdown";
        static int idx = 0;
        if (data == shutdown[idx++])
        {
            if (idx == 8)
            {
                LOG("PC", "Shutdown requested\n");
                pc_hlt_if_0();
            }
            else
            {
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
        if (bios_ptr[id] == 100 || data == '\n')
        {
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
    switch (port)
    {
    case 0x92:
        return a20;
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

int pc_init(struct pc_settings *pc)
{
    if (cpu_init() == -1)
        return -1;
    io_init();
    dma_init();
    cmos_init(pc->current_time);
    pc_init_cmos(pc); // must come before floppy initalization b/c reg 0x14
    fdc_init(pc);
    pit_init();
    pic_init(pc);
    kbd_init();
    vga_init(pc->vga_memory_size ? pc->vga_memory_size : 2 * 1024 * 1024);
    ide_init(pc);

    // Now come the optional components -- PCI and APIC. Halfix can simulate an ISA PC very well, but OSes like Windows XP don't like that so much.
    // If pc.enable_<component> is set to zero, then the function calls will do nothing.
    pci_init(pc);
    apic_init(pc);
    ioapic_init(pc);

    //cpu_set_a20(0); // causes code to be prefetched from 0xFFEFxxxx at boot
    cpu_set_a20(1);

    io_trigger_reset();

    display_init();

    //io_register_read(0x61, 1, bios_readb, NULL, NULL);
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

    if (!pc->pci_enabled)
    {
        io_register_write(0xCF8, 8, bios_writeb, bios_writeb, bios_writeb);
        io_register_read(0xCF8, 8, bios_readb, bios_readb, bios_readb);
    }

    if (!pc->apic_enabled)
    {
        io_register_mmio_write(0xFEE00000, 1 << 20, default_mmio_writeb, NULL, NULL);
        io_register_mmio_read(0xFEE00000, 1 << 20, default_mmio_readb, NULL, NULL);
    }

    // Check writes to ROM
    if (!pc->pci_enabled)
    {
        // PCI can control ROM areas using the Programmable Attribute Map Registers, so this will be handled there
        io_register_mmio_write(0xC0000, 0x40000, default_mmio_writeb, NULL, NULL);
    }

    // The rest is just CPU initialization
    if (cpu_init_mem(pc->memory_size) == -1)
        return -1;
    if (pc->pci_enabled)
        pci_init_mem(cpu_get_ram_ptr());

    // Initialize CPUID
    if (pc->cpu_type >= CPU_CLASS_MAXIMUM)
    {
        fprintf(stderr, "Unknown CPU class: %d\n", pc->cpu_type);
        return -1;
    }
    struct cpu_config *cfg = &cpu_models[pc->cpu_type];
    if (cpu_set_cpuid(cfg) == -1)
        return -1;

    // Check if BIOS and VGABIOS have sane values
    if (((uintptr_t)pc->bios.data | (uintptr_t)pc->vgabios.data) & 0xFFF)
    {
        fprintf(stderr, "BIOS and VGABIOS need to be aligned on a 4k boundary\n");
        return -1;
    }
    if (!pc->bios.length || !pc->vgabios.length)
    {
        fprintf(stderr, "BIOS/VGABIOS length is zero\n");
        return 0;
    }
    int v = cpu_add_rom(0x100000 - pc->bios.length, pc->bios.length, pc->bios.data);
    v |= cpu_add_rom(-pc->bios.length, pc->bios.length, pc->bios.data);
    v |= cpu_add_rom(0xC0000, pc->vgabios.length, pc->vgabios.data);
    if (v == -1)
    {
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
    next[3] = floppy_next(now);
    for (int i = 0; i < 4; i++)
    {
        if (next[i] < min)
            min = next[i];
    }
    return min;
}

static uint32_t devices_get_next(itick_t now, int *devices_need_servicing)
{
    int min = devices_get_next_raw(now);
    if ((unsigned int)min > 200000)
    {
        *devices_need_servicing = min - 200000;
        return 200000;
    }
    else
    {
        *devices_need_servicing = 0;
        return min;
    }
}

void pc_hlt_if_0(void)
{
    // Called when HLT with IF=0 was called
    return;
}

#define SYNC_POINTS_PER_SECOND 8
static int sync = 0;
#ifndef DISABLE_CONSTANT_SAVING
#endif

int pc_execute(void)
{
    // This function is called repeatedly.
    int frames = 10, cycles_to_run, cycles_run, exit_reason, devices_need_servicing = 0;
    itick_t now;
    sync++;
    if (sync == SYNC_POINTS_PER_SECOND)
    {
// Verify that timing is identical
#ifndef DISABLE_CONSTANT_SAVING
        state_store_to_file("savestates/halfix_state");
#ifndef DISABLE_RESTORE
        state_read_from_file("savestates/halfix_state");
#endif
#endif
        sync = 0;
    }
    // Call the callback if needed, for async drive cases
    drive_check_complete();
    do
    {
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
        if ((exit_reason = cpu_get_exit_reason()))
        {
            // We exited the loop because of a HLT instruction or an async function needs to be called.
            // Now skip forward a number of cycles, and determine how many ms we should sleep for
            int cycles_to_move_forward, wait_time;
            cycles_to_move_forward = cycles_to_run - cycles_run;

            if (exit_reason == EXIT_STATUS_HLT)
                cycles_to_move_forward += devices_need_servicing;
            add_now(cycles_to_move_forward);
            wait_time = (cycles_to_move_forward * 1000) / ticks_per_second;
            if (wait_time != 0)
                return wait_time;
            // Just continue since wait time is negligable
        }
    } while (frames--);
    return 0;
}