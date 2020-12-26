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
#include <string.h>

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
    // <<< BEGIN STRUCT "struct" >>>
    /// ignore: dmabuf, drives
    uint8_t status[2]; // 3F0/3F1
    uint8_t dor; // 3F2
    uint8_t tape_drive[4]; // 3F3, unused
    uint8_t msr, data_rate; // 3F4
    uint8_t fifo; // 3F5
    // 3F6 is reserved for IDE
    uint8_t dir[4], ccr; // 3F7, one dir for each drive

    // Status registers
    uint8_t st[4];

    int selected_drive;

    // For read/write
    int multi_mode;

    // For format
    int format_bytes_to_read,
        format_byte,
        format_dma_pos;

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
    uint32_t drive_inserted[4];
    uint32_t drive_size[4];
    uint32_t drive_heads[4];
    uint32_t drive_cylinders[4];
    uint32_t drive_spt[4];
    int drive_write_protected[4];

    uint32_t write_length;

    // Can be read/written, but not used
    uint8_t precomp, config;
    uint8_t locked;
    uint8_t perpendicular_mode; 

    uint8_t dmabuf[16 << 10];
    // <<< END STRUCT "struct" >>>
} fdc;

static void fdc_state(void)
{
    // <<< BEGIN AUTOGENERATE "state" >>>
    struct bjson_object* obj = state_obj("fdc", 36);
    state_field(obj, 2, "fdc.status", &fdc.status);
    state_field(obj, 1, "fdc.dor", &fdc.dor);
    state_field(obj, 4, "fdc.tape_drive", &fdc.tape_drive);
    state_field(obj, 1, "fdc.msr", &fdc.msr);
    state_field(obj, 1, "fdc.data_rate", &fdc.data_rate);
    state_field(obj, 1, "fdc.fifo", &fdc.fifo);
    state_field(obj, 4, "fdc.dir", &fdc.dir);
    state_field(obj, 1, "fdc.ccr", &fdc.ccr);
    state_field(obj, 4, "fdc.st", &fdc.st);
    state_field(obj, 4, "fdc.selected_drive", &fdc.selected_drive);
    state_field(obj, 4, "fdc.multi_mode", &fdc.multi_mode);
    state_field(obj, 4, "fdc.format_bytes_to_read", &fdc.format_bytes_to_read);
    state_field(obj, 4, "fdc.format_byte", &fdc.format_byte);
    state_field(obj, 4, "fdc.format_dma_pos", &fdc.format_dma_pos);
    state_field(obj, 16, "fdc.command_buffer", &fdc.command_buffer);
    state_field(obj, 1, "fdc.command_buffer_size", &fdc.command_buffer_size);
    state_field(obj, 1, "fdc.command_buffer_pos", &fdc.command_buffer_pos);
    state_field(obj, 16, "fdc.response_buffer", &fdc.response_buffer);
    state_field(obj, 1, "fdc.response_buffer_size", &fdc.response_buffer_size);
    state_field(obj, 1, "fdc.response_pos", &fdc.response_pos);
    state_field(obj, 8, "fdc.seek_cylinder", &fdc.seek_cylinder);
    state_field(obj, 8, "fdc.seek_head", &fdc.seek_head);
    state_field(obj, 8, "fdc.seek_sector", &fdc.seek_sector);
    state_field(obj, 8, "fdc.seek_internal_lba", &fdc.seek_internal_lba);
    state_field(obj, 4, "fdc.interrupt_countdown", &fdc.interrupt_countdown);
    state_field(obj, 16, "fdc.drive_inserted", &fdc.drive_inserted);
    state_field(obj, 16, "fdc.drive_size", &fdc.drive_size);
    state_field(obj, 16, "fdc.drive_heads", &fdc.drive_heads);
    state_field(obj, 16, "fdc.drive_cylinders", &fdc.drive_cylinders);
    state_field(obj, 16, "fdc.drive_spt", &fdc.drive_spt);
    state_field(obj, 16, "fdc.drive_write_protected", &fdc.drive_write_protected);
    state_field(obj, 4, "fdc.write_length", &fdc.write_length);
    state_field(obj, 1, "fdc.precomp", &fdc.precomp);
    state_field(obj, 1, "fdc.config", &fdc.config);
    state_field(obj, 1, "fdc.locked", &fdc.locked);
    state_field(obj, 1, "fdc.perpendicular_mode", &fdc.perpendicular_mode);
// <<< END AUTOGENERATE "state" >>>
}

