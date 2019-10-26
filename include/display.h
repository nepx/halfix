#ifndef DISPLAY_H
#define DISPLAY_H

void display_init(void);
void display_update(int scanline_start, int scanlines);
void display_set_resolution(int width, int height);
void* display_get_pixels(void);
void display_handle_events(void);
void display_update_cycles(int cycles_elapsed, int us);
void display_sleep(int ms);

void display_release_mouse(void);

#endif