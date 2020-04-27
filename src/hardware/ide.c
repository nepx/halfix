#include "cpuapi.h" // Needed for IDE DMA
#include "devices.h"
#include "drive.h"
#include "platform.h"
#include <string.h>

// A mostly-complete ATA implementation.
// This version works around a bug in the Bochs BIOS. It does not check if BSY is set after a write command has been executed.

#define IDE_LOG(x, ...) LOG("IDE", x, ##__VA_ARGS__)
#define IDE_FATAL(x, ...) \
    FATAL("IDE", x, ##__VA_ARGS__);

// Status bits: Seagate Manual Page 21
#define ATA_STATUS_BSY 0x80 // Busy
#define ATA_STATUS_DRDY 0x40 // Drive ready
#define ATA_STATUS_DF 0x20 // Drive write fault
#define ATA_STATUS_DSC 0x10 // Drive seek complete
#define ATA_STATUS_DRQ 0x08 // Data request ready
#define ATA_STATUS_CORR 0x04 // Corrected data
#define ATA_STATUS_IDX 0x02 // Index
#define ATA_STATUS_ERR 0x01

#define ATA_ERROR_BBK 0x80 // Bad sector
#define ATA_ERROR_UNC 0x40 // Uncorrectable data
#define ATA_ERROR_MC 0x20 // No media
#define ATA_ERROR_IDNF 0x10 // ID mark not found
#define ATA_ERROR_MCR 0x08 // No media
#define ATA_ERROR_ABRT 0x04 // Command aborted
#define ATA_ERROR_TK0NF 0x02 // Track 0 not found
#define ATA_ERROR_AMNF 0x01 // No address mark

// https://www.bswd.com/sff8020i.pdf
#define ATAPI_INTERRUPT_REASON_REL 0x04 // Always 0
#define ATAPI_INTERRUPT_REASON_IO 0x02 // 1 if transferring data out
#define ATAPI_INTERRUPT_REASON_CoD 0x01 // 1 if command bytes, 0 if data bytes

#define ATAPI_SENSE_NONE 0x00
#define ATAPI_SENSE_NOT_READY 0x02
#define ATAPI_SENSE_MEDIUM_ERROR 0x03
#define ATAPI_SENSE_HARDWARE_ERROR 0x04
#define ATAPI_SENSE_ILLEGAL_REQUEST 0x05
#define ATAPI_SENSE_UNIT_ATTENTION 0x06
#define ATAPI_SENSE_ABORTED 0x0B

// Upper 8 bits represent ASC, lower 8 bits rperesent ASCQ
#define ATAPI_ASC_CAUSE_NOT_REPORTABLE 0x0400
#define ATAPI_ASC_GETTING_READY 0x0401
#define ATAPI_ASC_MANUAL_INTERVENTION 0x0403
#define ATAPI_ASC_BSY 0x0407 // Operation in progress
#define ATAPI_ASC_OFFLINE 0x0412
#define ATAPI_ASC_MAINTENANCE 0x0481
#define ATAPI_ASC_OUT_OF_RANGE 0x2000 // Also illegal opcode
#define ATAPI_ASC_CLEANING_CARTRIDGE 0x3003
#define ATAPI_ASC_NOT_PRESENT 0x3A02 // Also set if mailslot is open
#define ATAPI_ASC_INVALID_FIELD 0x2400
#define ATAPI_ASC_INVALID_OFFSET 0x2100

#define ATAPI_ERROR_ABRT 0x04 // Command aborted
#define ATAPI_ERROR_EOM 0x02 // Command specific
#define ATAPI_ERROR_ILI 0x01 // Command specific

#define DISABLE_MULTIPLE_SECTORS
#ifdef DISABLE_MULTIPLE_SECTORS
#define MAX_MULTIPLE_SECTORS 1
#else
#define MAX_MULTIPLE_SECTORS 16 // According to QEMU and Bochs
#endif

// We keep all fields inside one big struct to make it easy for the autogen savestate
static struct ide_controller {
    // <<< BEGIN STRUCT "struct" >>>

    /// ignore: canary_below, canary_above

    // The ID of the currently selected drive for master (0) or slave (1)
    int selected;

    // Is LBA enabled for this controller?
    int lba;

    // The number of chunks to be transferred over every single ide_read/write_sector is called.
    // Note that it should be less than or equal to MAX_MULTIPLE_SECTORS
    int sectors_read;

    // Whether the current command requires lba48
    int lba48;

    // Set during the set multiple mode command.
    int multiple_sectors_count;

    // Various IDE registers. Note "sector number" refers to the position of sector while "sector count" refers to the number of sectors
    uint8_t error, feature, drive_and_head;
    // This is important for LBA48
    uint16_t sector_number, cylinder_low, cylinder_high, sector_count;

    uint8_t device_control, status;

    // The last command issued to the controller. Used to handle callbacks when the PIO buffer needs to be updated.
    uint8_t command_issued;

    // PIO buffer
    uint32_t pio_position, pio_length;
    uint32_t canary_below;
    union {
        uint8_t pio_buffer[16 * 512]; // MAX_MULTIPLE_SECTORS * 512

        // These are for aligned accesses
        uint16_t pio_buffer16[16 * 512 / 2];
        uint32_t pio_buffer32[16 * 512 / 4];
    };
    uint32_t canary_above;

    int irq_status;

    //  === The following registers are per-drive ===

    // Type of each drive attached to controller (DRIVE_TYPE_*)
    int type[2];
    // Drives can be attached to a controller, but they may or may not have media inserted.
    int media_inserted[2];

    // Disk geometry information

    // If an OS wants a different translation mode (specified by the "Initialize Drive Parameters" command), then the value corresponding to the drive will be set to 1.
    int translated[2];

    // Sectors per track -- master non-translated, master translated, slave non-translated, slave translated
    uint32_t sectors_per_track[4];
    // Same as above, but total number of heads
    uint32_t heads[4];
    // Same as above, but total number of heads
    uint32_t cylinders[4];

    // The total number of sectors accessible by CHS addressing
    uint32_t total_sectors_chs[2];
    // Total number of sectors on disk
    uint32_t total_sectors[2];

    // === The following registers are for PCI IDE ===

    uint8_t dma_command,
        dma_status;
    uint32_t prdt_address;

    int dma_enabled;

    // Multiword DMA, Ultimate DMA support (only used for IDENTIFY commands)
    uint16_t mdma, udma;

    // === The following registers are for ATAPI ===
    uint8_t sense_key;
    uint16_t asc;

    uint32_t atapi_lba;
    uint32_t atapi_sectors_to_read, atapi_sector_size;
    uint32_t
        // Total number of bytes to send per ATAPI read
        atapi_bytes_to_transfer,
        // Total number of bytes to send before we raise an IRQ
        atapi_cylinder_count,
        // Total number of bytes we've sent in this frame
        atapi_frame_bytes_to_transfer,
        atapi_frame_bytes_transferred,
        atapi_total_bytes_transferred;
    uint8_t atapi_command;

    uint8_t atapi_can_eject_cdrom;

    uint8_t atapi_dma_enabled;

    // <<< END STRUCT "struct" >>>

    struct drive_info* info[2];
} ide[2];

static void ide_state(void)
{

    // <<< BEGIN AUTOGENERATE "state" >>>
    struct bjson_object* obj = state_obj("ide[NUMBER]", (46) * 2);
    state_field(obj, 4, "ide[0].selected", &ide[0].selected);
    state_field(obj, 4, "ide[1].selected", &ide[1].selected);
    state_field(obj, 4, "ide[0].lba", &ide[0].lba);
    state_field(obj, 4, "ide[1].lba", &ide[1].lba);
    state_field(obj, 4, "ide[0].sectors_read", &ide[0].sectors_read);
    state_field(obj, 4, "ide[1].sectors_read", &ide[1].sectors_read);
    state_field(obj, 4, "ide[0].lba48", &ide[0].lba48);
    state_field(obj, 4, "ide[1].lba48", &ide[1].lba48);
    state_field(obj, 4, "ide[0].multiple_sectors_count", &ide[0].multiple_sectors_count);
    state_field(obj, 4, "ide[1].multiple_sectors_count", &ide[1].multiple_sectors_count);
    state_field(obj, 1, "ide[0].error", &ide[0].error);
    state_field(obj, 1, "ide[1].error", &ide[1].error);
    state_field(obj, 1, "ide[0].feature", &ide[0].feature);
    state_field(obj, 1, "ide[1].feature", &ide[1].feature);
    state_field(obj, 1, "ide[0].drive_and_head", &ide[0].drive_and_head);
    state_field(obj, 1, "ide[1].drive_and_head", &ide[1].drive_and_head);
    state_field(obj, 2, "ide[0].sector_number", &ide[0].sector_number);
    state_field(obj, 2, "ide[1].sector_number", &ide[1].sector_number);
    state_field(obj, 2, "ide[0].cylinder_low", &ide[0].cylinder_low);
    state_field(obj, 2, "ide[1].cylinder_low", &ide[1].cylinder_low);
    state_field(obj, 2, "ide[0].cylinder_high", &ide[0].cylinder_high);
    state_field(obj, 2, "ide[1].cylinder_high", &ide[1].cylinder_high);
    state_field(obj, 2, "ide[0].sector_count", &ide[0].sector_count);
    state_field(obj, 2, "ide[1].sector_count", &ide[1].sector_count);
    state_field(obj, 1, "ide[0].device_control", &ide[0].device_control);
    state_field(obj, 1, "ide[1].device_control", &ide[1].device_control);
    state_field(obj, 1, "ide[0].status", &ide[0].status);
    state_field(obj, 1, "ide[1].status", &ide[1].status);
    state_field(obj, 1, "ide[0].command_issued", &ide[0].command_issued);
    state_field(obj, 1, "ide[1].command_issued", &ide[1].command_issued);
    state_field(obj, 4, "ide[0].pio_position", &ide[0].pio_position);
    state_field(obj, 4, "ide[1].pio_position", &ide[1].pio_position);
    state_field(obj, 4, "ide[0].pio_length", &ide[0].pio_length);
    state_field(obj, 4, "ide[1].pio_length", &ide[1].pio_length);
    state_field(obj, 8192, "ide[0].pio_buffer", &ide[0].pio_buffer);
    state_field(obj, 8192, "ide[1].pio_buffer", &ide[1].pio_buffer);
    state_field(obj, 4, "ide[0].irq_status", &ide[0].irq_status);
    state_field(obj, 4, "ide[1].irq_status", &ide[1].irq_status);
    state_field(obj, 8, "ide[0].type", &ide[0].type);
    state_field(obj, 8, "ide[1].type", &ide[1].type);
    state_field(obj, 8, "ide[0].media_inserted", &ide[0].media_inserted);
    state_field(obj, 8, "ide[1].media_inserted", &ide[1].media_inserted);
    state_field(obj, 8, "ide[0].translated", &ide[0].translated);
    state_field(obj, 8, "ide[1].translated", &ide[1].translated);
    state_field(obj, 16, "ide[0].sectors_per_track", &ide[0].sectors_per_track);
    state_field(obj, 16, "ide[1].sectors_per_track", &ide[1].sectors_per_track);
    state_field(obj, 16, "ide[0].heads", &ide[0].heads);
    state_field(obj, 16, "ide[1].heads", &ide[1].heads);
    state_field(obj, 16, "ide[0].cylinders", &ide[0].cylinders);
    state_field(obj, 16, "ide[1].cylinders", &ide[1].cylinders);
    state_field(obj, 8, "ide[0].total_sectors_chs", &ide[0].total_sectors_chs);
    state_field(obj, 8, "ide[1].total_sectors_chs", &ide[1].total_sectors_chs);
    state_field(obj, 8, "ide[0].total_sectors", &ide[0].total_sectors);
    state_field(obj, 8, "ide[1].total_sectors", &ide[1].total_sectors);
    state_field(obj, 1, "ide[0].dma_command", &ide[0].dma_command);
    state_field(obj, 1, "ide[1].dma_command", &ide[1].dma_command);
    state_field(obj, 1, "ide[0].dma_status", &ide[0].dma_status);
    state_field(obj, 1, "ide[1].dma_status", &ide[1].dma_status);
    state_field(obj, 4, "ide[0].prdt_address", &ide[0].prdt_address);
    state_field(obj, 4, "ide[1].prdt_address", &ide[1].prdt_address);
    state_field(obj, 4, "ide[0].dma_enabled", &ide[0].dma_enabled);
    state_field(obj, 4, "ide[1].dma_enabled", &ide[1].dma_enabled);
    state_field(obj, 2, "ide[0].mdma", &ide[0].mdma);
    state_field(obj, 2, "ide[1].mdma", &ide[1].mdma);
    state_field(obj, 2, "ide[0].udma", &ide[0].udma);
    state_field(obj, 2, "ide[1].udma", &ide[1].udma);
    state_field(obj, 1, "ide[0].sense_key", &ide[0].sense_key);
    state_field(obj, 1, "ide[1].sense_key", &ide[1].sense_key);
    state_field(obj, 2, "ide[0].asc", &ide[0].asc);
    state_field(obj, 2, "ide[1].asc", &ide[1].asc);
    state_field(obj, 4, "ide[0].atapi_lba", &ide[0].atapi_lba);
    state_field(obj, 4, "ide[1].atapi_lba", &ide[1].atapi_lba);
    state_field(obj, 4, "ide[0].atapi_sectors_to_read", &ide[0].atapi_sectors_to_read);
    state_field(obj, 4, "ide[1].atapi_sectors_to_read", &ide[1].atapi_sectors_to_read);
    state_field(obj, 4, "ide[0].atapi_sector_size", &ide[0].atapi_sector_size);
    state_field(obj, 4, "ide[1].atapi_sector_size", &ide[1].atapi_sector_size);
    state_field(obj, 4, "ide[0].atapi_bytes_to_transfer", &ide[0].atapi_bytes_to_transfer);
    state_field(obj, 4, "ide[1].atapi_bytes_to_transfer", &ide[1].atapi_bytes_to_transfer);
    state_field(obj, 4, "ide[0].atapi_cylinder_count", &ide[0].atapi_cylinder_count);
    state_field(obj, 4, "ide[1].atapi_cylinder_count", &ide[1].atapi_cylinder_count);
    state_field(obj, 4, "ide[0].atapi_frame_bytes_to_transfer", &ide[0].atapi_frame_bytes_to_transfer);
    state_field(obj, 4, "ide[1].atapi_frame_bytes_to_transfer", &ide[1].atapi_frame_bytes_to_transfer);
    state_field(obj, 4, "ide[0].atapi_frame_bytes_transferred", &ide[0].atapi_frame_bytes_transferred);
    state_field(obj, 4, "ide[1].atapi_frame_bytes_transferred", &ide[1].atapi_frame_bytes_transferred);
    state_field(obj, 4, "ide[0].atapi_total_bytes_transferred", &ide[0].atapi_total_bytes_transferred);
    state_field(obj, 4, "ide[1].atapi_total_bytes_transferred", &ide[1].atapi_total_bytes_transferred);
    state_field(obj, 1, "ide[0].atapi_command", &ide[0].atapi_command);
    state_field(obj, 1, "ide[1].atapi_command", &ide[1].atapi_command);
    state_field(obj, 1, "ide[0].atapi_can_eject_cdrom", &ide[0].atapi_can_eject_cdrom);
    state_field(obj, 1, "ide[1].atapi_can_eject_cdrom", &ide[1].atapi_can_eject_cdrom);
    state_field(obj, 1, "ide[0].atapi_dma_enabled", &ide[0].atapi_dma_enabled);
    state_field(obj, 1, "ide[1].atapi_dma_enabled", &ide[1].atapi_dma_enabled);
    // <<< END AUTOGENERATE "state" >>>

    char filename[1000];
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            struct drive_info* info = ide[i].info[j];
            if (ide[i].media_inserted[j]) {
                sprintf(filename, "ide%d-%d", i, j);
                drive_state(info, filename);
            }
        }
    }
}

