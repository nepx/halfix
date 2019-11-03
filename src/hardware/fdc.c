// 82077AA floppy disk controller (FDC)
// http://www.buchty.net/casio/files/82077.pdf
// https://wiki.osdev.org/Floppy_Disk_Controller
// http://stanislavs.org/helppc/765.html

// TODO: Seek times

#include "devices.h"
#include "drive.h"
#include "io.h"
#include "pc.h"
#include "util.h"
#include <stdint.h>

#define FLOPPY_LOG(x, ...) LOG("FLOPPY", x, ##__VA_ARGS__)
#define FLOPPY_FATAL(x, ...) \
    FATAL("FLOPPY", x, ##__VA_ARGS__);

#define SRA_DIR 0x01
#define SRA_WP 0x02
#define SRA_INDX 0x04
#define SRA_HDSEL 0x08
#define SRA_TRK0 0x10
#define SRA_STEP 0x20
#define SRA_DRV2 0x40
#define SRA_INTPEND 0x80

#define SRB_MTR0 0x01
#define SRB_MTR1 0x02
#define SRB_WGATE 0x04
#define SRB_RDATA 0x08
#define SRB_WDATA 0x10
#define SRB_DR0 0x20
#define SRB_DRV2 0x80

#define SR0_EQPMT 0x10
#define SR0_SEEK 0x20
#define SR0_ABNTERM 0x40
#define SR0_INVCMD 0x80
#define SR0_RDY (SR0_ABNTERM | SR0_INVCMD)

#define SR1_NO_SECTOR 0x04
#define SR1_EOC 0x80

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
    /// ignore: dmabuf
    uint8_t status[2]; // 3F0/3F1
    uint8_t dor; // 3F2
    uint8_t tape_drive; // 3F3, unused
    uint8_t msr, data_rate; // 3F4
    uint8_t fifo; // 3F5
    // 3F6 is reserved for IDE
    uint8_t dir, ccr; // 3F7

    // Status registers
    uint8_t st[4];

    int selected_drive;

    // For read/write
    int multi_mode;

    // Command bytes sent to controller
    uint8_t command_buffer[16];
    uint8_t command_buffer_size; // Number of bytes that controller wants
    uint8_t command_buffer_pos; // Number of bytes sent to controller

    // Response bytes to be read by controller
    uint8_t response_buffer[16];
    uint8_t response_buffer_size;
    uint8_t response_pos;

    // Cache seek cylinder, head, and sector, and also cache internal LBA address for fast lookups.
    uint32_t seek_cylinder[2], seek_head[2], seek_sector[2], seek_internal_lba[2];

    // Interrupt countdown, needed for command 8
    int interrupt_countdown;

    // There are two floppy ribbons, with two drives attached to each
    struct drive_info* drives[4];
    struct fdc_drive_info {
        uint32_t inserted;
        uint32_t size;
        uint32_t heads; // Either 1 or 2, depending on if the bottom side of the floppy can be read or not
        uint32_t cylinders;
        uint32_t spt; // Sectors per cylinder
        int write_protected;
    } drive_info[4];

    uint8_t dmabuf[64 << 10];
} fdc;

// Seek to position in drive
static int fdc_seek(int drv, uint32_t cylinder, uint32_t head, uint32_t sector)
{
    struct fdc_drive_info* info = &fdc.drive_info[drv];
    // Verify all parameters before modifying internal state
    if (info->cylinders < cylinder || info->heads < head || info->spt < sector || !sector)
        return -1; // Seek failed

    fdc.seek_cylinder[drv] = cylinder;
    fdc.seek_head[drv] = head;
    fdc.seek_sector[drv] = sector;
    fdc.seek_internal_lba[drv] = ((head + cylinder * info->heads) * info->spt) + (sector - 1);
    return 0;
}

#define CUR_CYLINDER() fdc.seek_cylinder[fdc.selected_drive]
#define CUR_HEAD() fdc.seek_head[fdc.selected_drive]
#define CUR_SECTOR() fdc.seek_sector[fdc.selected_drive]

static void fdc_reset(int hardware)
{
    fdc.status[1] = 0xC0;
    fdc.selected_drive = 0;
    fdc.dor = DOR_RESET | DOR_IRQ;
    fdc.msr = MSR_RQM;

    fdc.command_buffer_size = 0;
    fdc.command_buffer_pos = 0;

    for (int i = 0; i < 2; i++) {
        if (fdc.drive_info[i].inserted) {
            fdc.seek_sector[i] = 1;
            fdc.seek_head[i] = 0;
            fdc.seek_cylinder[i] = 0;
        } else
            fdc_seek(i, hardware ? 0 : fdc.seek_cylinder[i], 0, 1);
    }
}

