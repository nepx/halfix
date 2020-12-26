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

int drive_internal_init(struct drive_info* info, char* filename, void* info_dat, int);

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

// Initialize a fixed disk drive for reading. File names are handled in JavaScript land
EMSCRIPTEN_KEEPALIVE
int drive_emscripten_init(struct drive_info* info, void* a, void* b, int c)
{
    printf("%p %p\n", a, b);
    return drive_internal_init(info, a, b, c);
}

static struct pc_settings pc;
EMSCRIPTEN_KEEPALIVE
void* emscripten_alloc(int size, int align)
{
    return aalloc(size, align);
}

EMSCRIPTEN_KEEPALIVE
void* emscripten_get_pc_config(void){
    return &pc;
}

EMSCRIPTEN_KEEPALIVE
int emscripten_init(void){
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

EMSCRIPTEN_KEEPALIVE
void emscripten_load_state(void){
    state_read_from_file("");
}

EMSCRIPTEN_KEEPALIVE
double emscripten_get_cycles(void){
    return (double)cpu_get_cycles();
}
EMSCRIPTEN_KEEPALIVE
double emscripten_get_now(void){
    return (double)get_now(); 
}

void pc_set_fast(int yes);
EMSCRIPTEN_KEEPALIVE
void emscripten_set_fast(int val){
    pc_set_fast(val);
}

EMSCRIPTEN_KEEPALIVE
void emscripten_dyncall_vii(void* func, void* a, int b){
    // All *_cb() functions have the following signature: void (void*, int)
    ((void(*)(void*, int))func)(a, b);
}

// does nothing
int main()
{
    return 0;
}