#define SELECTED(obj, field) obj->field[obj->selected]
#define TRANSLATED(obj, field) obj->field[(obj->selected << 1) | (SELECTED(obj, translated))]

// Returns 0 for master, 1 for slave
static inline int get_ctrl_id(struct ide_controller* ctrl)
{
    return ctrl == &ide[1];
}
static inline int selected_drive_has_media(struct ide_controller* ctrl)
{
    return SELECTED(ctrl, type) != DRIVE_TYPE_NONE;
}
static inline int controller_has_media(struct ide_controller* ctrl)
{
    return ctrl->media_inserted[0] | ctrl->media_inserted[1];
}

static void ide_update_irq(struct ide_controller* ctrl)
{
    if (ctrl->irq_status && !(ctrl->device_control & 2))
        pic_raise_irq(get_ctrl_id(ctrl) | 14);
    else
        pic_lower_irq(get_ctrl_id(ctrl) | 14);
}
static inline void ide_lower_irq(struct ide_controller* ctrl)
{
    ctrl->irq_status = 0;
    ide_update_irq(ctrl);
}
static inline void ide_raise_irq(struct ide_controller* ctrl)
{
    ctrl->dma_status |= 0x04;
    ctrl->irq_status = 1;
    ide_update_irq(ctrl);
}

// Indicates that the command has been aborted for one reason or another.
static void ide_abort_command(struct ide_controller* ctrl)
{
    ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DSC | ATA_STATUS_ERR;
    ctrl->error = ATA_ERROR_ABRT;
    ctrl->pio_position = 0;
    ctrl->dma_status |= 2; // Failed
    ide_raise_irq(ctrl);
}

// Reset the IDE. Simply resets the selected drives and removes translations
static void ide_reset(void)
{
    // Make both drives select thir master drive
    ide[0].selected = 0;
    ide[1].selected = 0;

    // Remove all translation modes
    ide[0].translated[0] = 0;
    ide[0].translated[1] = 0;
    ide[1].translated[0] = 0;
    ide[1].translated[1] = 0;
}

// Get the number of sectors specified by the sector_count register. Special case for zero
static uint32_t ide_get_sector_count(struct ide_controller* ctrl, int lba48)
{
    if (lba48)
        return (!ctrl->sector_count) << 16 | ctrl->sector_count;
    else {
        uint8_t real_sector_count = ctrl->sector_count;
        return (!real_sector_count) << 8 | real_sector_count;
    }
}

// Get the sector offset (i.e. place to seek in file divided by 512)
static uint64_t ide_get_sector_offset(struct ide_controller* ctrl, int lba48)
{
    uint64_t res;
    switch ((lba48 << 1 | ctrl->lba) & 3) {
    case 0: { // CHS
        uint64_t cyl = (ctrl->cylinder_low & 0xFF) | ((ctrl->cylinder_high << 8) & 0xFFFF);
        res = cyl * TRANSLATED(ctrl, heads) * TRANSLATED(ctrl, sectors_per_track);
        uint64_t heads = ctrl->drive_and_head & 0x0F;
        res += heads * TRANSLATED(ctrl, sectors_per_track);
        res += (ctrl->sector_number & 0xFF) - 1;
        break;
    }
    case 1: // LBA24
        res = ctrl->sector_number & 0xFF;
        res |= (ctrl->cylinder_low & 0xFF) << 8;
        res |= (ctrl->cylinder_high & 0xFF) << 16;
        res |= (ctrl->drive_and_head & 0x0F) << 24;
        break;
    case 2 ... 3: { // LBA48 commands override any IDE setting you may have set.
        res = ctrl->sector_number & 0xFF;
        res |= (uint64_t)(ctrl->cylinder_low & 0xFF) << 8L;
        res |= (uint64_t)(ctrl->cylinder_high & 0xFF) << 16L;
        res |= (uint64_t)(ctrl->sector_count >> 8 & 0xFF) << 24L;
        res |= (uint64_t)(ctrl->cylinder_low >> 8 & 0xFF) << 32L;
        res |= (uint64_t)(ctrl->cylinder_high >> 8 & 0xFF) << 40L;
        break;
    }
    }
    return res;
}
static void ide_set_sector_offset(struct ide_controller* ctrl, int lba48, uint64_t position)
{
    switch (lba48 << 1 | ctrl->lba) {
    case 0: { // CHS
        uint32_t heads_spt = TRANSLATED(ctrl, heads) * TRANSLATED(ctrl, sectors_per_track);
        uint32_t c = position / heads_spt;
        uint32_t t = position % heads_spt;
        uint32_t h = t / TRANSLATED(ctrl, sectors_per_track);
        uint32_t s = t % TRANSLATED(ctrl, sectors_per_track);

        ctrl->cylinder_low = c & 0xFF;
        ctrl->cylinder_high = c >> 8 & 0xFF;
        ctrl->drive_and_head = (ctrl->drive_and_head & 0xF0) | (h & 0x0F);
        ctrl->sector_number = s + 1;
        break;
    }
    case 1: // LBA24
        ctrl->drive_and_head = (ctrl->drive_and_head & 0xF0) | (position >> 24 & 0x0F);
        ctrl->cylinder_high = position >> 16 & 0xFF;
        ctrl->cylinder_low = position >> 8 & 0xFF;
        ctrl->sector_number = position >> 0 & 0xFF;
        break;
    case 2:
    case 3: { // LBA48
        // TODO: Make this more efficient
        ctrl->sector_number = position & 0xFF;
        ctrl->cylinder_low = position >> 8 & 0xFF;
        ctrl->cylinder_high = position >> 16 & 0xFF;
        ctrl->sector_number |= (position >> 24 & 0xFF) << 8;
        ctrl->cylinder_low |= (position >> 32 & 0xFF) << 8;
        ctrl->cylinder_high |= (position >> 40 & 0xFF) << 8;
        break;
    }
    }
}

static void ide_check_canary(struct ide_controller* ctrl)
{
    if (ctrl->canary_above != 0xDEADBEEF || ctrl->canary_below != 0xBEEFDEAD) {
        fprintf(stderr, "IDE PIO smashing canaries overwritten\n");
        IDE_FATAL("bad");
    }
}

// PIO buffer utilities. Useful for commands like IDENTIFY
static void ide_pio_store_byte(struct ide_controller* ctrl, int offset, uint8_t value)
{
    ctrl->pio_buffer[offset] = value;
}
static void ide_pio_store_word(struct ide_controller* ctrl, int offset, uint16_t value)
{
    ctrl->pio_buffer16[offset >> 1] = value;
}
static void ide_pio_store_word_be(struct ide_controller* ctrl, int offset, uint16_t value)
{
    ctrl->pio_buffer[offset] = value >> 8;
    ctrl->pio_buffer[offset + 1] = value;
}
static void ide_pio_store_dword_be(struct ide_controller* ctrl, int offset, uint32_t value)
{
    ctrl->pio_buffer[offset] = value >> 24;
    ctrl->pio_buffer[offset + 1] = value >> 16;
    ctrl->pio_buffer[offset + 2] = value >> 8;
    ctrl->pio_buffer[offset + 3] = value;
}
static void ide_pio_clear(struct ide_controller* ctrl, int offset, int length)
{
    for (int i = offset; i < (offset + length); i++)
        ctrl->pio_buffer[i] = 0;
}

// Store a string in the IDE PIO buffer.
//  Right justified strings (justify_left=0): "          HELLO WORLD"
//  Left justified strings (justify_left=1):  "HELLO WORLD          "
//  Swapped strings: "HELLO " --> "EHLL O"
static void ide_pio_store_string(struct ide_controller* ctrl, char* string, int pos, int length, int swap, int justify_left)
{
    char* buffer = alloca(length + 1); // Account for null-terminator
    // Justify the string.
    if (justify_left) {
        sprintf(buffer, "%-*s", length, string);
    } else {
        sprintf(buffer, "%*s", length, string);
    }
    for (int i = 0; i < length; i++) {
        ide_pio_store_byte(ctrl, pos + (i ^ swap), buffer[i]);
    }
}

// Simple utility functions to read data in big endian format
static inline uint16_t read16be(void* x)
{
    uint8_t* buf = x;
    return buf[0] << 8 | buf[1];
}
static inline uint32_t read32be(void* x)
{
    uint8_t* buf = x;
    return buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];
}

// These functions mess around with the interrupt status register.
// This function indicates  that there is a transfer pending
static void ide_atapi_init_transfer(struct ide_controller* ctrl)
{
    ctrl->sector_count = (ctrl->sector_count & 0xF8) | ATAPI_INTERRUPT_REASON_IO;
    ctrl->status |= ATA_STATUS_DRQ;
}

// This function indicates that it's ready to send command bytes
static void ide_atapi_init_command(struct ide_controller* ctrl)
{
    ctrl->sector_count = (ctrl->sector_count & 0xF8) | ATAPI_INTERRUPT_REASON_CoD;
    ctrl->status |= ATA_STATUS_DRQ;
}

// This function indicatesCPU that there is no transfer pending
static void ide_atapi_no_transfer(struct ide_controller* ctrl)
{
    ctrl->sector_count = (ctrl->sector_count & 0xF8) | ATAPI_INTERRUPT_REASON_IO | ATAPI_INTERRUPT_REASON_CoD;
    ctrl->status &= ~ATA_STATUS_DRQ;
}

