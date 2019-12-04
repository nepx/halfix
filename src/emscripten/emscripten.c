// Emscripten helpers

#ifdef EMSCRIPTEN
#include <emscripten.h>
#else // So that VSCode doesn't complain
#define EMSCRIPTEN_KEEPALIVE
#define EM_ASM(y)
#define EM_ASM_(y, ...)
#define EM_ASM_INT(y, ...)
#endif
#include "cpu/cpu.h"
#include "display.h"
#include "devices.h"
#include "drive.h"
#include "pc.h"
#include "util.h"

void emscripten_handle_resize(void* framebuffer, int w, int h)
{
    EM_ASM_({
        window["update_size"]($0, $1, $2);
    },
        framebuffer, w, h);
}
void emscripten_flip(void)
{
    EM_ASM_({
        window["update_screen"]();
    });
}

// Emscripten disk stuff
// All state is stored in JavaScript land, the C functions here are simple wrappers

struct emscripten_driver {
    int disk_id;
} disks[4];

static int drive_emscripten_read(void* this, void* cb_ptr, void* buffer, uint32_t size, uint32_t offset, drive_cb cb)
{
    int id = ((struct emscripten_driver*)this)->disk_id;
    return EM_ASM_INT({
        return window["drive_read"]($0, $1, $2, $3, $4, $5);
    },
        id, buffer, size, offset, cb, cb_ptr);
}
static int drive_emscripten_write(void* this, void* cb_ptr, void* buffer, uint32_t size, uint32_t offset, drive_cb cb)
{
    int id = ((struct emscripten_driver*)this)->disk_id;
    int retval = EM_ASM_INT({
        return window["drive_write"]($0, $1, $2, $3, $4, $5);
    },
        id, buffer, size, offset, cb, cb_ptr);
    //printf("Return value: %d\n", retval);
    return retval;
}

// Initialize a fixed disk drive for reading. File names are handled in JavaScript land
int drive_emscripten_init(struct drive_info* info, int id, int size)
{
    disks[id].disk_id = id;
    info->data = &disks[id];

    info->read = drive_emscripten_read;
    info->write = drive_emscripten_write;

    info->sectors = size / 512;
    info->sectors_per_cylinder = 63;
    info->heads = 16;
    info->cylinders_per_head = info->sectors / (info->sectors_per_cylinder * info->heads);

    return 0;
}

static struct pc_settings pc;
EMSCRIPTEN_KEEPALIVE
void emscripten_enable_pci(int a){
    pc.pci_enabled = a;
}
EMSCRIPTEN_KEEPALIVE
void emscripten_enable_apic(int a){
    pc.apic_enabled = a;
}
EMSCRIPTEN_KEEPALIVE
void emscripten_enable_acpi(int a){
    pc.acpi_enabled = a;
}

EMSCRIPTEN_KEEPALIVE
void* emscripten_alloc(int is_bios, int size)
{
    void* biosdata;
    if (is_bios) {
        pc.bios.data = biosdata = aalloc(size, 4096);
        pc.bios.length = size;
    } else {
        pc.vgabios.data = biosdata = aalloc(size, 4096);
        pc.vgabios.length = size;
    }
    return biosdata;
}

// We have to use doubles here since JavaScript does not support longs.
EMSCRIPTEN_KEEPALIVE
void emscripten_set_time(double now){
    pc.current_time = (uint64_t)now;
}

EMSCRIPTEN_KEEPALIVE
double emscripten_get_cycles(void){
    return (double)cpu_get_cycles();
}

EMSCRIPTEN_KEEPALIVE
void emscripten_init_disk(int disk, int has_media, char* path, void* info)
{
    if (has_media) {
        pc.drives[disk].type = DRIVE_TYPE_DISK;
        drive_internal_init(&pc.drives[0], path, info);
    } else
        pc.drives[disk].type = DRIVE_TYPE_NONE;
}

EMSCRIPTEN_KEEPALIVE
void emscripten_set_memory_size(int mem){
    pc.memory_size = mem;
}

EMSCRIPTEN_KEEPALIVE
void emscripten_set_vga_memory_size(int mem){
    pc.vga_memory_size = mem;
}


EMSCRIPTEN_KEEPALIVE
void emscripten_bootorder(int seq, int id){
    pc.boot_sequence[seq] = (id == 'h') ? BOOT_DISK : (id == 'c' ? BOOT_CDROM : (id == 'f' ? BOOT_FLOPPY : BOOT_NONE));
}

EMSCRIPTEN_KEEPALIVE
void emscripten_init_floppy(int disk, int has_media, char* path, void* info)
{
    if (has_media) {
        pc.floppy_enabled = 1;
        pc.floppy_drives[disk].type = DRIVE_TYPE_DISK;
        drive_internal_init(&pc.floppy_drives[0], path, info);
    } else
        pc.floppy_drives[disk].type = DRIVE_TYPE_NONE;
}

EMSCRIPTEN_KEEPALIVE
int emscripten_initialize(void)
{
#if 0
pc.drives[0].type = DRIVE_TYPE_NONE;
    pc.drives[1].type = DRIVE_TYPE_NONE;
    pc.drives[2].type = DRIVE_TYPE_NONE;
    pc.drives[3].type = DRIVE_TYPE_NONE;
#endif

    pc.cpu_type = CPU_CLASS_486;
    if(pc.memory_size == 0)
        pc.memory_size = 32 * 1024 * 1024;
    if(pc.vga_memory_size == 0)
        pc.vga_memory_size = 2 * 1024 * 1024;
    printf("PC memory size: %d mb VGA memory: %d mb\n", pc.memory_size / 1024 / 1024, pc.vga_memory_size / 1024 / 1024);
    return pc_init(&pc);
}

EMSCRIPTEN_KEEPALIVE
int emscripten_run(void)
{
    int ms_to_sleep = pc_execute();
    vga_update();
    display_handle_events();
    return ms_to_sleep;
}
EMSCRIPTEN_KEEPALIVE
void emscripten_vga_update(void){
    vga_update();
}

void cpu_debug(void);
EMSCRIPTEN_KEEPALIVE
void emscripten_debug(void){
    cpu_debug();
}

void state_get_buffer(void);
EMSCRIPTEN_KEEPALIVE
void emscripten_savestate(void){
    state_get_buffer();
}

// does nothing
int main()
{
    return 0;
}