// Seek to position in drive
static int fdc_seek(int drv, uint32_t cylinder, uint32_t head, uint32_t sector)
{
    // Verify all parameters before modifying internal state
    if (fdc.drive_cylinders[drv] < cylinder || fdc.drive_heads[drv] < head || fdc.drive_spt[drv] < sector || !sector)
        return -1; // Seek failed

    fdc.seek_cylinder[drv] = cylinder;
    fdc.seek_head[drv] = head;
    fdc.seek_sector[drv] = sector;
    fdc.seek_internal_lba[drv] = (cylinder * fdc.drive_heads[drv] + head) * fdc.drive_spt[drv] + (sector - 1);
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
        if (!fdc.drive_inserted[i]) {
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

static void fdc_lower_irq(void)
{
    fdc.status[0] &= ~SRA_INTPEND;
    pic_lower_irq(6);
}
static void fdc_raise_irq(void)
{
    fdc.interrupt_countdown = 0;
    fdc.status[0] |= SRA_INTPEND;
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
    case 0x3F3:
        FLOPPY_LOG("Read from tape drive register\n");
        if (!fdc.drive_inserted[fdc.selected_drive])
            return 0x20;
        return fdc.tape_drive[fdc.selected_drive];
    case 0x3F4:
        FLOPPY_LOG("Read from MSR: c=%d t=%d\n", fdc.command_buffer_pos, fdc.command_buffer_size);
        return fdc.msr;
    case 0x3F5: { // Read from FIFO out queue
        FLOPPY_LOG("Read from output queue\n");
        // If there is nothing to read, then return dummy value
        if (!fdc.response_buffer_size)
            return 0;
        uint8_t res = fdc.response_buffer[fdc.response_pos++];
        if (fdc.response_pos == fdc.response_buffer_size)
            reset_out_fifo(0);
        return res;
    }
    case 0x3F7: {
        uint8_t temp = fdc.dir[fdc.selected_drive];
        fdc.dir[fdc.selected_drive] &= ~0x80; // XXX: Clear bit during command, not during read
        return temp | (fdc.dor & (16 << fdc.selected_drive) ? 0x80 : 0);
    }
    default:
        FLOPPY_FATAL("Unknown port read: %04x\n", port);
    }
}

static void fdc_set_st0(int bits)
{
    fdc.st[0] = bits & 0xF8;
}
static int fdc_get_st0(void)
{
    return fdc.st[0] | (CUR_HEAD() << 2) | fdc.selected_drive;
}
static void fdc_abort_command(void)
{
    // For read/write sector/track commands
    fdc_set_st0(SR0_ABNTERM);
    fdc.st[1] = 4;
    fdc.st[2] = 0;
}
static void fdc_abort_command2(void)
{
    // For format commands
    fdc_set_st0(SR0_ABNTERM);
    fdc.st[1] = 0x27;
    fdc.st[2] = 0x31;
}
static void fdc_read_cb(void* a, int b)
{
    UNUSED(a);
    if (b != 0)
        FLOPPY_FATAL("TODO: Drive read errors\n");
    dma_raise_dreq(2);

    // The story continues in fdc_dma_complete
}
static void fdc_write_cb(void* a, int b)
{
    UNUSED(a);
    if (b == 0)
        fdc_set_st0(0x20);
    else
        fdc_set_st0(SR0_ABNTERM);
    uint32_t drvid = fdc.selected_drive,
             sector = fdc.seek_sector[drvid],
             head = fdc.seek_head[drvid],
             cylinder = fdc.seek_cylinder[drvid];
    sector++;
    if (sector > fdc.drive_spt[drvid]) {
        sector = 1;
        head++;
    }
    if (head >= fdc.drive_heads[drvid]) {
        head = 0;
        cylinder++;
    }
    if (cylinder >= fdc.drive_cylinders[drvid]) {
        cylinder = 0;
    }
    fdc.msr = MSR_RQM;
    reset_out_fifo(7);
    reset_cmd_fifo();
    fdc.response_buffer[0] = fdc_get_st0();
    fdc.response_buffer[1] = 0;
    fdc.response_buffer[2] = 0;
    fdc.response_buffer[3] = fdc.seek_cylinder[drvid] = cylinder;
    fdc.response_buffer[4] = fdc.seek_head[drvid] = head;
    fdc.response_buffer[5] = fdc.seek_sector[drvid] = sector;
    fdc.response_buffer[6] = fdc.command_buffer[4];
    fdc_raise_irq();
}

static void fdc_idle(void)
{
    fdc.msr = MSR_RQM;
    reset_cmd_fifo();
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
                case 0x0D:
                case 0x4D: // Format track
                    fdc.command_buffer_size = 6;
                    break;
                case 0x0E: // Dump registers
                    // TODO: Include more registers
                    FLOPPY_LOG("Command: Dump registers\n");
                    reset_out_fifo(10);
                    fdc.response_buffer[0] = fdc.seek_cylinder[0];
                    fdc.response_buffer[1] = fdc.seek_cylinder[1];
                    fdc.response_buffer[2] = 0;
                    fdc.response_buffer[3] = 0;
                    fdc.response_buffer[4] = 0;
                    fdc.response_buffer[5] = fdc.msr & MSR_NDMA ? 1 : 0;
                    fdc.response_buffer[6] = 0;
                    fdc.response_buffer[7] = fdc.locked << 7 | fdc.perpendicular_mode;
                    fdc.response_buffer[8] = fdc.config;
                    fdc.response_buffer[9] = fdc.precomp;
                    reset_cmd_fifo();
                    break;
                case 0x10: // Get version
                    FLOPPY_LOG("Command: Get version (0x90)\n");
                    reset_out_fifo(1);
                    fdc.response_buffer[0] = 0x90;

                    // Clean command FIFO
                    reset_cmd_fifo();
                    break;
                case 0x12: // Perpendicular mode
                    fdc.command_buffer_size = 2;
                    break;
                case 0x13: // Configure
                    fdc.command_buffer_size = 4;
                    break;
                case 0x14: // unlock
                case 0x94: // lock
                    FLOPPY_LOG("Command: %sock\n", data & 0x80 ? "Unl" : "L");
                    reset_out_fifo(1);
                    fdc.response_buffer[0] = (fdc.locked = fdc.command_buffer[0] >> 7) << 4;
                    reset_cmd_fifo();
                    break;
                case 0x18: // OS/2 uses this command, I can't find it anywhere
                    fdc_set_st0(0x80);
                    reset_out_fifo(1);
                    fdc.response_buffer[0] = fdc_get_st0();
                    reset_cmd_fifo();
                    break;
                // Read
                case 0x02: // Read complete track
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
                case 0xE5:
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
                        fdc.response_buffer[0] = 0xC0 | (fdc.seek_head[id] << 2) | id;
                    } else
                        fdc.response_buffer[0] = SR0_SEEK | (CUR_HEAD() << 2) | (fdc.selected_drive);

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
            case 0x0D:
            case 0x4D: { // Format track
                int drvid = drvid = fdc.command_buffer[1] & 3,
                    head = fdc.command_buffer[1] >> 2 & 1,
                    sector_size = 128 << fdc.command_buffer[2],
                    sectors = fdc.command_buffer[3],
                    // Gap3 is unused
                    fill = fdc.command_buffer[5];

                // Set right output register
                fdc.dor &= 0xFC;
                fdc.dor |= drvid;
                fdc.selected_drive = drvid;

                if ((uint32_t)sectors != fdc.drive_spt[drvid]) {
                    FLOPPY_LOG("Sector size != Sectors per track\n");
                    fdc_abort_command2();
                    goto abort_command2;
                }
                if (fdc.drive_inserted[drvid] == 0) {
                    FLOPPY_LOG("Drive not inserted\n");
                    fdc_abort_command2();
                    goto abort_command2;
                }
                if (fdc.drive_write_protected[drvid]) {
                    FLOPPY_LOG("Trying to format write-protected drive\n");
                    fdc_abort_command2();
                    goto abort_command2;
                }
                if (sector_size != 512) {
                    FLOPPY_LOG("Sector size != 512\n");
                    fdc_abort_command2();
                    goto abort_command2;
                }

                fdc.format_bytes_to_read = sectors << 2;
                fdc.format_byte = fill;
                // Request information from DMA
                dma_raise_dreq(2);
                return; // The story continues in fdc_dma_complete
            abort_command2:
                reset_out_fifo(7);
                reset_cmd_fifo();
                fdc.response_buffer[0] = fdc_get_st0();
                fdc.response_buffer[1] = fdc.st[1];
                fdc.response_buffer[2] = fdc.st[2];
                fdc.response_buffer[3] = fdc.seek_cylinder[drvid];
                fdc.response_buffer[4] = head;
                fdc.response_buffer[5] = fdc.seek_sector[drvid];
                fdc.response_buffer[6] = fdc.command_buffer[4];
                fdc_raise_irq();
                break;
            }
            case 0x12: // Perpendicular Mode (no idea what this does)
                FLOPPY_LOG("Command: Set perpendicular mode\n");
                fdc.perpendicular_mode = fdc.command_buffer[1] & 0x7F;
                fdc_idle();
                break;
            case 0x13: // Configure
                FLOPPY_LOG("Command: Configure\n");
                // fdc.command_buffer[1] == 0
                fdc.precomp = fdc.command_buffer[3];
                fdc.config = fdc.command_buffer[2];
                fdc_idle();
                break;
            // Read/Write
            case 0x02:
            case 0x22:
            case 0x42:
            case 0x62:
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
                /*
                Anatomy of command:
                [I/O] writeb: port=0x03f5 data=0xe6 - 0: Command
                [I/O] writeb: port=0x03f5 data=0x00 - 1: Head/Drive ID
                [I/O] writeb: port=0x03f5 data=0x00 - 2: Cylinder
                [I/O] writeb: port=0x03f5 data=0x00 - 3: Head (should be the same as in head/drive ID)
                [I/O] writeb: port=0x03f5 data=0x01 - 4: Sector
                [I/O] writeb: port=0x03f5 data=0x02 - 5: Sector size
                [I/O] writeb: port=0x03f5 data=0x01 - 6: Sector end
                [I/O] writeb: port=0x03f5 data=0x00 - 7: Gap size (unused)
                [I/O] writeb: port=0x03f5 data=0xff - 8: Number of bytes to read
                */
                int drvid = fdc.command_buffer[1] & 3,
                    head = fdc.command_buffer[1] >> 2 & 1,
                    cylinder = fdc.command_buffer[2],
                    sector = fdc.command_buffer[4],
                    sector_size = fdc.command_buffer[5],
                    max_sector = fdc.command_buffer[6],
                    gap3 = fdc.command_buffer[7],
                    length = fdc.command_buffer[8];
                UNUSED(gap3);

                // For read-track commands, the sector specification is ignored and the entire track is used
                if (fdc.command_buffer[0] == 2) {
                    sector = 1;
                    max_sector = fdc.drive_spt[drvid];
                }

                fdc.dor &= 0xFC;
                fdc.dor |= drvid;
                fdc.selected_drive = drvid;

                // Check for head coherency
                if (head != fdc.command_buffer[3]) {
                    fdc_abort_command();
                    goto abort_command;
                }

                // Check how many bytes to read
                int bytes_to_read;
                if (sector_size != 0)
                    bytes_to_read = (max_sector - (sector - 1)) << (7 + (sector_size & 7));
                else
                    bytes_to_read = length;

                // Check if media is present
                if (!fdc.drive_inserted[drvid]) {
                    fdc_abort_command();
                    goto abort_command;
                }

                // Try seeking
                if (fdc_seek(drvid, cylinder, head, sector) == -1) {
                    fdc_abort_command();
                    goto abort_command;
                }

                if (fdc.command_buffer[0] & 1) {
                    // Write
                    if (fdc.drive_write_protected[drvid]) {
                        fdc_abort_command();
                        goto abort_command;
                    }
                    fdc.write_length = bytes_to_read;
                    dma_raise_dreq(2);
                    // The story continues in fdc_dma_complete
                    break;
                } else {
                    FLOPPY_LOG("Command: Read sector at %d\n", fdc.seek_internal_lba[drvid]);
                    // Read
                    int res = drive_read(fdc.drives[drvid], NULL, fdc.dmabuf, bytes_to_read, fdc.seek_internal_lba[drvid] << 9, fdc_read_cb);
                    if (res == DRIVE_RESULT_ASYNC) {
                        // Set BSY bit if needed
                        fdc.msr |= MSR_CB | (fdc.msr & MSR_NDMA ? MSR_DIO | MSR_RQM : 0);
                        break;
                    } else if (res == DRIVE_RESULT_SYNC) {
                        fdc.msr |= MSR_CB;
                        fdc_read_cb(NULL, 0);
                        break;
                    } else {
                        fdc_abort_command();
                        goto abort_command;
                    }
                }

            abort_command:
                reset_out_fifo(7);
                reset_cmd_fifo();
                fdc.response_buffer[0] = fdc_get_st0();
                fdc.response_buffer[1] = fdc.st[1];
                fdc.response_buffer[2] = fdc.st[2];
                fdc.response_buffer[3] = cylinder;
                fdc.response_buffer[4] = head;
                fdc.response_buffer[5] = sector;
                fdc.response_buffer[6] = fdc.command_buffer[4];
                fdc_raise_irq();
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
    case 0x3f7:
        if(data != 0) FLOPPY_FATAL("unknown value written to configuration control register: %02x\n", data);
        break;
    default:
        FLOPPY_FATAL("Unknown port write: %04x data: %02x\n", port, data);
    }
}