// Called by io_trigger_reset
static void fdc_reset2(void)
{
    fdc_reset(1);
}

static int y = 0;
static void fdc_lower_irq(void)
{
    fdc.status[0] &= ~SRA_INTPEND;
    pic_lower_irq(6);
}
static void fdc_raise_irq(void)
{
    fdc.interrupt_countdown = 0;
    fdc.status[0] |= SRA_INTPEND;
    if(y++ == 3) __asm__("int3");
    printf(" irq 6 Time: %d\n", y++);
    pic_raise_irq(6);
}

static inline void reset_cmd_fifo(void)
{
    fdc.command_buffer_pos = 0;
    fdc.command_buffer_size = 0;
}

static inline void reset_out_fifo(int size)
{
    fdc.response_buffer_size = size;
    fdc.response_pos = 0;
    if (size) {
        fdc.msr |= MSR_CB | MSR_DIO; // there are bytes to be read
    } else {
        fdc.msr &= ~(MSR_CB | MSR_DIO); // no more bytes
    }
}

static int x = 0;
static uint32_t fdc_read(uint32_t port)
{
    switch (port) {
    case 0x3F0: // Status register A
    case 0x3F1: // Status register B
        FLOPPY_LOG("Read from status register %c\n", 'A' + (port & 1));
        return fdc.status[port & 1];
    case 0x3F2:
        FLOPPY_LOG("Read from DOR\n");
        return fdc.dor;
    case 0x3F4:
        if(x++ == 25) __asm__("int3");
        FLOPPY_LOG("Read from MSR (fifo: pos=%d v sz=%d)\n", fdc.response_pos, fdc.response_buffer_size);
        return fdc.msr;
    case 0x3F5: { // Read from FIFO out queue
        FLOPPY_LOG("Read from output queue\n");
        // If there is nothing to read, then return dummy value
        if (!fdc.response_buffer_size)
            return 0;
        uint8_t res = fdc.response_buffer[fdc.response_pos++];
        if (fdc.response_pos == fdc.response_buffer_size) {
            reset_out_fifo(0);
        }
        return res;
    }
    default:
        FLOPPY_FATAL("Unknown port read: %04x\n", port);
    }
}

static void fdc_set_st0(int val)
{
    fdc.st[0] = (fdc.st[0] & ~7) | (val & 0xF8);
}
static uint8_t fdc_get_st0(void)
{
    return fdc.st[0] | (fdc.seek_head[fdc.selected_drive] << 2) | fdc.selected_drive;
}

// Sets status registers indicating command failure
static void fdc_abort_command(void)
{
    // Command was abnormally terminad
    switch (fdc.command_buffer[0]) {
    case 0x06:
    case 0x26:
    case 0x46:
    case 0x66:
    case 0x86:
    case 0xA6:
    case 0xC6:
    case 0xE6: // Read sectors
        fdc_set_st0(SR0_ABNTERM);
        fdc.st[1] = SR1_NO_SECTOR;
        fdc.st[2] = 0;
        break;
    default:
        FLOPPY_FATAL("Unknonw floppy abort handler: %02x\n", fdc.command_buffer[0]);
    }
}

static void fdc_finish_read(void* this, int x)
{
    UNUSED(this);
    if (x != 0)
        FLOPPY_FATAL("TODO: Handle drive read error\n");

    // Clear bsy bit
    fdc.msr &= ~MSR_CB;
    fdc.msr |= (fdc.msr & MSR_NDMA ? (MSR_RQM | MSR_DIO) : 0);

    dma_raise_dreq(2);

    // Execution will continue at fdc_handle_transfer_end
}