// Abort an IDE command and mess around with the sense keys and "additional sense codes"
static void ide_atapi_abort(struct ide_controller* ctrl, int sense_key, int asc)
{
    IDE_LOG("ATAPI abort!!\n");
    ctrl->error = sense_key << 4;
    ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_ERR;
    ide_atapi_no_transfer(ctrl);
    ctrl->sense_key = sense_key;
    ctrl->asc = asc;
}
static void ide_atapi_start_transfer(struct ide_controller* ctrl, int size)
{
    ctrl->pio_position = 0;
    // Set byte count high/low
    ctrl->cylinder_low = size;
    ctrl->cylinder_high = size >> 8;
    ctrl->pio_length = size;
    ide_atapi_init_transfer(ctrl);
    ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DSC | ATA_STATUS_DRQ;

    if (ctrl->atapi_dma_enabled)
        IDE_FATAL("todo: dma trans\n");
    ide_raise_irq(ctrl);
}
static void ide_atapi_stop_command(struct ide_controller* ctrl)
{
    ctrl->pio_position = 0;
    ctrl->pio_length = 0;
    ide_atapi_no_transfer(ctrl);
    ctrl->status = ATA_STATUS_DRDY;
}

static void ide_atapi_read_complete(void* thisptr, int x)
{
    struct ide_controller* ctrl = thisptr;
    if (x == -1) {
        ide_atapi_abort(ctrl, ATAPI_SENSE_ILLEGAL_REQUEST, 0); // ?
        IDE_FATAL("ATAPI Read error todo\n");
    }
    ctrl->status &= ~ATA_STATUS_BSY;
    ide_atapi_init_transfer(ctrl);
    ctrl->status |= ATA_STATUS_DSC | ATA_STATUS_DRDY;

    ctrl->atapi_sectors_to_read--;
    ctrl->atapi_lba++;

    ctrl->cylinder_low = ctrl->atapi_bytes_to_transfer & 0xFF;
    ctrl->cylinder_high = ctrl->atapi_bytes_to_transfer >> 8 & 0xFF;
    //ctrl->pio_length = ctrl->atapi_sector_size;
    IDE_LOG("ATAPI: finished reading left=%d\n", ctrl->atapi_sectors_to_read);

    ide_raise_irq(ctrl);
}

// Read 1 sector of CD-ROM
// The number of bytes in ctrl->cylinder_[low|high] is how many sectors we transfer before we refill.
// The number of sectors in ctrl->sectors_to_read is how many sectors we have to read from the disk, in total.

static void ide_atapi_read(struct ide_controller* ctrl)
{
    IDE_LOG("   atapi read sector=%d\n", ctrl->atapi_lba);
    // XXX -- make sure that ctrl->atapi_lba * ctrl->atapi_sector_size can be over 0xFFFFFFFF
    int res = drive_read(SELECTED(ctrl, info), ctrl, ctrl->pio_buffer, ctrl->atapi_sector_size, ctrl->atapi_lba * ctrl->atapi_sector_size, ide_atapi_read_complete);

    // We have already prefetched this data
    if (res != DRIVE_RESULT_SYNC) {
        printf(" == Internal IDE inconsistency == ");
        printf("Fetch offset: %08x [blk%08x.bin]\n", ctrl->atapi_lba * ctrl->atapi_sector_size, (ctrl->atapi_lba * ctrl->atapi_sector_size) / (256 << 10));
        printf("Fetch bytes: %d\n", ctrl->atapi_sector_size);
        IDE_FATAL("Error trying to fetch already-fetched ATAPI data\n");
    }

    ide_atapi_init_transfer(ctrl);
    ctrl->status |= ATA_STATUS_DSC | ATA_STATUS_DRDY;

    ctrl->atapi_sectors_to_read--;
    ctrl->atapi_lba++;

    // Determine bytes to transfer
    uint32_t total_bytes = ctrl->atapi_cylinder_count;
    if (total_bytes > ctrl->atapi_bytes_to_transfer)
        total_bytes = ctrl->atapi_bytes_to_transfer;
    ctrl->atapi_frame_bytes_to_transfer = total_bytes;
    ctrl->atapi_frame_bytes_transferred = 0;

    ctrl->cylinder_low = total_bytes & 0xFF;
    ctrl->cylinder_high = total_bytes >> 8 & 0xFF;
    ctrl->pio_position = 0;
    ctrl->pio_length = total_bytes > ctrl->atapi_sector_size ? ctrl->atapi_sector_size : total_bytes;
    //ctrl->pio_length = ctrl->atapi_sector_size;
    IDE_LOG("ATAPI: finished reading\n");

    ide_raise_irq(ctrl);
}

static void ide_atapi_read_cb(void* thisptr, int stat)
{
    struct ide_controller* ctrl = thisptr;
    if (stat == -1) {
        IDE_FATAL("ATAPI: failed to read sector\n");
    }
    ctrl->status &= ~ATA_STATUS_BSY;
    ide_atapi_read(ctrl);
}