static void fdc_handle_format(void* a, int b)
{
    uint8_t sector[512];
    UNUSED(a);
    if(b != 0) FLOPPY_FATAL("Unable to format sector\n");
    for (;;) {
        if (fdc.format_dma_pos >= fdc.format_bytes_to_read) {
            fdc_set_st0(0x20);
                reset_out_fifo(7);
                reset_cmd_fifo();
                fdc.response_buffer[0] = fdc_get_st0();
                fdc.response_buffer[1] = fdc.st[1];
                fdc.response_buffer[2] = fdc.st[2];
                fdc.response_buffer[3] = fdc.seek_cylinder[fdc.selected_drive];
                fdc.response_buffer[4] = fdc.seek_head[fdc.selected_drive];
                fdc.response_buffer[5] = fdc.seek_cylinder[fdc.selected_drive];
                fdc.response_buffer[6] = fdc.command_buffer[4];
                fdc_raise_irq();
                break;
            return;
        }
        int track = 0, head = 0, sector_number = 0;
        track = fdc.dmabuf[fdc.format_dma_pos + 0];
        head = fdc.dmabuf[fdc.format_dma_pos + 1];
        sector_number = fdc.dmabuf[fdc.format_dma_pos + 2];
        if (fdc.dmabuf[fdc.format_dma_pos + 3] != 2)
            FLOPPY_FATAL("Unknown floppy size in format\n");
        fdc.format_dma_pos += 4;
        
        // Fill sector
        memset(sector, fdc.format_byte, 512);

        // Seek to right location
        if(fdc_seek(fdc.selected_drive, track, head, sector_number) == -1)
            FLOPPY_FATAL("Invalid seek CHS: %d/%d/%d\n", track, head, sector_number);
        
        int res = drive_write(fdc.drives[fdc.selected_drive], NULL, sector, 512, fdc.seek_internal_lba[fdc.selected_drive] << 9, fdc_handle_format);
        if(res == DRIVE_RESULT_ASYNC){
            fdc.msr = MSR_CB;
            return;
        }else if(res == DRIVE_RESULT_SYNC){
            continue;
        }else{
            FLOPPY_FATAL("Unable to format sector\n");
        }
    }
}

