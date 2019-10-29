// 82077AA floppy disk controller (FDC)
// http://www.buchty.net/casio/files/82077.pdf
// https://wiki.osdev.org/Floppy_Disk_Controller

// TODO: Seek times

#include "drive.h"
#include "io.h"
#include "pc.h"
#include "util.h"
#include <stdint.h>

#define FLOPPY_LOG(x, ...) LOG("FLOPPY", x, ##__VA_ARGS__)
#define FLOPPY_FATAL(x, ...) \
    FATAL("FLOPPY", x, ##__VA_ARGS__);

#define DOR_MOTD 0x80
#define DOR_MOTC 0x40
#define DOR_MOTB 0x20
#define DOR_MOTA 0x10
#define DOR_IRQ 0x08
#define DOR_RESET 0x04
#define DOR_DSEL1 0x02
#define DOR_DSEL0 0x01

#define MSR_RQM 0x80
#define MSR_DIO 0x40
#define MSR_NDMA 0x20
#define MSR_CB 0x10 // Similar to IDE BSY
#define MSR_ACTD 0x08
#define MSR_ACTC 0x04
#define MSR_ACTB 0x02
#define MSR_ACTA 0x01

struct fdc {
    uint8_t status[2]; // 3F0/3F1
    uint8_t dor; // 3F2
    uint8_t tape_drive; // 3F3, unused
    uint8_t msr, data_rate; // 3F4
    uint8_t fifo; // 3F5
    // 3F6 is reserved for IDE
    uint8_t dir, ccr; // 3F7

    // Cache seek track, head, and sector, and also cache internal LBA address for fast lookups.
    uint32_t seek_track, seek_head, seek_sector, seek_internal_lba;

    // There are two floppy ribbons, with two drives attached to each
    struct drive_info* drives[4];
    struct fdc_drive_info {
        uint32_t inserted;
        uint32_t size;
        uint32_t heads; // Either 1 or 2, depending on if the bottom side of the floppy can be read or not
        uint32_t tracks;
        uint32_t spt; // Sectors per track
    } drive_info[4];
} fdc;

#if 0
// Seek to position in drive
static int fdc_seek(int drv, uint32_t track, uint32_t head, uint32_t sector)
{
    struct fdc_drive_info* info = &fdc.drive_info[drv];
    // Verify all parameters before modifying internal state
    if (info->tracks < track || info->heads < head || info->spt < sector || !sector)
        return -1; // Seek failed

    fdc.seek_track = track;
    fdc.seek_head = head;
    fdc.seek_sector = sector;
    fdc.seek_internal_lba = (head * (info->spt * info->tracks)) + (track * info->spt) + (sector - 1);
}
#endif

static void fdc_reset(void)
{
}

static uint32_t fdc_read(uint32_t port){
    switch(port){
        case 0x3F0: // Status register A
        case 0x3F1: // Status register B
            return fdc.status[port & 1];
        default:
            FLOPPY_FATAL("Unknown port read: %04x\n", port);
    }
}
static void fdc_write(uint32_t port, uint32_t data){
    switch(port){
        default:
            FLOPPY_FATAL("Unknown port write: %04x data: %02x\n", port, data);
    }
}

void fdc_init(struct pc_settings* pc)
{
    if (!pc->floppy_enabled)
        return;

    // Register I/O handlers
    io_register_reset(fdc_reset);

    // Register I/O
    io_register_read(0x3F0, 6, fdc_read, NULL, NULL);
    io_register_write(0x3F0, 6, fdc_write, NULL, NULL);
    for (int i = 0; i < 4; i++) {
        struct drive_info* drive = &pc->drives[i];
        if (drive->type == DRIVE_TYPE_NONE)
            continue;

        struct fdc_drive_info* info = &fdc.drive_info[i];
        info->inserted = 1;

        switch (drive->sectors) {
#define MAKE_DISK_TYPE(h, t, sectors_per_track)         \
    case (h * t * sectors_per_track):                   \
        info->size = (h * t * sectors_per_track) * 512; \
        info->heads = h;                                \
        info->tracks = t;                               \
        info->spt = sectors_per_track;                  \
        break
            MAKE_DISK_TYPE(1, 40, 8); // 160K
            MAKE_DISK_TYPE(1, 40, 9); // 180K
            MAKE_DISK_TYPE(2, 40, 8); // 320K
            MAKE_DISK_TYPE(2, 40, 9); // 360K
            MAKE_DISK_TYPE(2, 80, 8); // 640K
            MAKE_DISK_TYPE(2, 80, 9); // 720K
            MAKE_DISK_TYPE(2, 80, 15); // 1220K
            MAKE_DISK_TYPE(2, 80, 21); // 1680K
            MAKE_DISK_TYPE(2, 80, 23); // 1840K
            MAKE_DISK_TYPE(2, 80, 36); // 2880K
        default:
            printf("Unknown disk size: %d, defaulting to 1440K\n", drive->sectors);
            break;
            MAKE_DISK_TYPE(2, 80, 18); // 1440K
        }
    }
}