// Run an ATAPI command
static void ide_atapi_run_command(struct ide_controller* ctrl)
{
    // Copy all 12 bytes to a safe place
    uint8_t command[12];
    for (int i = 0; i < 12; i++)
        command[i] = ctrl->pio_buffer[i];

    ctrl->atapi_command = command[0];

    int dont_xor = -1;
    IDE_LOG("Command issued: %02x\n", command[0]);

    switch (command[0]) {
    case 0x00: // Test if ready
        IDE_LOG("Command: ATAPI: Test if ready\n");
        if (SELECTED(ctrl, media_inserted)) {
            ide_atapi_stop_command(ctrl);
        } else {
            ide_atapi_abort(ctrl, ATAPI_SENSE_NOT_READY, ATAPI_ASC_NOT_PRESENT);
        }
        ide_raise_irq(ctrl);
        break;
    case 0x03: // Request Sense
        IDE_LOG("Command: ATAPI Request Sense\n");
        ide_pio_clear(ctrl, 0, 18);
        ide_pio_store_byte(ctrl, 0, 0xF0);
        ide_pio_store_byte(ctrl, 2, ctrl->sense_key);
        ide_pio_store_byte(ctrl, 7, 10);
        ide_pio_store_byte(ctrl, 12, ctrl->asc >> 8); // ASC
        ide_pio_store_byte(ctrl, 13, ctrl->asc); // ASCQ
        if (ctrl->sense_key == 6)
            ctrl->sense_key = 0;
        ide_atapi_start_transfer(ctrl, command[4] > 18 ? 18 : command[4]);
        break;
    case 0x12: // Inquiry
        IDE_LOG("Command: ATAPI: Inquiry\n");
        ide_pio_store_byte(ctrl, 0, 0x05); // CD-ROM drive
        ide_pio_store_byte(ctrl, 1, 0x80); // Removable
        ide_pio_store_byte(ctrl, 2, 0x00); // Version
        ide_pio_store_byte(ctrl, 3, 0x21); // Version
        ide_pio_store_byte(ctrl, 4, 0x1F); // Extra data length
        ide_pio_store_byte(ctrl, 5, 0x00); // ?
        ide_pio_store_byte(ctrl, 6, 0x00); // ?
        ide_pio_store_byte(ctrl, 7, 0x00); // ?
        ide_pio_store_string(ctrl, "Halfix", 8, 8, 0, 1);
        ide_pio_store_string(ctrl, "Halfix CD-ROM", 16, 16, 0, 1);
        ide_pio_store_string(ctrl, "1.0", 24, 4, 0, 1);
        ide_atapi_start_transfer(ctrl, command[4] > 36 ? 36 : command[4]);
        break;
    case 0x1E: // Lock CD-ROM doors
        IDE_LOG("Command: ATAPI: %sock Doors\n", ~command[4] & 1 ? "Unl" : "L");
        if (SELECTED(ctrl, media_inserted)) {
            ctrl->atapi_can_eject_cdrom = ~command[4] & 1;
            ide_atapi_stop_command(ctrl); // everything is done
        } else {
            ide_atapi_abort(ctrl, ATAPI_SENSE_NOT_READY, ATAPI_ASC_NOT_PRESENT);
        }
        ide_raise_irq(ctrl);
        break;
    case 0x25: // Get media capacity
        IDE_LOG("Command: ATAPI: Get Capacity\n");
        if (SELECTED(ctrl, media_inserted)) {
            ide_pio_store_dword_be(ctrl, 0, SELECTED(ctrl, total_sectors) - 1);
            ide_pio_store_dword_be(ctrl, 4, 2048);
            ide_atapi_start_transfer(ctrl, 8);
        } else {
            ide_atapi_abort(ctrl, ATAPI_SENSE_NOT_READY, ATAPI_ASC_NOT_PRESENT);
            ide_raise_irq(ctrl);
        }
        break;
    case 0x43: {
        IDE_LOG("Command: ATAPI: Read table of contents\n");
        // Read table of contents. Based on values observed from Bochs and QEMU
        // https://www.bswd.com/sff8020i.pdf starting page 183
        int length = read16be(command + 7), nlength,
            format = command[9] >> 6,
            track_start = command[6],
            bufpos, sectors;
        ide_pio_clear(ctrl, 0, length);
        switch (format) {
        case 0: // Read TOC data format
            ide_pio_store_byte(ctrl, 2, 1);
            ide_pio_store_byte(ctrl, 3, 1);
            bufpos = 4;
            if (track_start < 2) {
                ide_pio_store_byte(ctrl, bufpos++, 0);
                ide_pio_store_byte(ctrl, bufpos++, 0x14);
                ide_pio_store_byte(ctrl, bufpos++, 0x01);
                ide_pio_store_byte(ctrl, bufpos++, 0);

                ide_pio_store_byte(ctrl, bufpos++, 0);
                ide_pio_store_byte(ctrl, bufpos++, 0);
                ide_pio_store_byte(ctrl, bufpos++, command[1] & 2);
                ide_pio_store_byte(ctrl, bufpos++, 0);
            }
            ide_pio_store_byte(ctrl, bufpos++, 0);
            ide_pio_store_byte(ctrl, bufpos++, 0x16);
            ide_pio_store_byte(ctrl, bufpos++, 0xAA);
            ide_pio_store_byte(ctrl, bufpos++, 0);

            sectors = SELECTED(ctrl, total_sectors);

            if (command[1] & 2) {
                ide_pio_store_byte(ctrl, bufpos++, 0);
                ide_pio_store_byte(ctrl, bufpos++, ((sectors + 150) / 75) / 60);
                ide_pio_store_byte(ctrl, bufpos++, ((sectors + 150) / 75) % 60);
                ide_pio_store_byte(ctrl, bufpos++, ((sectors + 150) % 75));
            } else {
                ide_pio_store_dword_be(ctrl, bufpos, sectors);
                bufpos += 4;
            }
            ide_pio_store_word_be(ctrl, 0, bufpos - 2);
            nlength = bufpos;
            break;
        case 1: // Multi-session
            nlength = 12;
            ide_pio_store_word_be(ctrl, 0, 0x0A); // TOC data length
            ide_pio_store_byte(ctrl, 2, 1); // First session
            ide_pio_store_byte(ctrl, 3, 1); // Last session
            break;
        case 2: // Raw TOC data
            ide_pio_store_byte(ctrl, 2, 1);
            ide_pio_store_byte(ctrl, 3, 1);
            bufpos = 4;
            for (int i = 0; i < 4; i++) {
                ide_pio_store_byte(ctrl, bufpos++, 0x01);
                ide_pio_store_byte(ctrl, bufpos++, 0x14);
                ide_pio_store_byte(ctrl, bufpos++, 0);
                if (i == 3)
                    ide_pio_store_byte(ctrl, bufpos++, 0xA3);
                else
                    ide_pio_store_byte(ctrl, bufpos++, i);
                ide_pio_store_byte(ctrl, bufpos++, 0);
                ide_pio_store_byte(ctrl, bufpos++, 0);
                ide_pio_store_byte(ctrl, bufpos++, 0);
                if (i & 2) {
                    sectors = SELECTED(ctrl, total_sectors);
                    if (command[1] & 2) {
                        ide_pio_store_byte(ctrl, bufpos++, 0);
                        ide_pio_store_byte(ctrl, bufpos++, ((sectors + 150) / 75) / 60);
                        ide_pio_store_byte(ctrl, bufpos++, ((sectors + 150) / 75) % 60);
                        ide_pio_store_byte(ctrl, bufpos++, ((sectors + 150) % 75));
                    } else {
                        ide_pio_store_dword_be(ctrl, bufpos, sectors);
                        bufpos += 4;
                    }
                } else {
                    ide_pio_store_byte(ctrl, bufpos++, 0);
                    ide_pio_store_byte(ctrl, bufpos++, 1);
                    ide_pio_store_byte(ctrl, bufpos++, 0);
                    ide_pio_store_byte(ctrl, bufpos++, 0);
                }
            }
            ide_pio_store_word_be(ctrl, 0, bufpos - 2);
            nlength = bufpos;
            break;
        case 3:
            IDE_FATAL("Unknown toc command 3\n");
        }
        IDE_LOG("nlength=%d length=%d\n", nlength, length);
        //ide_atapi_start_transfer(ctrl, nlength < length ? nlength : length);
        UNUSED(length);
        ide_atapi_start_transfer(ctrl, nlength);
        ide_raise_irq(ctrl);
        break;
    }
    case 0x1B:
        ide_atapi_no_transfer(ctrl);
        ide_raise_irq(ctrl);
        break;
    case 0x1A:
    case 0x5A: { // Mode sense
        int length, nlength;
        if (command[0] & 0x40)
            length = read16be(command + 6);
        else
            length = command[4];
        IDE_LOG("ATAPI: Mode Sense [len=%d x=%d]\n", length, command[2]);
        switch (command[2]) {
        // Current values
        case 1: // Error recovery
            ide_pio_clear(ctrl, 0, nlength = 16);
            ide_pio_store_word_be(ctrl, 0, 22);
            ide_pio_store_byte(ctrl, 2, 0x70);
            ide_pio_store_byte(ctrl, 8, 0x01);
            ide_pio_store_byte(ctrl, 9, 0x06);
            ide_pio_store_byte(ctrl, 11, 0x05); // Retry five times
            break;
        case 0x2A: // Capabilities
        case (2 << 6) | 0x2A:
            ide_pio_clear(ctrl, 0, nlength = 28);
            ide_pio_store_word_be(ctrl, 0, 34);
            ide_pio_store_byte(ctrl, 2, 0x70);
            ide_pio_store_byte(ctrl, 8, 0x2A);
            ide_pio_store_byte(ctrl, 9, 0x12);
            ide_pio_store_byte(ctrl, 12, 0x70);
            ide_pio_store_byte(ctrl, 13, 0x60);
            ide_pio_store_byte(ctrl, 14, 41 | 0); // TODO: Locked bit (bit 2)
            ide_pio_store_word_be(ctrl, 16, 706);
            ide_pio_store_word_be(ctrl, 18, 2);
            ide_pio_store_word_be(ctrl, 20, 512);
            ide_pio_store_word_be(ctrl, 22, 706);
            break;
        default:
            IDE_LOG("ATAPI: Unknown Mode Sense: %02x\n", command[2]);
            ide_atapi_abort(ctrl, ATAPI_SENSE_ILLEGAL_REQUEST, ATAPI_ASC_INVALID_FIELD);
            ide_raise_irq(ctrl);
            return;
        }

        ide_atapi_start_transfer(ctrl, nlength < length ? nlength : length);
        break;
    }
    case 0x28: // Read sectors
    case 0xA8: { // Read sectors
        uint32_t sectors, lba, total_sectors, bytecount;
        if (command[0] & 0x80)
            sectors = read32be(command + 6);
        else
            sectors = read16be(command + 7);

        lba = read32be(command + 2);
        IDE_LOG("ATAPI: Read %d sector starting %d ending %d\n", sectors, lba, lba + sectors);
        if (SELECTED(ctrl, media_inserted)) {
            total_sectors = SELECTED(ctrl, total_sectors);
            int tmp;
            if ((lba + sectors) >= total_sectors) {
                tmp = total_sectors - lba + 1;
                if (tmp < 0) {
                    // LBA is out of range
                    ide_atapi_abort(ctrl, ATAPI_SENSE_NOT_READY, ATAPI_ASC_OUT_OF_RANGE);
                    ide_raise_irq(ctrl);
                    break;
                } else if (tmp == 0) {
                    // LBA is in range
                    ide_atapi_stop_command(ctrl);
                    ide_raise_irq(ctrl);
                    break;
                } else
                    sectors = tmp - 1;
            }

            if (sectors == 0) {
                ide_atapi_stop_command(ctrl);
                break;
            }

            ctrl->atapi_lba = lba;
            ctrl->atapi_sectors_to_read = sectors;
            ctrl->atapi_sector_size = 2048;

            // Total number of bytes to transfer
            ctrl->atapi_cylinder_count = (ctrl->cylinder_high << 8 & 0xFF00) | (ctrl->cylinder_low & 0xFF);
            ctrl->atapi_bytes_to_transfer = bytecount = ctrl->atapi_sector_size * ctrl->atapi_sectors_to_read;
            ctrl->atapi_total_bytes_transferred = 0;

            // Reset cylinder low/high values
            ctrl->cylinder_low = 0;
            ctrl->cylinder_high = 0;

            // Bytecount must be even
            if (ctrl->atapi_cylinder_count & 1)
                ctrl->atapi_cylinder_count--;

            //if(command[4] == 5 && command[5] == 0x1D && command[8] == 0x3A) __asm__("int3");

            // Prefetch all the data beforehand
            IDE_LOG("Prefetch: %d start=%08x end=%08x\n", ctrl->atapi_cylinder_count, ctrl->atapi_lba * ctrl->atapi_sector_size, ctrl->atapi_lba * ctrl->atapi_sector_size + ctrl->atapi_bytes_to_transfer);
            //fprintf(stderr, "cylinder bytes=%d [0x%x] offs=%08x [blk%08x.bin] real bytes=0x%x\n", ctrl->atapi_cylinder_count, ctrl->atapi_cylinder_count, ctrl->atapi_lba * ctrl->atapi_sector_size, (ctrl->atapi_lba * ctrl->atapi_sector_size) / (256 * 1024), ctrl->atapi_bytes_to_transfer);
            int res = drive_prefetch(SELECTED(ctrl, info), ctrl, ctrl->atapi_bytes_to_transfer, ctrl->atapi_lba * ctrl->atapi_sector_size, ide_atapi_read_cb);
            if (res == DRIVE_RESULT_ASYNC) {
                ctrl->status |= ATA_STATUS_BSY | ATA_STATUS_DRDY | ATA_STATUS_DSC;
            } else if (res == DRIVE_RESULT_SYNC) {
                ide_atapi_read_cb(ctrl, 0);
            } else {
                ide_atapi_abort(ctrl, ATAPI_SENSE_NOT_READY, ATAPI_ASC_NOT_PRESENT);
                ide_raise_irq(ctrl);
            }
            dont_xor = 0;
        } else {
            ide_atapi_abort(ctrl, ATAPI_SENSE_NOT_READY, ATAPI_ASC_NOT_PRESENT);
            ide_raise_irq(ctrl);
        }
        break;
    }
    case 0x2B: // Seek
        IDE_LOG("ATAPI: Seek\n");
        if (SELECTED(ctrl, media_inserted)) {
            uint32_t lba = read32be(command + 2);
            if (lba >= SELECTED(ctrl, total_sectors)) {
                ide_atapi_abort(ctrl, ATAPI_SENSE_ILLEGAL_REQUEST, ATAPI_ASC_INVALID_OFFSET);
                ide_raise_irq(ctrl);
                break;
            }
            ide_atapi_stop_command(ctrl);
            ide_raise_irq(ctrl);
        } else {
            ide_atapi_abort(ctrl, ATAPI_SENSE_NOT_READY, ATAPI_ASC_NOT_PRESENT);
            ide_raise_irq(ctrl);
        }
        break;
    case 0x42: // Read sub-channel
        IDE_LOG("ATAPI: Read Sub-Channel (stubbed)\n");
        if (SELECTED(ctrl, media_inserted)) {
            int length = command[8] < 8 ? command[8] : 8;
            ide_pio_clear(ctrl, 0, length);
            ide_atapi_start_transfer(ctrl, length);
        } else {
            ide_atapi_abort(ctrl, ATAPI_SENSE_NOT_READY, ATAPI_ASC_NOT_PRESENT);
            ide_raise_irq(ctrl);
        }
        break;
    case 0x51: // Read disk information
        IDE_LOG("ATAPI: Read disk information (stubbed)\n");
        ide_atapi_abort(ctrl, ATAPI_SENSE_ILLEGAL_REQUEST, 36);
        break;
    case 0xBD: { // Mechanism status
        IDE_LOG("ATAPI: Mechanism status\n");
        int x = read16be(command + 8);
        ide_pio_clear(ctrl, 0, 8);
        ide_pio_store_byte(ctrl, 5, 1);
        ide_atapi_start_transfer(ctrl, x > 8 ? 8 : x);
        break;
    }
    case 0xBE: // Read CD (todo)
        IDE_LOG("ATAPI: Read CD (unimplemented)\n");
        if (SELECTED(ctrl, media_inserted)) {
            uint32_t length = read32be(command + 5) & 0x00FFFFFF,
                     lba = read32be(command + 2);
            if (length == 0) {
                ide_atapi_stop_command(ctrl);
                break;
            }
            UNUSED(lba);
            IDE_FATAL("TODO: ATAPI Read CD command\n");
        } else {
            ide_atapi_abort(ctrl, ATAPI_SENSE_NOT_READY, ATAPI_ASC_NOT_PRESENT);
            ide_raise_irq(ctrl);
        }
        break;
    case 0x46:
    case 0x8D:
    case 0x55:
    case 0xa6:
    case 0x4b:
    case 0x45:
    case 0x47:
    case 0xbc:
    case 0xb9:
    case 0x44:
    case 0xba:
    case 0xbb:
    case 0x4e:
    case 0x4a:
        IDE_LOG("ATAPI: Unknown command '%02x'\n", command[0]);
        ide_atapi_abort(ctrl, 5, ATAPI_ASC_OUT_OF_RANGE);
        ide_raise_irq(ctrl);
        break;
    default:
        DRIVE_FATAL("Unknown ATAPI command: %02x\n", command[0]);
    }

    if (!dont_xor)
        return;

    int bit = ATAPI_INTERRUPT_REASON_IO & dont_xor;
    if (!(ctrl->status & ATA_STATUS_BSY)) {
        ide_raise_irq(ctrl);
        if (ctrl->pio_length == 0) {
            bit |= ATAPI_INTERRUPT_REASON_CoD & dont_xor;
            ctrl->status &= ~ATA_STATUS_DRQ;
        }
    }
    ctrl->sector_count &= 0xF8;
    ctrl->sector_count |= bit;
}

// We need these functions to restart the command
static void ide_read_sectors(struct ide_controller* ctrl, int lba48, int chunk_count);
static void drive_write_callback(void* this, int res);