int floppy_next(itick_t now)
{
    UNUSED(now);
    return -1;
}

void* fdc_dma_buf(void)
{
    return fdc.dmabuf;
}
void fdc_dma_complete(void)
{
    switch (fdc.command_buffer[0]) {
    case 0x0D:
    case 0x4D: { // Format track
    fdc_handle_format(NULL, 0);
    break;
    }
    case 0x05:
    case 0x25:
    case 0x45:
    case 0x65:
    case 0x85:
    case 0xA5:
    case 0xC5:
    case 0xE5: {
        int res = drive_write(fdc.drives[fdc.selected_drive], NULL, fdc.dmabuf, fdc.write_length, fdc.seek_internal_lba[fdc.selected_drive] << 9, fdc_write_cb);
        if (res == DRIVE_RESULT_ASYNC) {
            // Set BSY bit if needed
            fdc.msr |= MSR_CB | (fdc.msr & MSR_NDMA ? MSR_DIO | MSR_RQM : 0);
            return;
        } else if (res == DRIVE_RESULT_SYNC) {
            fdc_write_cb(NULL, 0);
            return;
        } else {
            fdc_abort_command();
            fdc_write_cb(NULL, -1);
        }
        break;
    }
    case 0x02: // Read whole track
    case 0x22:
    case 0x42:
    case 0x62:
    case 0x06:
    case 0x26:
    case 0x46:
    case 0x66:
    case 0x86:
    case 0xA6:
    case 0xC6:
    case 0xE6: {
        uint32_t drvid = fdc.selected_drive,
                 sector = fdc.seek_sector[drvid],
                 head = fdc.seek_head[drvid],
                 cylinder = fdc.seek_cylinder[drvid];
        sector++;
        if (sector > fdc.drive_spt[drvid]) {
            sector = 1;
            head++;
        }
        if (head >= fdc.drive_heads[drvid]) {
            head = 0;
            cylinder++;
        }
        if (cylinder >= fdc.drive_cylinders[drvid]) {
            cylinder = 0;
        }
        fdc.msr = MSR_RQM;
        reset_out_fifo(7);
        reset_cmd_fifo();
        fdc_set_st0(0x20);
        fdc.response_buffer[0] = fdc_get_st0();
        fdc.response_buffer[1] = 0;
        fdc.response_buffer[2] = 0;
        fdc.response_buffer[3] = fdc.seek_cylinder[drvid] = cylinder;
        fdc.response_buffer[4] = fdc.seek_head[drvid] = head;
        fdc.response_buffer[5] = fdc.seek_sector[drvid] = sector;
        fdc.response_buffer[6] = fdc.command_buffer[4];
        fdc_raise_irq();
        break;
    }
    }
}