static void fdc_write(uint32_t port, uint32_t data)
{
    uint8_t diffxor;
    switch (port) {
    case 0x3F0: // Status register A
    case 0x3F1: // Status register B
        FLOPPY_LOG("Write to status register %c (ignored)\n", 'A' + (port & 1));
        break;
    case 0x3F2: // Digital Output Register
        diffxor = fdc.dor ^ data;
        if ((data | diffxor) & DOR_IRQ)
            fdc_lower_irq();
        if (diffxor & DOR_MOTA) {
            if (data & DOR_MOTA)
                fdc.status[1] |= SRB_MTR0;
            else
                fdc.status[1] &= ~SRB_MTR0;
        }
        if (diffxor & data & DOR_MOTB) {
            if (data & DOR_MOTA)
                fdc.status[1] |= SRB_MTR1;
            else
                fdc.status[1] &= ~SRB_MTR1;
        }

        if (diffxor & data & DOR_DSEL0)
            fdc.status[1] |= SRB_DR0;
        else
            fdc.status[1] &= ~SRB_DR0;

        if (diffxor & DOR_RESET) {
            // Unlike IDE reset, floppy disk reset happens when the reset bit goes from 1 -> 0 -> 1
            if (data & DOR_RESET) {
                FLOPPY_LOG("Drive reset\n");
                fdc_reset(0);

                fdc_raise_irq();
                fdc.interrupt_countdown = 4;
            } else
                FLOPPY_LOG("Drive being reset\n");
        }
        fdc.selected_drive = data & 3;
        fdc.dor = data;
        break;
    case 0x3F4: // Data rate selection. Currently unused.
        fdc.data_rate = data;
        break;
    case 0x3F5: // Write FIFO command byte
        fdc.command_buffer[fdc.command_buffer_pos++] = data;
        if (fdc.command_buffer_pos != fdc.command_buffer_size) {
            if (fdc.command_buffer_size == 0) {
                // Starting a new command
                switch (data) {
                // Read
                case 0x06:
                case 0x26:
                case 0x46:
                case 0x66:
                case 0x86:
                case 0xA6:
                case 0xC6:
                case 0xE6:
                    fdc.multi_mode = data >> 7;
                    fdc.command_buffer_size = 9;
                    break;
                case 7: // Calibrate drive
                    fdc.command_buffer_size = 2;
                    break;
                case 8: // Sense interrupt
                    FLOPPY_LOG("Sense interrupt\n");
                    if (fdc.interrupt_countdown != 0) {
                        // XXX ?
                        int id = 3 ^ fdc.interrupt_countdown--;
                        fdc_set_st0(SR0_RDY);
                        fdc.response_buffer[0] = fdc.st[0] | (fdc.seek_head[id] << 2) | id;
                    } else {
                        fdc_set_st0(SR0_SEEK);
                        fdc.response_buffer[0] = fdc_get_st0();
                    }

                    fdc.response_buffer[1] = CUR_CYLINDER();
                    reset_out_fifo(2);
                    fdc_raise_irq();
                    reset_cmd_fifo();
                    break;
                default:
                    FLOPPY_FATAL("Unknown command: %02x\n", data);
                }
            }
        } else {
            switch (fdc.command_buffer[0]) {
            // Read
            case 0x06:
            case 0x26:
            case 0x46:
            case 0x66:
            case 0x86:
            case 0xA6:
            case 0xC6:
            case 0xE6:
            // Write
            case 0x05:
            case 0x25:
            case 0x45:
            case 0x65:
            case 0x85:
            case 0xA5:
            case 0xC5:
            case 0xE5: { // TODO: Support non-DMA modes
                // Command layout:
                /*
                    [I/O] writeb: port=0x03f5 data=0xe6 - 0 - Command 
                    [I/O] writeb: port=0x03f5 data=0x00 - 1 - Drive and head
                    [I/O] writeb: port=0x03f5 data=0x00 - 2 - Cylinder
                    [I/O] writeb: port=0x03f5 data=0x00 - 3 - Head
                    [I/O] writeb: port=0x03f5 data=0x01 - 4 - Sector Number
                    [I/O] writeb: port=0x03f5 data=0x02 - 5 - Sector Size
                    [I/O] writeb: port=0x03f5 data=0x01 - 6 - Track end offset
                    [I/O] writeb: port=0x03f5 data=0x00 - 7 - Sector
                    [I/O] writeb: port=0x03f5 data=0xff - 8 - Data length (pretty much useless since this only holds values from 0 ... 255)
                */
                FLOPPY_LOG("Command: Read sector\n");
                int drvid = fdc.command_buffer[1] & 3, 
                    head = fdc.command_buffer[1] >> 2 & 1,
                    cylinder = fdc.command_buffer[2], 
                    sector = fdc.command_buffer[4], 
                    sector_size = fdc.command_buffer[5],
                    max_sector = fdc.command_buffer[6], 
                    gap3 = fdc.command_buffer[7], 
                    length = fdc.command_buffer[8];

                // Update digital output register with new data
                fdc.dor = (fdc.dor & 0xFC) | (drvid);
                fdc.selected_drive = drvid;
                // Check for coherency
                if (head != (fdc.command_buffer[3])) {
                    FLOPPY_LOG("Head count inconsistent\n");
                    fdc_abort_command();
                    goto fill_queue;
                }

                // Check for motor
                if (~fdc.dor >> 4 & (1 << drvid)) {
                    FLOPPY_LOG("Read command called when motor is off, ignoring\n");
                    fdc_abort_command();
                    goto fill_queue;
                }
                // Check if drive exists
                if (fdc.drive_info[drvid].inserted == 0) {
                    FLOPPY_LOG("Trying to read from non-existent floppy, ignoring\n");
                    fdc_abort_command();
                    goto fill_queue;
                }

                int bytes_to_read;
                if (sector_size == 0)
                    bytes_to_read = length + 1; // Floppy drive is programmed to read (length + 1) bytes
                else
                    bytes_to_read = (max_sector - (sector - 1)) << (7 + (sector_size & 7));

                UNUSED(bytes_to_read);
                // Try to seek
                printf("CHS: %d/%d/%d\n", cylinder, head, sector);
                if (fdc_seek(drvid, cylinder, head, sector) == -1) {
                    // fdc_seek does not set the registers for us, so do it ourselves
                    fdc.seek_cylinder[drvid] = cylinder;
                    fdc.seek_head[drvid] = head;
                    fdc.seek_sector[drvid] = sector;
                    fdc_abort_command();
                    goto fill_queue;
                }

                // Clear all status flags except NDMA
                fdc.msr &= MSR_NDMA;
                // Now actually begin read/write command
                if (fdc.command_buffer[0] & 1) { // write
                } else { // read
                    // TODO: Have accurate seek times.
                    FLOPPY_LOG("Reading %d sectors at %d\n", bytes_to_read >> 9, fdc.seek_internal_lba[drvid]);
                    int result = drive_read(fdc.drives[drvid], NULL, fdc.dmabuf, bytes_to_read, fdc.seek_internal_lba[drvid] << 9, fdc_finish_read);
                    if (result == DRIVE_RESULT_SYNC) {
                        fdc_finish_read(NULL, 0);
                        break;
                    } else if (result == DRIVE_RESULT_ASYNC) {
                        fdc.msr |= MSR_CB;
                        break;
                    } else {
                        // bad
                        fdc_abort_command();
                        goto fill_queue;
                    }
                }
                FLOPPY_FATAL("write\n");
                UNUSED(gap3);

            fill_queue:
                fdc.response_buffer[0] = fdc_get_st0();
                fdc.response_buffer[1] = fdc.st[1];
                fdc.response_buffer[2] = fdc.st[2];
                fdc.response_buffer[3] = fdc.seek_cylinder[drvid];
                fdc.response_buffer[4] = fdc.seek_head[drvid];
                fdc.response_buffer[5] = fdc.seek_sector[drvid];
                fdc.response_buffer[6] = sector_size;
                reset_out_fifo(7);
                fdc_raise_irq();
                reset_cmd_fifo();

                break;
            }
            case 7: // Calibrate drive
                FLOPPY_LOG("Command: Calibrate Drive\n");
                fdc_seek(data & 1, 0, 0, 1);
                fdc_raise_irq();
                break;
            }

            reset_cmd_fifo();
        }
        break;
    default:
        FLOPPY_FATAL("Unknown port write: %04x data: %02x\n", port, data);
    }
}