// After the PIO buffer is emptied, this function is called so that the drive knows what to do with the data
static void ide_pio_read_callback(struct ide_controller* ctrl)
{
    // Reset position to zero so that we don't keep writing out of bounds
    unsigned int old_pio = ctrl->pio_position;
    ctrl->pio_position = 0;

    ctrl->status &= ~ATA_STATUS_DRQ;

    switch (ctrl->command_issued) {
    case 0xA0: { // ATAPI command
        switch (ctrl->atapi_command) {
        case 0x28:
        case 0xA8: {
            if (ctrl->pio_length != old_pio) {
                IDE_FATAL("Expected: %x Got: %x\n", ctrl->pio_length, old_pio);
            }
            ctrl->atapi_frame_bytes_transferred += ctrl->pio_length;
            ctrl->atapi_total_bytes_transferred += ctrl->pio_length;
            if (ctrl->atapi_frame_bytes_transferred >= ctrl->atapi_frame_bytes_to_transfer) {
                // We are done with this series of reads
                ctrl->atapi_bytes_to_transfer -= ctrl->atapi_frame_bytes_transferred;
                IDE_LOG("Finished current frame : str=%d btt=%d %02x\n", ctrl->atapi_sectors_to_read, ctrl->atapi_bytes_to_transfer, ctrl->status);
                if (ctrl->atapi_bytes_to_transfer == 0) {
                    ctrl->cylinder_low = ctrl->atapi_frame_bytes_to_transfer & 0xFF;
                    ctrl->cylinder_high = ctrl->atapi_frame_bytes_to_transfer >> 8 & 0xFF;
                    ctrl->status = ATA_STATUS_DRDY;
                    ctrl->atapi_frame_bytes_transferred = 1;
                    ide_atapi_stop_command(ctrl);
                    ide_raise_irq(ctrl);
                } else {
                    // Check if there is still more to be read
                    ide_raise_irq(ctrl);
                    int continue_frame = 0;
                    if ((continue_frame = ctrl->atapi_total_bytes_transferred % ctrl->atapi_sector_size)) {
                        // No, we stopped in the middle of a frame. This could happen if the cylinder count is not a multiple of sector size.
                        ctrl->atapi_lba--; // Go back one sector and read the one we just finished.
                        ctrl->atapi_sectors_to_read++;
                    }
                    // There is still more to be read.
                    ide_atapi_read(ctrl);
                    // Copy the rest of the data to the beginning
                    memmove(ctrl->pio_buffer, ctrl->pio_buffer + continue_frame, ctrl->atapi_sector_size - continue_frame);
                    ctrl->pio_position = 0;
                    ctrl->pio_length = ctrl->atapi_sector_size - continue_frame;
                    ctrl->atapi_frame_bytes_transferred = 0;

                    IDE_LOG("Continue frame: %08x/%08x\n", ctrl->pio_position, ctrl->pio_length);
                }
            } else {
                IDE_LOG("Reading sector %d - %d left - frame %d/%d [res: %d], sectsize=%d\n", ctrl->atapi_lba, ctrl->atapi_sectors_to_read, ctrl->atapi_frame_bytes_transferred, ctrl->atapi_frame_bytes_to_transfer, -(ctrl->atapi_frame_bytes_transferred - ctrl->atapi_frame_bytes_to_transfer), ctrl->atapi_sector_size);
                // Reload, but don't reset anything.
                int res = drive_read(SELECTED(ctrl, info), ctrl, ctrl->pio_buffer, ctrl->atapi_sector_size, ctrl->atapi_lba * ctrl->atapi_sector_size, ide_atapi_read_complete);

                // We have already prefetched this data
                if (res != DRIVE_RESULT_SYNC) {
                    fprintf(stderr, " == Internal IDE inconsistency == ");
                    fprintf(stderr, "Fetch offset: %08x [blk%08x.bin]\n", ctrl->atapi_lba * ctrl->atapi_sector_size, (ctrl->atapi_lba * ctrl->atapi_sector_size) / (256 << 10));
                    fprintf(stderr, "Fetch bytes: %d\n", ctrl->atapi_sector_size);
                    IDE_FATAL("Error trying to fetch already-fetched ATAPI data\n");
                }

                unsigned int bytes_left = ctrl->atapi_frame_bytes_to_transfer - ctrl->atapi_frame_bytes_transferred;
                ctrl->pio_length = bytes_left > ctrl->atapi_sector_size ? ctrl->atapi_sector_size : bytes_left;
                IDE_LOG("pio length: %d\n", ctrl->pio_length);
                ide_atapi_init_transfer(ctrl);
                ctrl->atapi_lba++;
                ctrl->atapi_sectors_to_read--;
            }
            break;
        }

            ctrl->atapi_bytes_to_transfer -= ctrl->pio_length;
            if (ctrl->atapi_cylinder_count > ctrl->atapi_bytes_to_transfer)
                ctrl->atapi_cylinder_count = ctrl->atapi_bytes_to_transfer;
            if (ctrl->atapi_sectors_to_read == 0) {
                IDE_LOG("  finished atapi read: cylval: %08x\n", ctrl->atapi_bytes_to_transfer);
                ctrl->cylinder_low = ctrl->atapi_cylinder_count & 0xFF;
                ctrl->cylinder_high = ctrl->atapi_cylinder_count >> 8 & 0xFF;
                ctrl->sector_count &= 0xF8;
                ctrl->sector_count |= 3;
                break;
            }
            ide_atapi_read(ctrl);
            IDE_LOG("  bytes to trasnfer after %08x [cylcount=%08x]\n", ctrl->atapi_bytes_to_transfer, ctrl->cylinder_low | ctrl->cylinder_high << 8);
            break;
        default:
            ide_raise_irq(ctrl);
            ctrl->sector_count |= ATAPI_INTERRUPT_REASON_CoD;
            ctrl->error = 0;
        }
        break;
    }
    case 0xEC: // Identify
    case 0xA1: // ATAPI Identify
        break;
    case 0x29: // Read multiple, using LBA48
    case 0xC4: // Read multiple
    case 0x20: // Read, with retry
    case 0x21: // Read, without retry
    case 0x24: // Read, using LBA48
        // Raise an IRQ so that the OS knows we're done
        ide_raise_irq(ctrl);

        // Check that we haven't read too much.
        ide_check_canary(ctrl);

        // If there are still sectors yet to be read, then start reading them.
        if (ctrl->lba48 ? ctrl->sector_count != 0 : (ctrl->sector_count & 0xFF) != 0)
            ide_read_sectors(ctrl, ctrl->lba48, (ctrl->command_issued == 0x29 || ctrl->command_issued == 0xC4) ? ctrl->multiple_sectors_count : 1);
        else {
            // Otherwise, we're done
            ctrl->error = 0;
            ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
        }
        break;
    case 0: // Uninitialized, Windows NT does this.
        break;
    default:
        IDE_FATAL("Unknown PIO read command: %02x\n", ctrl->command_issued);
    }
}

// After the PIO buffer is full, this function is called so that the drive knows what to do with the data
static void ide_pio_write_callback(struct ide_controller* ctrl)
{
    ctrl->pio_position = 0;
    switch (ctrl->command_issued) {
    case 0xA0: // ATAPI Packet
        ide_atapi_run_command(ctrl);
        break;
    case 0x39: // Write multiple, using LBA48
    case 0xC5: // Write multiple
    case 0x30: // Write, with retry
    case 0x31: // Write, without retry
    case 0x34: { // Write, using LBA48
        ide_raise_irq(ctrl);

        uint64_t sector_offset = ide_get_sector_offset(ctrl, ctrl->lba48);
        IDE_LOG("Writing %d sectors at %llx\n", ctrl->sectors_read, (unsigned long long)sector_offset);
#ifndef EMSCRIPTEN
        printf("Writing %d sectors at %d\n", ctrl->sectors_read, (uint32_t)sector_offset);
#endif

        int res = drive_write(SELECTED(ctrl, info), ctrl, ctrl->pio_buffer, ctrl->sectors_read * 512, sector_offset * (uint64_t)512, drive_write_callback);
        if (res == DRIVE_RESULT_SYNC)
            drive_write_callback(ctrl, 0);
        else if (res == DRIVE_RESULT_ASYNC) {
            ctrl->status = ATA_STATUS_DSC | ATA_STATUS_DRDY | ATA_STATUS_BSY;
        } else
            ide_abort_command(ctrl);

        break;
    }
    default:
        IDE_FATAL("Unknown PIO write command: %02x\n", ctrl->command_issued);
    }
}

#ifdef PIO_LOG
static FILE* test;
#endif
// Read a byte from the PIO buffer
static uint32_t ide_pio_readb(uint32_t port)
{
    struct ide_controller* ctrl = &ide[~port >> 7 & 1];
    uint8_t result = ctrl->pio_buffer[ctrl->pio_position++];
    if (ctrl->pio_position >= ctrl->pio_length)
        ide_pio_read_callback(ctrl);
#ifdef PIO_LOG
    fprintf(test, "m 01f0 = %02x\n", result);
#endif
    return result;
}
// Read a word from the PIO buffer
static uint32_t ide_pio_readw(uint32_t port)
{
    struct ide_controller* ctrl = &ide[~port >> 7 & 1];
    // Penalize unaligned PIO accesses
    if ((ctrl->pio_position | ctrl->pio_length) & 1) {
        uint32_t res = ide_pio_readb(port);
        res |= ide_pio_readb(port) << 8;
        return res;
    }

    uint32_t result = ctrl->pio_buffer16[ctrl->pio_position >> 1];
    ctrl->pio_position += 2;
    if (ctrl->pio_position >= ctrl->pio_length)
        ide_pio_read_callback(ctrl);
#ifdef PIO_LOG
    fprintf(test, "m 01f0 = %04x\n", result);
#endif
    return result;
}
// Read a dword from the PIO buffer
static uint32_t ide_pio_readd(uint32_t port)
{
    struct ide_controller* ctrl = &ide[~port >> 7 & 1];
    // Penalize unaligned PIO accesses
    if ((ctrl->pio_position | ctrl->pio_length) & 3) {
        uint32_t res = ide_pio_readb(port);
        res |= ide_pio_readb(port) << 8;
        res |= ide_pio_readb(port) << 16;
        res |= ide_pio_readb(port) << 24;
        return res;
    }

    uint32_t result = ctrl->pio_buffer32[ctrl->pio_position >> 2];
    ctrl->pio_position += 4;
    if (ctrl->pio_position >= ctrl->pio_length)
        ide_pio_read_callback(ctrl);
#ifdef PIO_LOG
    fprintf(test, "m 01f0 = %08x\n", result);
#endif
    return result;
}

// Write a byte to the PIO buffer
static void ide_pio_writeb(uint32_t port, uint32_t data)
{
    struct ide_controller* ctrl = &ide[~port >> 7 & 1];
    ctrl->pio_buffer[ctrl->pio_position++] = data;
    if (ctrl->pio_position >= ctrl->pio_length)
        ide_pio_write_callback(ctrl);
#ifdef PIO_LOG
    fprintf(test, "o 01f0 = %02x\n", data);
#endif
}
// Write a word to the PIO buffer
static void ide_pio_writew(uint32_t port, uint32_t data)
{
    struct ide_controller* ctrl = &ide[~port >> 7 & 1];
#ifdef PIO_LOG
    fprintf(test, "o 01f0 = %04x\n", data);
#endif
    if ((ctrl->pio_position | ctrl->pio_length) & 1) {
        ide_pio_writeb(port, data & 0xFF);
        ide_pio_writeb(port, data >> 8 & 0xFF);
        return;
    }
    ctrl->pio_buffer16[ctrl->pio_position >> 1] = data;
    ctrl->pio_position += 2;
    if (ctrl->pio_position >= ctrl->pio_length)
        ide_pio_write_callback(ctrl);
}
// Write a dword to the PIO buffer
static void ide_pio_writed(uint32_t port, uint32_t data)
{
    struct ide_controller* ctrl = &ide[~port >> 7 & 1];
#ifdef PIO_LOG
    fprintf(test, "o 01f0 = %08x\n", data);
#endif
    if ((ctrl->pio_position | ctrl->pio_length) & 3) {
        ide_pio_writeb(port, data & 0xFF);
        ide_pio_writeb(port, data >> 8 & 0xFF);
        ide_pio_writeb(port, data >> 16 & 0xFF);
        ide_pio_writeb(port, data >> 24 & 0xFF);
        return;
    }
    ctrl->pio_buffer32[ctrl->pio_position >> 2] = data;
    ctrl->pio_position += 4;
    if (ctrl->pio_position >= ctrl->pio_length)
        ide_pio_write_callback(ctrl);
}

// Sets IDE signature. This is useful when trying to identify what kind of device exists at the end of the bus.
static void ide_set_signature(struct ide_controller* ctrl)
{
    ctrl->drive_and_head &= 15;
    ctrl->sector_number = 1;
    ctrl->sector_count = 1;
    switch (SELECTED(ctrl, type)) {
    case DRIVE_TYPE_NONE:
        ctrl->cylinder_low = 0xFF;
        ctrl->cylinder_high = 0xFF;
        break;
    case DRIVE_TYPE_DISK:
        ctrl->cylinder_low = 0;
        ctrl->cylinder_high = 0;
        break;
    case DRIVE_TYPE_CDROM:
        ctrl->cylinder_low = 0x14;
        ctrl->cylinder_high = 0xEB;
        break;
    }
}

// Read from an IDE port
static uint32_t ide_read(uint32_t port)
{
    struct ide_controller* ctrl = &ide[~port >> 7 & 1];
    UNUSED(ctrl);
    switch (port | 0x80) {
    case 0x1F1:
        return ctrl->error;
    case 0x1F2:
        return ctrl->sector_count;
    case 0x1F3:
        return ctrl->sector_number;
    case 0x1F4:
        return ctrl->cylinder_low;
    case 0x1F5:
        return ctrl->cylinder_high;
    case 0x1F6:
        return ctrl->drive_and_head;
    case 0x1F7:
        if (selected_drive_has_media(ctrl) || ctrl->device_control & 4) {
            ide_lower_irq(ctrl);
            //IDE_LOG("Status: %02x\n", ctrl->status);
            return ctrl->status;
        }
        return 0;
    case 0x3F6: // Read status without resetting IRQ. Returns 0 if the selected drive has no media
        return -selected_drive_has_media(ctrl) & ctrl->status;
    default:
        IDE_FATAL("Unknown IDE readb: 0x%x\n", port);
    }
}

