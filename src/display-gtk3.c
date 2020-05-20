// Simple display driver built with GTK3.
// Note that the browser version relies on SDL and has its own UI, so this is intended only for native.
#include "util.h"
#include <stdlib.h>

#include <gtk/gtk.h>

#define DISPLAY_LOG(x, ...) LOG("DISPLAY", x, ##__VA_ARGS__)
#define DISPLAY_FATAL(x, ...)          \
    do {                               \
        DISPLAY_LOG(x, ##__VA_ARGS__); \
        ABORT();                       \
    } while (0)

static GtkApplication* app;
static GtkWidget* window;
static GdkPixbuf* pix;

static void display_activate(GtkApplication* app, gpointer user_data)
{
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Halfix x86 emulator");
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 400);
    gtk_widget_show_all(window);
    UNUSED(user_data);
}

void display_init(void)
{
    app = gtk_application_new("org.nepx.halfix", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(display_activate), NULL);
    g_application_run(app, 0, NULL);
}
void display_handle_events(void)
{
}
void display_sleep(int ms)
{
    UNUSED(ms);
}

void display_release_mouse(void)
{
}

void display_set_resolution(int width, int height)
{
    UNUSED(width | height);
}
void* display_get_pixels(void)
{
    return NULL;
}
void display_update_cycles(int cycles_elapsed, int us)
{
    UNUSED(cycles_elapsed | us);
}
void display_update(int scanline_start, int scanlines)
{
    UNUSED(scanline_start | scanlines);
}