void fdc_replace_drive(int idx, struct drive_info* drive)
{
    fdc.drives[idx] = drive;
    fdc.dir[idx] |= 0x80;
}

void fdc_init(struct pc_settings* pc)
{
    if (!pc->floppy_enabled)
        return;

    // Register I/O handlers
    io_register_reset(fdc_reset2);
    state_register(fdc_state);

    // Register I/O
    io_register_read(0x3F0, 6, fdc_read, NULL, NULL);
    io_register_read(0x3F7, 1, fdc_read, NULL, NULL);
    io_register_write(0x3F0, 6, fdc_write, NULL, NULL);
    io_register_write(0x3F7, 1, fdc_write, NULL, NULL);

    uint8_t fdc_types = 0, fdc_equipment = 0;
    for (int i = 0; i < 2; i++) {
        struct drive_info* drive = &pc->floppy_drives[i];
        if (drive->type == DRIVE_TYPE_NONE) {
            if (i == 1) {
                fdc.status[0] |= SRA_DRV2;
                fdc.status[1] |= SRB_DRV2;
            }
            continue;
        }
        fdc_equipment |= 1 << (i + 6);

        fdc.drives[i] = drive;
        fdc.drive_inserted[i] = 1;
        fdc.drive_write_protected[i] = pc->floppy_settings[i].write_protected;

        switch (drive->sectors) {
#define MAKE_DISK_TYPE(h, t, sectors_per_track, type, tdr)                                           \
    case (h * t * sectors_per_track):                                                                \
        fdc.drive_size[i] = (h * t * sectors_per_track) * 512;                                       \
        fdc.drive_heads[i] = h;                                                                      \
        fdc.drive_cylinders[i] = t;                                                                  \
        fdc.drive_spt[i] = sectors_per_track;                                                        \
        fdc.tape_drive[i] = tdr;                                                                     \
        if (type == 0)                                                                               \
            FLOPPY_LOG("Unsupported floppy disk drive size. The BIOS may not recognize the disk\n"); \
        else                                                                                         \
            fdc_types |= type << ((i ^ 1) * 4);                                                      \
        break
            MAKE_DISK_TYPE(1, 40, 8, 0, 0); // 160K
            MAKE_DISK_TYPE(1, 40, 9, 0, 0); // 180K
            MAKE_DISK_TYPE(2, 40, 8, 0, 0); // 320K
            MAKE_DISK_TYPE(2, 40, 9, 1, 0); // 360K
            MAKE_DISK_TYPE(2, 80, 8, 0, 0); // 640K
            MAKE_DISK_TYPE(2, 80, 9, 3, 0xC0); // 720K
            MAKE_DISK_TYPE(2, 80, 15, 2, 0); // 1220K
            MAKE_DISK_TYPE(2, 80, 21, 0, 0); // 1680K
            MAKE_DISK_TYPE(2, 80, 23, 0, 0); // 1840K
            MAKE_DISK_TYPE(2, 80, 36, 5, 0x40); // 2880K
        default:
            printf("Unknown disk size: %d, defaulting to 1440K\n", drive->sectors);
            break;
            MAKE_DISK_TYPE(2, 80, 18, 4, 0x80); // 1440K
        }
    }

    // http://bochs.sourceforge.net/doc/docbook/development/cmos-map.html
    cmos_set(0x10, fdc_types);
    cmos_set(0x14, cmos_get(0x14) | fdc_equipment);
}