static void ide_update_head(struct ide_controller* ctrl)
{
    ctrl->lba = ctrl->drive_and_head >> 6 & 1;
    ctrl->selected = ctrl->drive_and_head >> 4 & 1;
    IDE_LOG("Chose %s drive on %sary\n", ctrl->selected ? "slave" : "master", get_ctrl_id(ctrl) ? "second" : "prim");
}

static void ide_read_sectors_callback(void* this, int result)
{
    if (result < 0)
        ide_abort_command(this);
    struct ide_controller* ctrl = this;

    // Decrement sector count
    ctrl->sector_count -= ctrl->sectors_read;
    ctrl->error = 0;

    // Move sector registers forward
    ide_set_sector_offset(ctrl, ctrl->lba48, ide_get_sector_offset(ctrl, ctrl->lba48) + ctrl->sectors_read);

    ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DSC | ATA_STATUS_DRQ;
    ctrl->pio_length = ctrl->sectors_read * 512;
    ctrl->pio_position = 0;
    ide_raise_irq(ctrl);
}

// This function initializes the read and tells the block driver to hand the PIO buffer some bytes.
// This function will determine how many bytes to request from the block driver (chunk_count), the
// offset is (lba48 and sector_offset). It will raise an IRQ once it's over.
static void ide_read_sectors(struct ide_controller* ctrl, int lba48, int chunk_count)
{
    // What happens during an ATA read command?
    //  <ide_read_sectors>:
    //  - Determine number (chunk_count) and offset (sector_offset) of sectors to be read
    //  - Call block driver to get the data
    //  - Set error register to 0
    //  - Set status register to BSY+DRDY
    //  - If total number of sectors to read (sector_count) is less than chunk_count, then only read sector_count sectors
    //  - Otherwise, read sector_count sectors
    //  - If block driver is able to get our data synchronously, then call the read callback
    //  - If block driver is able to get our data asynchronously, then wait for the read callback to be called
    //  - If block driver is unable to read our data, then abort the command
    //  <ide_read_sectors_callback>:
    //  - Decrement sector_count by the number of sectors we just read (note: sector_count should NEVER be less than zero)
    //  - Set status register to DRDY+DSC+DRQ
    //  - Prep the PIO buffer for reading
    //  - Raise IRQ
    //  <ide_pio_read_callback@case 0x20>:
    //  - Advance forward the sector number, cylinder low, cylinder high, etc. registers
    //  - Raise IRQ
    //  - If sector_count is not zero, then call ide_read_sectors again
    //  - Otherwise, set DRDY+DSC

    // Note that sector_count != chunk_count, usually. "sector_count" is the TOTAL number of sectors to be transferred
    // over. "chunk_count" is the number of sectors to be transferred over at a time. For instance, with command 0x20,
    // chunk_count will be 1 (meaning that one chunk will be sent at a time) while sector_count is whatever you set the
    // sector count register as. Every time you read one sector, an IRQ will be raised and another sector will be read.

    // Save the registers here so that they can be fetched again in ide_read_sectors_callback and ide_pio_read_callback
    ctrl->lba48 = lba48;

    // Get count and offset
    uint32_t sector_count = ide_get_sector_count(ctrl, lba48);
    uint64_t sector_offset = ide_get_sector_offset(ctrl, lba48);

    ctrl->status = ATA_STATUS_DRDY /* | ATA_STATUS_BSY*/;

    int sectors_to_read = chunk_count;
    if (sector_count < (unsigned int)chunk_count)
        sectors_to_read = sector_count;
    ctrl->sectors_read = sectors_to_read;

#ifdef EMSCRIPTEN
    IDE_LOG("Reading %d sectors at %llx\n", sector_count, sector_offset);
#else
    IDE_LOG("Reading %d sectors at %x\n", sector_count, (uint32_t)sector_offset);
#endif
    if (sector_offset > 0xFFFFFFFF)
        IDE_LOG("Big sector!!\n");
    int res = drive_read(SELECTED(ctrl, info), ctrl, ctrl->pio_buffer, sectors_to_read * 512, sector_offset * (uint64_t)512, ide_read_sectors_callback);

    if (res < 0)
        ide_abort_command(ctrl);
    else if (res == 0)
        ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DSC | ATA_STATUS_BSY;
    else
        ide_read_sectors_callback(ctrl, 0);
}

static void ide_write_sectors(struct ide_controller* ctrl, int lba48, int chunk_count)
{
    // What happens during an ATA write command?
    //  <ide_write_sectors>:
    //  - Determine number (chunk_count) and offset (sector_offset) of sectors to be read
    //  - Prep the PIO buffer
    //  - Set status register to DRDY+DSC+DRQ
    //  - Prep the PIO buffer for writing
    //  - Raise IRQ
    //  <ide_pio_write_callback@case 0x30>:
    //  - Move registers forward
    //  - If data can be written synchronously, then call the write callback
    //  - If data can be written asynchronously, then wait for the read callback to be called and set BSY
    //  - If data cannot be written, abort the command.
    //  <ide_write_sectors_callback>
    //  - If sector count is zero, then set DRDY+DSC
    //  - Otherwise, call ide_write_sectors again

    ctrl->lba48 = lba48;

    uint32_t sector_count = ide_get_sector_count(ctrl, ctrl->lba48);
    int sectors_to_write = chunk_count;
    if (sector_count < (unsigned int)chunk_count)
        sectors_to_write = sector_count;
    ctrl->sectors_read = sectors_to_write;

    ctrl->error = 0;
    ctrl->status = ATA_STATUS_DSC | ATA_STATUS_DRDY | ATA_STATUS_DRQ;
    ctrl->pio_position = 0;
    ctrl->pio_length = ctrl->sectors_read * 512;
}
static void drive_write_callback(void* this, int result)
{
    if (result < 0)
        ide_abort_command(this);
    struct ide_controller* ctrl = this;
    // Decrement sector count
    ctrl->sector_count -= ctrl->sectors_read;

    // Move sector registers forward
    ide_set_sector_offset(ctrl, ctrl->lba48, ide_get_sector_offset(ctrl, ctrl->lba48) + ctrl->sectors_read);

    if (ctrl->lba48 ? ctrl->sector_count != 0 : (ctrl->sector_count & 0xFF) != 0)
        ide_write_sectors(ctrl, ctrl->lba48, (ctrl->command_issued == 0x39 || ctrl->command_issued == 0xC5) ? ctrl->multiple_sectors_count : 1);
    else {
        // Otherwise, we're done
        ctrl->error = 0;
        ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DSC; // Linux wants DSC
    }
}

static void ide_identify(struct ide_controller* ctrl)
{
    // See: ATAPI 7.1.7, ATA 7.7
    // Note: This Example --> hTsiE axpmel
    int cdrom = SELECTED(ctrl, type) == DRIVE_TYPE_CDROM;

    if (cdrom) {
        ide_pio_clear(ctrl, 0, 512);
        ide_pio_store_word(ctrl, 0, 0x85C0);
        // Serial number
        ide_pio_store_string(ctrl, "HFXCD 000000", 10 << 1, 20, 1, 1);
        ide_pio_store_string(ctrl, "0.0.1", 23 << 1, 8, 1, 1);
        ide_pio_store_string(ctrl, "Halfix CD-ROM drive", 27 << 1, 40, 1, 1);
        ide_pio_store_word(ctrl, 48 << 1, 1);
        int v = 512;
        if (ctrl->dma_enabled)
            v |= 128;
        ide_pio_store_word(ctrl, 48 << 1, v);
        ide_pio_store_word(ctrl, 53 << 1, 3);

        v = 0;
        if (ctrl->dma_enabled)
            v |= 7 | ctrl->mdma;
        ide_pio_store_word(ctrl, 63 << 1, v);
        ide_pio_store_word(ctrl, 64 << 1, 1);
        ide_pio_store_word(ctrl, 65 << 1, 0xB4);
        ide_pio_store_word(ctrl, 66 << 1, 0xB4);
        ide_pio_store_word(ctrl, 67 << 1, 0x12C);
        ide_pio_store_word(ctrl, 68 << 1, 0xB4);
        ide_pio_store_word(ctrl, 71 << 1, 30);
        ide_pio_store_word(ctrl, 72 << 1, 30);
        ide_pio_store_word(ctrl, 80 << 1, 0x1E);
    } else {

#define NOT_TRANSLATED(obj, field) obj->field[obj->selected << 1]
        // General configuration
        ide_pio_store_byte(ctrl, (0 << 1) | 0, 0x40);
        ide_pio_store_byte(ctrl, (0 << 1) | 1, -cdrom & 0x85);
        ide_pio_store_word(ctrl, 1 << 1, NOT_TRANSLATED(ctrl, cylinders));
        ide_pio_store_word(ctrl, 2 << 1, 0);
        ide_pio_store_word(ctrl, 3 << 1, NOT_TRANSLATED(ctrl, heads));
        ide_pio_store_word(ctrl, 4 << 1, NOT_TRANSLATED(ctrl, sectors_per_track) * 512);
        ide_pio_store_word(ctrl, 5 << 1, 512);
        ide_pio_store_word(ctrl, 6 << 1, NOT_TRANSLATED(ctrl, sectors_per_track));
        ide_pio_store_word(ctrl, 7 << 1, 0);
        ide_pio_store_word(ctrl, 8 << 1, 0);
        ide_pio_store_word(ctrl, 9 << 1, 0);
        ide_pio_store_string(ctrl, "HFXHD 000000", 10 << 1, 20, 1, 0);
        ide_pio_store_word(ctrl, 20 << 1, 3);
        ide_pio_store_word(ctrl, 21 << 1, 16 * 512 / 512);
        ide_pio_store_word(ctrl, 22 << 1, 4);
        ide_pio_store_word(ctrl, 23 << 1, 4); // TODO: Firmware Revision (8 chrs, left justified)
        ide_pio_store_word(ctrl, 24 << 1, 4);
        ide_pio_store_word(ctrl, 25 << 1, 4);
        ide_pio_store_word(ctrl, 26 << 1, 4);
        ide_pio_store_string(ctrl, "HALFIX 123456", 27 << 1, 40, 1, 1);
        ide_pio_store_word(ctrl, 47 << 1, MAX_MULTIPLE_SECTORS); // Max multiple sectors
        ide_pio_store_word(ctrl, 48 << 1, 1); // DWORD IO supported
        ide_pio_store_word(ctrl, 49 << 1, 1 << 9); // LBA supported (TODO: DMA)
        ide_pio_store_word(ctrl, 50 << 1, 0);
        ide_pio_store_word(ctrl, 51 << 1, 0x200);
        ide_pio_store_word(ctrl, 52 << 1, 0x200 | (ctrl->dma_enabled ? 0x100 : 0));
        ide_pio_store_word(ctrl, 53 << 1, 7);
        ide_pio_store_word(ctrl, 54 << 1, TRANSLATED(ctrl, cylinders));
        ide_pio_store_word(ctrl, 55 << 1, TRANSLATED(ctrl, heads));
        ide_pio_store_word(ctrl, 56 << 1, TRANSLATED(ctrl, sectors_per_track));
        ide_pio_store_word(ctrl, 57 << 1, SELECTED(ctrl, total_sectors_chs) & 0xFFFF);
        ide_pio_store_word(ctrl, 58 << 1, SELECTED(ctrl, total_sectors_chs) >> 16 & 0xFFFF);

        int multiple_sector_mask = -(ctrl->multiple_sectors_count != 0);
        ide_pio_store_word(ctrl, 59 << 1, multiple_sector_mask & (0x100 | ctrl->multiple_sectors_count));

        ide_pio_store_word(ctrl, 60 << 1, SELECTED(ctrl, total_sectors) & 0xFFFF);
        ide_pio_store_word(ctrl, 61 << 1, SELECTED(ctrl, total_sectors) >> 16 & 0xFFFF);
        ide_pio_store_word(ctrl, 62 << 1, 0);
        ide_pio_store_word(ctrl, 63 << 1, 7 | ctrl->mdma);
        for (int i = 64; i < 65; i++)
            ide_pio_store_word(ctrl, i << 1, 0);
        for (int i = 65; i < 69; i++)
            ide_pio_store_word(ctrl, i << 1, 0x78);
        for (int i = 69; i < 80; i++)
            ide_pio_store_word(ctrl, i << 1, 0);
        ide_pio_store_word(ctrl, 80 << 1, 0x7E);
        ide_pio_store_word(ctrl, 81 << 1, 0);
        ide_pio_store_word(ctrl, 82 << 1, 1 << 14);
        ide_pio_store_word(ctrl, 83 << 1, (1 << 14) | (1 << 13) | (1 << 12)); // TODO: Set bit 10 for LBA48
        ide_pio_store_word(ctrl, 84 << 1, 1 << 14);
        ide_pio_store_word(ctrl, 85 << 1, 1 << 14);
        ide_pio_store_word(ctrl, 86 << 1, (1 << 14) | (1 << 13) | (1 << 12)); // Same as word 83
        ide_pio_store_word(ctrl, 87 << 1, 1 << 14);
        ide_pio_store_word(ctrl, 88 << 1, -(ctrl->dma_enabled != 0) & (0x3F | ctrl->udma));
        for (int i = 89; i < 93; i++)
            ide_pio_store_word(ctrl, i << 1, 0);
        ide_pio_store_word(ctrl, 93 << 1, 24577);
        for (int i = 94; i < 100; i++)
            ide_pio_store_word(ctrl, i << 1, 0);
        ide_pio_store_word(ctrl, 100 << 1, SELECTED(ctrl, total_sectors) & 0xFFFF);
        ide_pio_store_word(ctrl, 101 << 1, SELECTED(ctrl, total_sectors) >> 16 & 0xFFFF);
        ide_pio_store_word(ctrl, 102 << 1, 0);
        ide_pio_store_word(ctrl, 103 << 1, 0);
    }
    ctrl->pio_length = 512;
    ctrl->pio_position = 0;
}