int floppy_next(itick_t now)
{
    UNUSED(now);
    return -1;
}

// DMA API
void* fdc_get_dma_buf(void)
{
    return fdc.dmabuf;
}
void fdc_handle_transfer_end(void)
{
    switch (fdc.command_buffer[0]) {
    case 0x06:
    case 0x26:
    case 0x46:
    case 0x66:
    case 0x86:
    case 0xA6:
    case 0xC6:
    case 0xE6:
    case 0x05:
    case 0x25:
    case 0x45:
    case 0x65:
    case 0x85:
    case 0xA5:
    case 0xC5:
    case 0xE5: {
        unsigned int sector = fdc.seek_sector[fdc.selected_drive],
            head = fdc.seek_head[fdc.selected_drive],
            cylinder = fdc.seek_cylinder[fdc.selected_drive];
        sector++;
        if (sector > fdc.drive_info[fdc.selected_drive].spt) {
            sector = 1;
            head++;
        }
        if(head >= fdc.drive_info[fdc.selected_drive].heads){
            head= 0;
            cylinder++;
        }
        if(cylinder >= fdc.drive_info[fdc.selected_drive].cylinders){
            cylinder= 0;
        }

        // Send response
        fdc_set_st0(0x20);
        reset_out_fifo(7);
        fdc.response_buffer[0] = fdc_get_st0();
        fdc.response_buffer[1] = fdc.st[1] = 0;
        fdc.response_buffer[2] = fdc.st[2] = 0;
        fdc.response_buffer[3] = fdc.seek_cylinder[fdc.selected_drive] = cylinder;
        fdc.response_buffer[4] = fdc.seek_head[fdc.selected_drive] = head;
        fdc.response_buffer[5] = fdc.seek_sector[fdc.selected_drive] = sector;
        fdc.response_buffer[6] = fdc.command_buffer[4];
                fdc_raise_irq();
                reset_cmd_fifo();

                fdc.msr = MSR_RQM | MSR_DIO; // XXX should we or this?
                //fdc.msr &= ~MSR_NDMA;
        break;
    }
    }
}

