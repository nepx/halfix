#ifndef PC_H
#define PC_H

#include "drive.h"
#include <stdint.h>

struct loaded_file
{
    uint32_t length;
    void *data;
};

enum
{
    DRIVE_TYPE_NONE = 0,
    DRIVE_TYPE_DISK,
    DRIVE_TYPE_CDROM
};

enum {
    CPU_TYPE_486,
    CPU_TYPE_PENTIUM,
    CPU_TYPE_PENTIUM_PRO,
    CPU_TYPE_PENTIUM_4,
    CPU_TYPE_CORE_DUO
};

// Important: do not free this struct or modify values in it after passing it to pc_init
struct pc_settings
{
    uint32_t memory_size, vga_memory_size;
    struct loaded_file bios, vgabios;

    unsigned int cpu_type;

    int
        // Setting pci_enabled to zero will disable direct memory disk accesses. Otherwise, the system will function identically to that of one without PCI support.
        pci_enabled,
        // Setting apic_enabled to zero will disable both the I/O APIC and the local APIC. Otherwise, the system will function identically to that of one without an APIC.
        apic_enabled,
        // Setting acpi_enabled to zero will disable the ACPI interface and make the system function identically to a system that is not ACPI-compliant
        acpi_enabled,
        // Settting vbe_enabled to zero will disable the Bochs VBE extensions (it will not disable the VBE functions in the BIOS, although the BIOS won't be able to enable VBE)
        vbe_enabled,
        // Setting pci_vga_enabled to zero will disable PCI VGA accleration. Note that in some cases, it will make screen updating slower due to how the Halfix fetch-decode-execute loop is implemented
        pci_vga_enabled;

    // Current time according to the CMOS clock
    uint64_t current_time;

    uint8_t boot_sequence[3];

    struct drive_info drives[4];

    // Set to 1 if floppy should be enabled
    int floppy_enabled;
    
    struct {
        // If the image is write-protected (note: has nothing to do with the drive_info write-protected bit)
        int write_protected;
    } floppy_settings[2];

    struct drive_info floppy_drives[2];
};

enum
{
    BOOT_NONE = 0,
    BOOT_FLOPPY = 1,
    BOOT_DISK = 2,
    BOOT_CDROM = 3
};

int pc_init(struct pc_settings *pc);
int pc_execute(void);
uint32_t pc_run(void);
void pc_set_a20(int state);
void pc_in_hlt(void);
void pc_hlt_if_0(void);
void pc_run_device_timers(void);
void pc_out_of_hlt(void);

void pc_hlt_state(void);

#endif