static void ide_read_dma_handler(void* this, int status)
{
    struct ide_controller* ctrl = this;
    UNUSED(status);
    uint32_t prdt_addr = ctrl->prdt_address,
             sectors = ide_get_sector_count(ctrl, ctrl->lba48),
             bytes_in_buffer = sectors * 512;
    uint64_t offset = ide_get_sector_offset(ctrl, ctrl->lba48) * 512ULL;
    struct drive_info* drv = SELECTED(ctrl, info);

    // XXX -- our goal should be to write it directly into memory
    void* temp = alloca(65536);
    while (1) {
        // Read fields from PRDT
        uint32_t dest = cpu_read_phys(prdt_addr), other_stuff = cpu_read_phys(prdt_addr + 4),
                 count = other_stuff & 0xFFFF, end = other_stuff & 0x80000000;
        count |= !count << 16; // If count is zero, then we requested 0x10000 bytes.

        uint32_t dma_bytes = count;
        if (dma_bytes > bytes_in_buffer)
            dma_bytes = bytes_in_buffer;

        // This should be a sync read
        IDE_LOG("PCI IDE read\n");
        IDE_LOG(" -- Destination: %08x\n", dest);
        IDE_LOG(" -- Length: %08x [real: %08x] End? %s\n", count, dma_bytes, end ? "Yes" : "No");
        IDE_LOG(" -- sector: %llx\n", (unsigned long long)offset >> 9);
        //if(offset == 0x19ba15000) __asm__("int3");

        // Invalidate the TLB for all the pages we are going to mess with
        {
            // Round up so that we catch every page
            int count_rounded = ((count + 0xFFF) >> 12) << 12;
            for (int i = 0; i < count_rounded; i += 4096)
                cpu_init_dma(dest + i);
        }
        while (dma_bytes >= 512) {
            int res = drive_read(drv, NULL, temp, 512, offset, NULL);
            if (res != DRIVE_RESULT_SYNC)
                IDE_FATAL("Expected sync response for prefetched data\n");

            cpu_write_mem(dest, temp, 512);
            dma_bytes -= 512;
            dest += 512;
            offset += 512;
        }

        // Move ourselves forward.

        bytes_in_buffer -= dma_bytes;
        offset += dma_bytes;
        prdt_addr += 8;
        if (!bytes_in_buffer || end)
            break;
    }
    ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
    ctrl->dma_status &= ~1;
    ctrl->dma_status |= 4;
    ide_set_sector_offset(ctrl, ctrl->lba48, ide_get_sector_offset(ctrl, ctrl->lba48) + sectors);
    ide_raise_irq(ctrl);
}

void drive_debug(int64_t x)
{
    uint32_t offset = x & 511;
    uint8_t buf[512];
    int res = drive_read(SELECTED((&ide[0]), info), NULL, buf, 512, x & ~511, NULL);
    if (res == DRIVE_RESULT_SYNC)
        IDE_LOG("Cannot read\n");
    for (int i = 0; i < 16; i++) {
        printf("%02x ", buf[offset]);
        offset++;
    }
    printf("\n");
}

static void ide_write_dma_handler(void* this, int status)
{
    struct ide_controller* ctrl = this;
    UNUSED(status);
    uint32_t prdt_addr = ctrl->prdt_address,
             sectors = ide_get_sector_count(ctrl, ctrl->lba48),
             bytes_in_buffer = sectors * 512;
    uint64_t offset = ide_get_sector_offset(ctrl, ctrl->lba48) * 512ULL;
    struct drive_info* drv = SELECTED(ctrl, info);

    void* mem = cpu_get_ram_ptr();

    while (1) {
        // Read fields from PRDT
        uint32_t dest = cpu_read_phys(prdt_addr), other_stuff = cpu_read_phys(prdt_addr + 4),
                 count = other_stuff & 0xFFFF, end = other_stuff & 0x80000000;
        count |= !count << 16; // If count is zero, then we requested 0x10000 bytes.

        uint32_t dma_bytes = count;
        if (dma_bytes > bytes_in_buffer)
            dma_bytes = bytes_in_buffer;

        // This should be a sync read
        IDE_LOG("PCI IDE write\n");
        IDE_LOG(" -- Destination: %08x\n", dest);
        IDE_LOG(" -- Length: %08x [real: %08x] End? %s\n", count, dma_bytes, end ? "Yes" : "No");
        IDE_LOG(" -- sector: %llx\n", (unsigned long long)offset >> 9);
        while (dma_bytes >= 512) {
            int res = drive_write(drv, NULL, mem + dest, 512, offset, NULL);
            if (res != DRIVE_RESULT_SYNC)
                IDE_FATAL("Expected sync response for prefetched data\n");
            dma_bytes -= 512;
            dest += 512;
            offset += 512;
        }

        // Move ourselves forward.

        bytes_in_buffer -= dma_bytes;
        offset += dma_bytes;
        prdt_addr += 8;
        if (!bytes_in_buffer || end)
            break;
    }
    ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
    ctrl->dma_status &= ~1;
    ctrl->dma_status |= 4;
    ide_set_sector_offset(ctrl, ctrl->lba48, ide_get_sector_offset(ctrl, ctrl->lba48) + sectors);
    ide_raise_irq(ctrl);
}

static void ide_read_dma(struct ide_controller* ctrl, int lba48)
{
    // Prefetch the sectors and write them to disk according to memory.
    ctrl->status = ATA_STATUS_DSC | ATA_STATUS_DRQ | ATA_STATUS_DRDY;
    ctrl->dma_status |= 1;
    ctrl->lba48 = lba48;
}

static void ide_write_dma(struct ide_controller* ctrl, int lba48)
{
    ctrl->status = ATA_STATUS_DSC | ATA_STATUS_DRQ | ATA_STATUS_DRDY;
    ctrl->dma_status |= 1;
    ctrl->lba48 = lba48;
}