void fdc_init(struct pc_settings* pc)
{
    if (!pc->floppy_enabled)
        return;

    // Register I/O handlers
    io_register_reset(fdc_reset2);

    // Register I/O
    io_register_read(0x3F0, 6, fdc_read, NULL, NULL);
    io_register_read(0x3F7, 1, fdc_read, NULL, NULL);
    io_register_write(0x3F0, 6, fdc_write, NULL, NULL);
    io_register_write(0x3F7, 1, fdc_write, NULL, NULL);

    uint8_t fdc_types = 0, fdc_equipment = 0;
    for (int i = 0; i < 2; i++) {
        struct drive_info* drive = &pc->floppy_drives[i];
        fdc.drives[i] = drive;
        if (drive->type == DRIVE_TYPE_NONE) {
            if (i == 1) {
                fdc.status[0] |= SRA_DRV2;
                fdc.status[1] |= SRB_DRV2;
            }
            continue;
        }
        fdc_equipment |= 1 << (i + 6);

        struct fdc_drive_info* info = &fdc.drive_info[i];
        info->inserted = 1;
        info->write_protected = pc->floppy_settings[i].write_protected;

        switch (drive->sectors) {
#define MAKE_DISK_TYPE(h, t, sectors_per_track, type)                                                \
    case (h * t * sectors_per_track):                                                                \
        info->size = (h * t * sectors_per_track) * 512;                                              \
        info->heads = h;                                                                             \
        info->cylinders = t;                                                                            \
        info->spt = sectors_per_track;                                                               \
        if (type == 0)                                                                               \
            FLOPPY_LOG("Unsupported floppy disk drive size. The BIOS may not recognize the disk\n"); \
        else                                                                                         \
            fdc_types |= type << ((i ^ 1) * 4);                                                      \
        break
            MAKE_DISK_TYPE(1, 40, 8, 0); // 160K
            MAKE_DISK_TYPE(1, 40, 9, 0); // 180K
            MAKE_DISK_TYPE(2, 40, 8, 0); // 320K
            MAKE_DISK_TYPE(2, 40, 9, 1); // 360K
            MAKE_DISK_TYPE(2, 80, 8, 0); // 640K
            MAKE_DISK_TYPE(2, 80, 9, 3); // 720K
            MAKE_DISK_TYPE(2, 80, 15, 2); // 1220K
            MAKE_DISK_TYPE(2, 80, 21, 0); // 1680K
            MAKE_DISK_TYPE(2, 80, 23, 0); // 1840K
            MAKE_DISK_TYPE(2, 80, 36, 5); // 2880K
        default:
            printf("Unknown disk size: %d, defaulting to 1440K\n", drive->sectors);
            break;
            MAKE_DISK_TYPE(2, 80, 18, 4); // 1440K
        }
    }

    // http://bochs.sourceforge.net/doc/docbook/development/cmos-map.html
    cmos_set(0x10, fdc_types);
    cmos_set(0x14, cmos_get(0x14) | fdc_equipment);
}