// Write to an IDE port
static void ide_write(uint32_t port, uint32_t data)
{
    struct ide_controller* ctrl = &ide[~port >> 7 & 1];
    int ctrl_has_media = -controller_has_media(ctrl);
    switch (port | 0x80) {
    case 0x1F1:
        ctrl->feature = ctrl_has_media & data;
        break;
    case 0x1F2:
        ctrl->sector_count = ctrl_has_media & (ctrl->sector_count << 8 | data);
        IDE_LOG("%d\n", ctrl_has_media);
        break;
    case 0x1F3:
        ctrl->sector_number = ctrl_has_media & (ctrl->sector_number << 8 | data);
        break;
    case 0x1F4:
        ctrl->cylinder_low = ctrl_has_media & (ctrl->cylinder_low << 8 | data);
        break;
    case 0x1F5:
        ctrl->cylinder_high = ctrl_has_media & (ctrl->cylinder_high << 8 | data);
        break;
    case 0x1F6:
        if (ctrl_has_media) {
            ctrl->drive_and_head = data;
            ide_update_head(ctrl);
        }
        break;
    case 0x1F7: {
        // Things to do here:
        //  - Check if writes should be ignored
        //  - Clear IRQ
        //  - Check if BSY is set
        //  - Clear error register
        if (ctrl->selected == 1 && !selected_drive_has_media(ctrl))
            break;
        ide_lower_irq(ctrl);
        if (ctrl->status & ATA_STATUS_BSY) {
            IDE_LOG("Sending command when BSY bit is set\n");
            break;
        }
        ctrl->status &= ~ATA_STATUS_ERR;
        ctrl->command_issued = data;
        switch (data) {
        case 8: // ATAPI Reset
            IDE_LOG("ATAPI Reset\n");
            if (SELECTED(ctrl, type) == DRIVE_TYPE_CDROM) {
                ctrl->error &= ~ATA_ERROR_BBK;
                ctrl->error = ATA_ERROR_AMNF;
                ctrl->status = 0; // ?
                ide_set_signature(ctrl);
            } else
                ide_abort_command(ctrl);
            break;
        case 0x10 ... 0x1F: // Calibrate Drive
            if (SELECTED(ctrl, type) != DRIVE_TYPE_DISK)
                ide_abort_command(ctrl);
            if (!selected_drive_has_media(ctrl)) {
                ctrl->error = ATA_ERROR_TK0NF;
                ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_ERR;
            } else {
                // Set cylinder count to zero, DRDY+DSC, error register is already zeroed from above
                ctrl->cylinder_low = 0;
                ctrl->cylinder_high = 0;
                ctrl->error = 0;
                ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
            }
            ide_raise_irq(ctrl);
            break;
        case 0x25: // Read DMA lba48
        case 0xC8: // Read DMA
            IDE_LOG("Command: READ DMA [w/%s LBA48]\n", data == 0x25 ? "" : "o");
            ide_read_dma(ctrl, data == 0x25);
            break;
        case 0x35: // Write DMA lba48
        case 0xCA: // Write DMA
            IDE_LOG("Command: WRITE DMA [w/%s LBA48]\n", data == 0x35 ? "" : "o");
            ide_write_dma(ctrl, data == 0x35);
            break;
        case 0x29: // Read multiple, using LBA48
        case 0xC4: // Read multiple
            IDE_LOG("Command: READ MULTIPLE [w/%s LBA]\n", data == 0x29 ? "" : "o");
            if (ctrl->multiple_sectors_count == 0 || SELECTED(ctrl, type) != DRIVE_TYPE_DISK){
                IDE_LOG("READ MULTIPLE failed\n");
                ide_abort_command(ctrl);
            }else
                ide_read_sectors(ctrl, data == 0x29, ctrl->multiple_sectors_count);
            break;
        case 0x20: // Read, with retry
        case 0x21: // Read, without retry
        case 0x24: // Read, using LBA48
            IDE_LOG("Command: READ SINGLE [w/%s LBA48]\n", data == 0x24 ? "" : "o");
            if (SELECTED(ctrl, type) != DRIVE_TYPE_DISK)
                ide_abort_command(ctrl);
            else
                ide_read_sectors(ctrl, data == 0x24, 1);
            break;

        case 0x39: // Write multiple, using LBA48
        case 0xC5: // Write multiple
            IDE_LOG("Command: WRITE MULTIPLE [w/%s LBA]\n", data == 0x39 ? "" : "o");
            if (ctrl->multiple_sectors_count == 0 || SELECTED(ctrl, type) != DRIVE_TYPE_DISK)
                ide_abort_command(ctrl);
            else
                ide_write_sectors(ctrl, data == 0x39, ctrl->multiple_sectors_count);
            break;

        case 0x30: // Write, with retry
        case 0x31: // Write, without retry
        case 0x34: // Write, using LBA48
            IDE_LOG("Command: WRITE SINGLE [w/%s LBA48]\n", data == 0x24 ? "" : "o");
            if (SELECTED(ctrl, type) != DRIVE_TYPE_DISK)
                ide_abort_command(ctrl);
            else
                ide_write_sectors(ctrl, data == 0x34, 1);
            break;

        case 0x40: // Read Verify Sectors
        case 0x41: // Read Verify Sectors, no retry
        case 0x42: // Read Verify Sectors, LBA48
            if (SELECTED(ctrl, type) != DRIVE_TYPE_DISK)
                ide_abort_command(ctrl);
            else {
                int lba48 = data == 0x42;
                ide_set_sector_offset(ctrl, lba48, ide_get_sector_offset(ctrl, lba48) + (uint64_t)((ctrl->sector_count & 0xFF) - 1));
                ide->status = ATA_STATUS_DRDY;
                ide_raise_irq(ctrl);
            }
            break;

        case 0x91: // Initiallize Drive Parameters
            if (SELECTED(ctrl, type) != DRIVE_TYPE_DISK)
                ide_abort_command(ctrl);
            else {
                if (selected_drive_has_media(ctrl)) {
                    int sectors = ctrl->sector_count & 0xFF, heads = (ctrl->drive_and_head & 0x0F) + 1;
                    if (heads != 1) { // Command is still valid if ctrl->drive_and_head & 0x0F == 0 [note the +1 above], needed by Linux
#define TRANSLATED_IDX (ctrl->selected << 1) | 1
                        ctrl->sectors_per_track[TRANSLATED_IDX] = sectors;
                        ctrl->heads[TRANSLATED_IDX] = heads;
                        ctrl->cylinders[TRANSLATED_IDX] = SELECTED(ctrl, total_sectors) / (sectors * heads); // Is this right? IDK
                        ctrl->selected = 1;
                    }
                }
                ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
                ide_raise_irq(ctrl);
            }
            break;
        case 0xE5: // Windows NT 4 SP6
            IDE_LOG("Command: CHECK POWER MODE\n");
            ctrl->status = ATA_STATUS_DRDY;
            ctrl->sector_count = 255;
            ide_raise_irq(ctrl);
            break;
        case 0xE0:
        case 0xE1:
        case 0xE7:
        case 0xEA:
            IDE_LOG("Command: IDLE IMMEDIATE\n");
            ctrl->status = ATA_STATUS_DRDY;
            ide_raise_irq(ctrl); // nothing much to do here...
            break;
        case 0xA0: // ATAPI Packet
            IDE_LOG("Command: ATAPI Packet\n");
            if (SELECTED(ctrl, type) == DRIVE_TYPE_CDROM) {
                if (ctrl->feature & 2) {
                    IDE_LOG("Overlapped packet feature not supported\n");
                    ide_abort_command(ctrl);
                    break;
                }
                ide_atapi_init_command(ctrl);
                ctrl->status &= ~(ATA_STATUS_BSY | ATA_STATUS_DF);
                ctrl->atapi_dma_enabled = ctrl->feature & 1;

                // Prepare for PIO transfer
                ctrl->pio_length = 12;
                ctrl->pio_position = 0;
            } else
                ide_abort_command(ctrl);
            break;
        case 0xA1: // ATAPI Identify
            IDE_LOG("Command: ATAPI IDENTIFY\n");
            if (SELECTED(ctrl, type) == DRIVE_TYPE_CDROM) {
                ctrl->error = 0;
                ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DRQ | ATA_STATUS_DSC;
                ide_identify(ctrl);
                ide_raise_irq(ctrl);
            } else
                ide_abort_command(ctrl);
            break;

        case 0xEC: // Identify
            IDE_LOG("Command: IDENTIFY\n");
            if (!selected_drive_has_media(ctrl)) {
                IDE_LOG("Aborting: Selected drive doesn't have media\n");
                ide_abort_command(ctrl);
            } else if (SELECTED(ctrl, type) == DRIVE_TYPE_CDROM) {
                IDE_LOG("Aborting: Selected CD-ROM\n");
                ide_set_signature(ctrl);
                ide_abort_command(ctrl);
            } else {
                ide_identify(ctrl);
                ctrl->error = 0;
                ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DRQ | ATA_STATUS_DSC;
                ide_raise_irq(ctrl);
            }
            break;

        case 0xEF: // Set Features
            IDE_LOG("Command: SET FEATURES [idx=%02x]\n", ctrl->feature);
            switch (ctrl->feature) {
            case 3: // Set transfer mode
#ifndef DISABLE_MULTIPLE_SECTORS
                switch (ctrl->sector_count) {
                case 0 ... 15: // PIO
                    ctrl->mdma = 0;
                    ctrl->udma = 0;
                    break;
                case 32 ... 39: // MDMA
                    ctrl->mdma = 16 << (ctrl->sector_count & 7);
                    ctrl->udma = 0;
                    break;
                case 64 ... 0x71: // UDMA
                    ctrl->mdma = 0;
                    ctrl->udma = 16 << (ctrl->sector_count & 7);
                    break;
                default:
                    ide_abort_command(ctrl);
                    return;
                }
                ctrl->status = ATA_STATUS_DRDY | ATA_STATUS_DSC;
                ide_raise_irq(ctrl);
                break;
#else
                ide_abort_command(ctrl);
                break;
#endif
            case 2: // ?
            case 130: // ?
            case 0x66: // Windows XP writes to this one
            case 0x95:
            case 0xAA: // ReactOS
            case 0:
                ctrl->status = ATA_STATUS_DSC | ATA_STATUS_DRDY;
                ide_raise_irq(ctrl);
                break;
            default:
                IDE_FATAL("Unknown IDE feature\n");
            }
            break;
        case 0xC6: // Set Multiple Mode
            IDE_LOG("Command: SET MULTIPLE MODE (%d)\n", ctrl->sector_count & 0xFF);
            if (!selected_drive_has_media(ctrl))
                ide_abort_command(ctrl);
            else if (SELECTED(ctrl, type) == DRIVE_TYPE_CDROM)
                ide_abort_command(ctrl);
            else {
                int multiple_count = ctrl->sector_count & 0xFF;
                if (multiple_count > MAX_MULTIPLE_SECTORS || (multiple_count & (multiple_count - 1)) != 0) {
                    IDE_LOG("SET MULTIPLE MODE command failed");
                    ide_abort_command(ctrl);
                } else {
                    ctrl->multiple_sectors_count = multiple_count;
                    ctrl->status = ATA_STATUS_DRDY;
                    ide_raise_irq(ctrl);
                }
            }
            break;
        case 0xF8: { // Read max address, normal, seagate: Idle Immediate
            if (SELECTED(ctrl, type) != DRIVE_TYPE_DISK)
                ide_abort_command(ctrl);
            uint32_t multiple_count = SELECTED(ctrl, info)->sectors - 1;
            ide_set_sector_offset(ctrl, 0, multiple_count);
            break;
        }
        case 0xF9: // Set max, ReactOS
        case 0x2F:
        case 0xF0:
        case 0xF5: // No clue, but Windows XP writes to this register
        case 0xDA: // Windows 98 boot
        case 0xDE: // Windows 2000 boot
            IDE_LOG("Command %02x unknown, aborting!\n", data);
            ide_abort_command(ctrl);
            break;
        default:
            IDE_FATAL("Unknown command: %02x\n", data);
        }
        break;
    }
    case 0x3F6:
        IDE_LOG("Device Control Register: %02x\n", data);
        // Seagate Manual page 23
        if ((ctrl->device_control ^ data) & 4) {
            if (data & 4)
                ctrl->status |= ATA_STATUS_BSY;
            else {
                IDE_LOG("Reset controller id=%d\n", get_ctrl_id(ctrl));
                // Clears BSY bit, sets DRDY
                ctrl->status = ATA_STATUS_DRDY;
                ctrl->error = 1;
                ide_set_signature(ctrl);

                // Reset to master after we have set the signature
                ctrl->selected = 0;

                // Cancel any pending requests, if any.
                drive_cancel_transfers();
            }
        }
        ctrl->device_control = data;
        ide_update_irq(ctrl);
        break;
    default:
        IDE_FATAL("Unknown IDE writeb: 0x%x\n", port);
    }
}

// The following section is just for PCI-enabled DMA
void ide_write_prdt(uint32_t addr, uint32_t data)
{
    struct ide_controller* this = &ide[addr >> 3 & 1];
    switch (addr & 7) {
    case 0: {
        uint8_t diffxor = this->dma_command ^ data;
        if (diffxor & 1) { // Only update status bits if bit 0 changed.
            this->dma_command = data & 9;
            int lba48 = this->lba48, result;
            if ((data & 1) == 0)
                return;
            IDE_LOG("Executing DMA command\n");
            switch (this->command_issued) {
            case 0x25:
            case 0xC8:
                result = drive_prefetch(SELECTED(this, info), this, ide_get_sector_count(this, lba48), ide_get_sector_offset(this, lba48) << (drv_offset_t)9, ide_read_dma_handler);
                if (result == DRIVE_RESULT_SYNC)
                    ide_read_dma_handler(this, 0);
                else
                    this->status |= ATA_STATUS_BSY;
                break;
            case 0x35:
            case 0xCA:
                result = drive_prefetch(SELECTED(this, info), this, ide_get_sector_count(this, lba48), ide_get_sector_offset(this, lba48) << (drv_offset_t)9, ide_write_dma_handler);
                if (result == DRIVE_RESULT_SYNC)
                    ide_write_dma_handler(this, 0);
                else
                    this->status |= ATA_STATUS_BSY;
                break;
            }
        }
        break;
    }
    case 2:
        this->dma_status &= ~(data & 6);
        break;
    case 4 ... 7: {
        int shift = ((addr & 3) << 3);
        this->prdt_address &= ~(0xFF << shift);
        this->prdt_address |= data << shift;
        break;
    }
    default:
        IDE_FATAL("TODO: write prdt addr=%08x data=%02x\n", addr, data);
    }
}
uint32_t ide_read_prdt(uint32_t addr)
{
    struct ide_controller* this = &ide[addr >> 3 & 1];
    switch (addr & 7) {
    case 0:
        return this->dma_command;
    case 2:
        return this->dma_status;
    case 1:
    case 3:
        return 0; // Invalid
    case 4:
        return this->prdt_address >> 0 & 0xFF;
    case 5:
        return this->prdt_address >> 8 & 0xFF;
    case 6:
        return this->prdt_address >> 16 & 0xFF;
    case 7:
        return this->prdt_address >> 24 & 0xFF;
    }
    return 0; // unreachable
}

void ide_init(struct pc_settings* pc)
{
#ifdef PIO_LOG
    test = fopen("idelog.txt", "wb");
    setbuf(test, NULL);
#endif
    io_register_reset(ide_reset);
    state_register(ide_state);
    io_register_read(0x1F0, 1, ide_pio_readb, ide_pio_readw, ide_pio_readd);
    io_register_write(0x1F0, 1, ide_pio_writeb, ide_pio_writew, ide_pio_writed);
    io_register_read(0x170, 1, ide_pio_readb, ide_pio_readw, ide_pio_readd);
    io_register_write(0x170, 1, ide_pio_writeb, ide_pio_writew, ide_pio_writed);

    io_register_read(0x1F1, 7, ide_read, NULL, NULL);
    io_register_read(0x171, 7, ide_read, NULL, NULL);
    io_register_write(0x1F1, 7, ide_write, NULL, NULL);
    io_register_write(0x171, 7, ide_write, NULL, NULL);

    io_register_read(0x376, 1, ide_read, NULL, NULL);
    io_register_read(0x3F6, 1, ide_read, NULL, NULL);
    io_register_write(0x376, 1, ide_write, NULL, NULL);
    io_register_write(0x3F6, 1, ide_write, NULL, NULL);

    for (int i = 0; i < 4; i++) {
        struct drive_info* info = &pc->drives[i];
        struct ide_controller* ctrl = &ide[i >> 1];

        // Check to make sure that we are not overwriting boundaries
        ctrl->canary_above = 0xDEADBEEF;
        ctrl->canary_below = 0xBEEFDEAD;

        int drive_id = i & 1;
        ctrl->info[drive_id] = info;
        ctrl->dma_enabled = pc->pci_enabled;

        if (info->sectors != 0) {
            printf("Initializing disk %d\n", i);
            // Set appropriate DMA status bit
            ctrl->dma_status |= 0x20 << (i & 1);

            // Note that we store all these values whether it's a CD-ROM drive or not.
            // These values are bogus and ignored if they're an ATAPI drive.
            ctrl->sectors_per_track[drive_id << 1] = info->sectors_per_cylinder;
            ctrl->heads[drive_id << 1] = info->heads;
            ctrl->cylinders[drive_id << 1] = info->cylinders_per_head;
            ctrl->media_inserted[drive_id] = 1;
            ctrl->total_sectors_chs[drive_id] = info->cylinders_per_head * info->heads * info->sectors_per_cylinder;
            ctrl->total_sectors[drive_id] = info->type == DRIVE_TYPE_CDROM ? info->sectors >> 2 : info->sectors; // Adjust for 2048-byte sectors
        } else {
            ctrl->media_inserted[drive_id] = 0;
        }
        ctrl->type[drive_id] = info->type;
    }
}