// Halfix generic VGA emulator
// http://www.osdever.net/FreeVGA/vga/portidx.htm
// ftp://ftp.apple.asimov.net/pub/apple_II/documentation/hardware/video/Second%20Sight%20VGA%20Registers.pdf
// The ET4000 manual (on archive.org)
// https://01.org/sites/default/files/documentation/ilk_ihd_os_vol3_part1r2_0.pdf
// https://ia801809.us.archive.org/11/items/bitsavers_ibmpccardseferenceManualMay92_1756350/IBM_VGA_XGA_Technical_Reference_Manual_May92.pdf
// https://www-user.tu-chemnitz.de/~kzs/tools/whatvga/vga.txt
// https://wiki.osdev.org/Bochs_VBE_Extensions

#include "cpuapi.h"
#include "devices.h"
#include "display.h"
#include "io.h"
#include "state.h"
#include <string.h>

#define VGA_LOG(x, ...) LOG("VGA", x, ##__VA_ARGS__)
#define VGA_FATAL(x, ...)          \
    do {                           \
        VGA_LOG(x, ##__VA_ARGS__); \
        abort();                   \
    } while (0)

#define VBE_LFB_BASE 0xE0000000

static struct vga_info {
    // <<< BEGIN STRUCT "struct" >>>

    /// ignore: framebuffer, vram, scanlines_modified, scanlines_to_update, mem, rom, rom_size

    // CRT Controller
    uint8_t crt[256], crt_index;
    // Attribute Controller
    uint8_t attr[32], attr_index, attr_palette[16];
    // Sequencer
    uint8_t seq[8], seq_index;
    // Graphics Registers
    uint8_t gfx[256], gfx_index;
    // Digital To Analog
    uint8_t dac[1024];
    uint32_t dac_palette[256];
    uint8_t dac_mask,
        dac_state, // 0 if reading, 3 if writing
        dac_address, // Index into dac_palette
        dac_color, // Current color being read (0: red, 1: blue, 2: green)
        dac_read_address // same as dac_address, but for reads
        ;

    // Status stuff
    uint8_t status[2];

    // Miscellaneous Graphics Register
    uint8_t misc;

    // Text Mode Rendering variables
    uint8_t char_width;
    uint32_t character_map[2];

    // General rendering variables
    uint8_t pixel_panning, current_pixel_panning;
    uint32_t total_height, total_width;
    int renderer;
    uint32_t current_scanline, character_scanline;
    uint32_t* framebuffer; // where pixel data is written to, created by SDL
    uint32_t framebuffer_offset; // the offset being written to right now
    uint32_t vram_addr; // Current VRAM offset being accessed by renderer
    uint32_t scanlines_to_update; // Number of scanlines to update per vga_update

    // Memory access settings
    uint8_t write_access, read_access, write_mode;
    uint32_t vram_window_base, vram_window_size;
    union {
        uint8_t latch8[4];
        uint32_t latch32;
    };

    // VBE stuff
    uint16_t vbe_index, vbe_version, vbe_enable;
    uint32_t vbe_regs[10];
    uint32_t vbe_bank;

    // PCI VGA stuff
    uint32_t vgabios_addr;
    uint8_t* mem;

    int vram_size;
    uint8_t *vram, *rom;
    uint32_t rom_size;
    // <<< END STRUCT "struct" >>>

    // These fields should not be saved in the VRAM savestate since they have to do with rendering.
    uint8_t* vbe_scanlines_modified;

    // Screen data cannot change if memory_modified is zero.
    int memory_modified;
} vga /* = { 0 }*/;

#define VBE_DISPI_DISABLED 0x00
#define VBE_DISPI_ENABLED 0x01
#define VBE_DISPI_GETCAPS 0x02
#define VBE_DISPI_8BIT_DAC 0x20
#define VBE_DISPI_LFB_ENABLED 0x40
#define VBE_DISPI_NOCLEARMEM 0x80

static void vga_update_size(void);

static void vga_alloc_mem(void)
{
    if (vga.vram)
        afree(vga.vram);
    vga.vram = aalloc(vga.vram_size, 8);
    memset(vga.vram, 0, vga.vram_size);
}

static void vga_state(void)
{
    // <<< BEGIN AUTOGENERATE "state" >>>
    struct bjson_object* obj = state_obj("vga", 42);
    state_field(obj, 256, "vga.crt", &vga.crt);
    state_field(obj, 1, "vga.crt_index", &vga.crt_index);
    state_field(obj, 32, "vga.attr", &vga.attr);
    state_field(obj, 1, "vga.attr_index", &vga.attr_index);
    state_field(obj, 16, "vga.attr_palette", &vga.attr_palette);
    state_field(obj, 8, "vga.seq", &vga.seq);
    state_field(obj, 1, "vga.seq_index", &vga.seq_index);
    state_field(obj, 256, "vga.gfx", &vga.gfx);
    state_field(obj, 1, "vga.gfx_index", &vga.gfx_index);
    state_field(obj, 1024, "vga.dac", &vga.dac);
    state_field(obj, 1024, "vga.dac_palette", &vga.dac_palette);
    state_field(obj, 1, "vga.dac_mask", &vga.dac_mask);
    state_field(obj, 1, "vga.dac_state", &vga.dac_state);
    state_field(obj, 1, "vga.dac_address", &vga.dac_address);
    state_field(obj, 1, "vga.dac_color", &vga.dac_color);
    state_field(obj, 1, "vga.dac_read_address", &vga.dac_read_address);
    state_field(obj, 2, "vga.status", &vga.status);
    state_field(obj, 1, "vga.misc", &vga.misc);
    state_field(obj, 1, "vga.char_width", &vga.char_width);
    state_field(obj, 8, "vga.character_map", &vga.character_map);
    state_field(obj, 1, "vga.pixel_panning", &vga.pixel_panning);
    state_field(obj, 1, "vga.current_pixel_panning", &vga.current_pixel_panning);
    state_field(obj, 4, "vga.total_height", &vga.total_height);
    state_field(obj, 4, "vga.total_width", &vga.total_width);
    state_field(obj, 4, "vga.renderer", &vga.renderer);
    state_field(obj, 4, "vga.current_scanline", &vga.current_scanline);
    state_field(obj, 4, "vga.character_scanline", &vga.character_scanline);
    state_field(obj, 4, "vga.framebuffer_offset", &vga.framebuffer_offset);
    state_field(obj, 4, "vga.vram_addr", &vga.vram_addr);
    state_field(obj, 1, "vga.write_access", &vga.write_access);
    state_field(obj, 1, "vga.read_access", &vga.read_access);
    state_field(obj, 1, "vga.write_mode", &vga.write_mode);
    state_field(obj, 4, "vga.vram_window_base", &vga.vram_window_base);
    state_field(obj, 4, "vga.vram_window_size", &vga.vram_window_size);
    state_field(obj, 4, "vga.latch8", &vga.latch8);
    state_field(obj, 2, "vga.vbe_index", &vga.vbe_index);
    state_field(obj, 2, "vga.vbe_version", &vga.vbe_version);
    state_field(obj, 2, "vga.vbe_enable", &vga.vbe_enable);
    state_field(obj, 40, "vga.vbe_regs", &vga.vbe_regs);
    state_field(obj, 4, "vga.vbe_bank", &vga.vbe_bank);
    state_field(obj, 4, "vga.vgabios_addr", &vga.vgabios_addr);
    state_field(obj, 4, "vga.vram_size", &vga.vram_size);
// <<< END AUTOGENERATE "state" >>>
    if (state_is_reading()) {
        vga_update_size();
        vga_alloc_mem();
    }
    state_file(vga.vram_size, "vram", vga.vram);

    // Force a redraw.
    vga.memory_modified = 3;
}

enum {
    CHAIN4,
    ODDEVEN,
    NORMAL,
    READMODE_1
};

enum {
    BLANK_RENDERER = 0, // Shows nothing on the screen
    ALPHANUMERIC_RENDERER = 2, // AlphaNumeric Mode (aka text mode)
    MODE_13H_RENDERER = 4, // Mode 13h
    RENDER_4BPP = 6,
    // VBE render modes
    RENDER_32BPP = 8, // Windows XP uses this
    RENDER_8BPP = 10, // Debian uses this one
    RENDER_16BPP = 12,
    RENDER_24BPP = 14
};

static void vga_update_mem_access(void)
{
    // Different VGA memory access modes.
    // Note that some have higher precedence than others; if Chain4 and Odd/Even write are both set, then Chain4 will be selected
    if (vga.seq[4] & 8)
        vga.write_access = CHAIN4;
    else if (!(vga.seq[4] & 4)) // Note: bit has to be 0
        vga.write_access = ODDEVEN;
    else
        vga.write_access = NORMAL;

    if (vga.gfx[5] & 8)
        vga.read_access = READMODE_1;
    else if (vga.seq[4] & 8) // Note: Same bit as write
        vga.read_access = CHAIN4;
    else if (vga.gfx[5] & 0x10) // Note: Different bit than write
        vga.read_access = ODDEVEN;
    else
        vga.read_access = NORMAL;

    vga.write_mode = vga.gfx[5] & 3;
    VGA_LOG("Updating Memory Access Constants: write=%d [mode=%d], read=%d\n", vga.write_access, vga.write_mode, vga.read_access);
}
// despite its name, it only resets drawing state
static void vga_complete_redraw(void)
{
    vga.current_scanline = 0;
    vga.character_scanline = vga.crt[8] & 0x1F;
    vga.current_pixel_panning = vga.pixel_panning;
    vga.vram_addr = ((vga.crt[0x0C] << 8) | vga.crt[0x0D]) << 2; // Video Address Start is done by planar offset
    vga.framebuffer_offset = 0;

    // Force a complete redraw of the screen, and to do that, pretend that memory has been written.
    vga.memory_modified = 3;
}

static void vga_change_renderer(void)
{

    // Check if VBE is enabled.
    if (vga.vbe_enable & VBE_DISPI_ENABLED) {
        switch (vga.vbe_regs[3]) {
        // Depends on BPP
        case 8:
            vga.renderer = RENDER_8BPP;
            break;
        case 16:
            vga.renderer = RENDER_16BPP;
            break;
        case 24:
            vga.renderer = RENDER_24BPP;
            break;
        case 32:
            vga.renderer = RENDER_32BPP;
            break;
        default:
            VGA_FATAL("TODO: support %dbpp displays!\n", vga.vbe_regs[3]);
        }
        goto done;
    }

    // First things first: check if screen is enabled
    if (((vga.seq[1] & 0x20) == 0) && (vga.attr_index & 0x20)) {
        if (vga.gfx[6] & 1) {
            // graphics mode
            if (vga.gfx[5] & 0x40) {
                // 256 mode (AKA mode 13h)
                vga.renderer = MODE_13H_RENDERER;
                vga.renderer |= vga.attr[0x10] >> 6 & 1;
                goto done;
            } else {
                if (!(vga.gfx[5] & 0x20)) {
                    vga.renderer = RENDER_4BPP;
                } else
                    VGA_FATAL("TODO: other gfx mode\n");
            }
        } else {
            // alphanumeric
            vga.renderer = ALPHANUMERIC_RENDERER;
        }
    } else {
        vga.renderer = BLANK_RENDERER;
    }
    VGA_LOG("Change renderer to: %d\n", vga.renderer);
    vga.renderer |= (vga.seq[1] >> 3 & 1);
done:
    vga_complete_redraw();
}

static uint32_t vga_char_map_address(int b)
{
    return b << 13;
}

static void vga_update_size(void)
{
    int width, height;
    // Check if VBE is enabled, and if so, use that
    if (vga.vbe_enable & VBE_DISPI_ENABLED) {
        width = vga.vbe_regs[1]; // xres
        height = vga.vbe_regs[2]; // yres
    } else {
        // CR01 and CR02 control width.
        // Technically, CR01 should be less than CR02, but that may not always be the case.
        // Both should be less than CR00
        int horizontal_display_enable_end = vga.crt[1] + 1;
        int horizontal_blanking_start = vga.crt[2];
        int total_horizontal_characters = (horizontal_display_enable_end < horizontal_blanking_start) ? horizontal_display_enable_end : horizontal_blanking_start;
        // Screen width is measured in terms of characters
        width = total_horizontal_characters * vga.char_width;

        // CR12 and CR15 control height
        int vertical_display_enable_end = (vga.crt[0x12] + (((vga.crt[0x07] >> 1 & 1) | (vga.crt[0x07] >> 5 & 2)) << 8)) + 1;
        int vertical_blanking_start = vga.crt[0x15] + (((vga.crt[0x07] >> 3 & 1) | (vga.crt[0x09] >> 4 & 2)) << 8);
        height = vertical_display_enable_end < vertical_blanking_start ? vertical_display_enable_end : vertical_blanking_start;
    }

    display_set_resolution(width, height);

    vga.framebuffer = display_get_pixels();

    vga.total_height = height;
    vga.total_width = width;

    if (vga.vbe_scanlines_modified)
        vga.vbe_scanlines_modified = realloc(vga.vbe_scanlines_modified, vga.total_height);
    else
        vga.vbe_scanlines_modified = malloc(vga.total_height);
    memset(vga.vbe_scanlines_modified, 1, vga.total_height);

    vga.scanlines_to_update = height >> 1;
}

static uint8_t c6to8(uint8_t a)
{
    if (vga.vbe_enable & VBE_DISPI_8BIT_DAC)
        return a;
    uint8_t b = a & 1;
    return a << 2 | b << 1 | b;
}
static void update_one_dac_entry(int i)
{
    int index = i << 2;
#ifndef EMSCRIPTEN
    vga.dac_palette[i] = 255 << 24 | c6to8(vga.dac[index | 0]) << 16 | c6to8(vga.dac[index | 1]) << 8 | c6to8(vga.dac[index | 2]);
#else
    // Reverse order of palette
    vga.dac_palette[i] = 255 << 24 | c6to8(vga.dac[index | 2]) << 16 | c6to8(vga.dac[index | 1]) << 8 | c6to8(vga.dac[index | 0]);
#endif
}
static void update_all_dac_entries(void)
{
    for (int i = 0; i < 256; i++) {
        update_one_dac_entry(i);
    }
}

static void vga_change_attr_cache(int i)
{
    if (vga.attr[0x10] & 0x80)
        vga.attr_palette[i] = (vga.attr[i] & 0x0F) | ((vga.attr[0x14] << 4) & 0xF0);
    else
        vga.attr_palette[i] = (vga.attr[i] & 0x3F) | ((vga.attr[0x14] << 4) & 0xC0);
}

#define MASK(n) (uint8_t)(~n)

static const uint32_t vbe_maximums[3] = { 1024, 768, 32 };

#ifndef VGA_LIBRARY
static
#endif
    void
    vga_write(uint32_t port, uint32_t data)
{
    if ((port >= 0x3B0 && port <= 0x3BF && (vga.misc & 1)) || (port >= 0x3D0 && port <= 0x3DF && !(vga.misc & 1))) {
        VGA_LOG("Ignoring unsupported write to addr=%04x data=%02x misc=%02x\n", port, data, vga.misc);
        return;
    }
    uint8_t diffxor;
    switch (port) {
    case 0x1CE: // Bochs VBE index
        vga.vbe_index = data;
        break;
    case 0x1CF: // Bochs VBE data
        switch (vga.vbe_index) {
        case 0:
            vga.vbe_version = data;
            break;
        case 1 ... 3:
            if (vga.vbe_enable & VBE_DISPI_GETCAPS)
                VGA_LOG("Ignoring write (%d): GETCAPS bit\n", port);
            else {
                if (vga.vbe_index == 3 && data == 0)
                    data = 8;
                if (!(vga.vbe_enable & VBE_DISPI_ENABLED)) {
                    if (data <= vbe_maximums[vga.vbe_index - 1])
                        vga.vbe_regs[vga.vbe_index] = data; // Note: no "vga.vbe_index - 1" required here
                    else
                        VGA_LOG("VBE reg out of range: reg=%d val=%x\n", port, data);
                } else
                    VGA_LOG("Setting reg %d when VBE is enabled\n", vga.vbe_index);
            }
            break;
        case 4:
            diffxor = vga.vbe_enable ^ data;
            if (diffxor) {
                if (!(diffxor & VBE_DISPI_ENABLED)) {
                    data &= ~VBE_DISPI_LFB_ENABLED;
                    data |= vga.vbe_enable & VBE_DISPI_LFB_ENABLED;
                }
                VGA_LOG(" Set VBE enable=%04x bpp=%d diffxor=%04x current=%04x\n", data, vga.vbe_regs[3], diffxor, vga.vbe_enable);
                vga.vbe_enable = data;
                if (vga.vbe_regs[3] == 4)
                    VGA_FATAL("TODO: support VBE 4-bit modes\n");

                int width = vga.vbe_regs[1], // AKA xres
                    height = vga.vbe_regs[2]; // AKA yres
                //int bytes_per_pixel = (vga.vbe_regs[3] + 7) >> 3, total_bytes_used = bytes_per_pixel * width * height;

                vga.total_height = height;
                vga.total_width = width;
                vga_update_size();

                if (diffxor & VBE_DISPI_ENABLED) {
                    vga_change_renderer();
                    if (vga.vbe_enable & VBE_DISPI_ENABLED)
                        if (!(data & VBE_DISPI_NOCLEARMEM)) // should i use diffxor or data?
                            memset(vga.vram, 0, vga.vram_size);
                }

                if (diffxor & VBE_DISPI_8BIT_DAC) {
                    // 8-bit DAC: TODO
                    update_all_dac_entries();
                }

                vga.vbe_regs[8] = 0;
                vga.vbe_regs[9] = 0;
                vga.vbe_regs[6] = vga.total_width;
                vga.vbe_regs[7] = vga.total_height;
                // TODO...
            }
            break;
        case 5:
            data <<= 16;
            if (data >= (unsigned int)vga.vram_size)
                VGA_FATAL("Unsupported VBE bank offset: %08x\n", data);
            vga.vbe_regs[5] = data;
            break;
        case 6: { // vbe virtual width
            int bpp = (vga.vbe_regs[3] + 7) >> 3;
            vga.vbe_regs[6] = data;
            if (bpp)
                vga.vbe_regs[7] = vga.vram_size / bpp;
            else
                vga.vbe_regs[7] = 1;
            break;
        }
        case 7: // vbe virtual height
            vga.vbe_regs[7] = data;
            break;
        case 8 ... 9:
            vga.vbe_regs[vga.vbe_index] = data;
            break;
        default:
            VGA_FATAL("Unknown VBE register: %d\n", vga.vbe_index);
        }
        break;
    case 0x3C0: // Attribute controller register
        if (!(vga.attr_index & 0x80)) {
            // Select attribute index
            diffxor = (vga.attr_index ^ data);
            vga.attr_index = data & 0x7F /* | (vga.attr_index & 0x80) */; // We already know that attr_index is zero
            if (diffxor & 0x20)
                vga_change_renderer();
            vga.attr_index = data & 0x7F /* | (vga.attr_index & 0x80) */; // We already know that attr_index is zero
        } else {
            // Select attribute data
            uint8_t index = vga.attr_index & 0x1F;
            diffxor = vga.attr[index] ^ data;
            if (diffxor) {
                vga.attr[index] = data;
                switch (index) {
                case 0 ... 15:
                    if (diffxor & 0x3F)
                        vga_change_attr_cache(index);
                    break;
                case 16: // Mode Control Register, mostly for text modes
                    /*
bit   0  Graphics mode if set, Alphanumeric mode else.
      1  Monochrome mode if set, color mode else.
      2  9-bit wide characters if set.
         The 9th bit of characters C0h-DFh will be the same as
         the 8th bit. Otherwise it will be the background color.
      3  If set Attribute bit 7 is blinking, else high intensity.
      5  (VGA Only) If set the PEL panning register (3C0h index 13h) is
         temporarily set to 0 from when the line compare causes a wrap around
         until the next vertical retrace when the register is automatically
         reloaded with the old value, else the PEL panning register ignores
         line compares.
      6  (VGA Only) If set pixels are 8 bits wide. Used in 256 color modes.
      7  (VGA Only) If set bit 4-5 of the index into the DAC table are taken
         from port 3C0h index 14h bit 0-1, else the bits in the palette
         register are used.
                */
                    if (diffxor & ((1 << 0) | // Alphanumeric/Graphical Mode
                                      //(1 << 5) | // Line Compare Register
                                      (1 << 6)) // Pixel Width
                        )
                        vga_change_renderer(); // Changes between graphics/alphanumeric mode
                    if (diffxor & 0x80)
                        for (int i = 0; i < 16; i++)
                            vga_change_attr_cache(i);
                    if (diffxor & ((1 << 2) | // Character Width
                                      (1 << 3) | // Blinking
                                      (1 << 5)) // Line compare reset PEL Panning
                        )
                        vga_complete_redraw();
                    VGA_LOG("Mode Control Register: %02x\n", data);
                    break;
                case 17: // Overscan color register break;
                    VGA_LOG("Overscan color (currently unused): %02x\n", data);
                    break;
                case 18: // Color Plane Enable
                    VGA_LOG("Color plane enable: %02x\n", data);
                    vga.attr[18] &= 0x0F;
                    break;
                case 19: // Horizontal PEL Panning Register
                    // This register enables you to shift display data "x" pixels to the left.
                    // However, in an effort to confuse people, this value is interpreted differently based on graphics mode
                    //
                    //    pixels to shift left
                    // Value 8-dot 9-dot 256 color
                    //   0     0     1       0
                    //   1     1     2       -
                    //   2     2     3       1
                    //   3     3     4       -
                    //   4     4     5       2
                    //   5     5     6       -
                    //   6     6     7       3
                    //   7     7     8       -
                    //   8     -     0       -
                    //   9 and above: all undefined
                    // Note that due to these restrictions, it's impossible to obscure a full col of characters (and why would you want to do such a thing?)
                    if (data > 8)
                        VGA_FATAL("Unknown PEL pixel panning value");
                    if (vga.gfx[5] & 0x40)
                        vga.pixel_panning = data >> 1 & 3;
                    else
                        vga.pixel_panning = (data & 7) + (vga.char_width & 1);
                    VGA_LOG("Pixel panning: %d [raw], %d [effective value]\n", data, vga.pixel_panning);
                    break;
                case 20: // Color Select Register
                    VGA_LOG("Color select register: %02x\n", data);
                    if (diffxor & 15)
                        for (int i = 0; i < 16; i++)
                            vga_change_attr_cache(i);
                    break;
                }
            }
        }
        vga.attr_index ^= 0x80;
        break;
    case 0x3C2: // Miscellaneous Register
        VGA_LOG("Write VGA miscellaneous register: 0x%02x\n", data);
        /*
bit   0  If set Color Emulation. Base Address=3Dxh else Mono Emulation. Base
         Address=3Bxh.
      1  Enable CPU Access to video memory if set
    2-3  Clock Select
          0: 14MHz(EGA)     25MHz(VGA)
          1: 16MHz(EGA)     28MHz(VGA)
          2: External(EGA)  Reserved(VGA)
      4  (EGA Only) Disable internal video drivers if set
      5  When in Odd/Even modes Select High 64k bank if set
      6  Horizontal Sync Polarity. Negative if set
      7  Vertical Sync Polarity. Negative if set
         Bit 6-7 indicates the number of lines on the display:
              0=200(EGA)  Reserved(VGA)
              1=          400(VGA)
              2=350(EGA)  350(VGA)
              3=          480(VGA).
        */
        vga.misc = data;
        break;
    case 0x3B8:
    case 0x3BF: // ???
    case 0x3C3: // ???
    case 0x3DA:
    case 0x3D8:
    case 0x3CD:
        VGA_LOG("Unknown write to %x: %02x\n", port, data);
        break;
    case 0x3C4: // Sequencer Index
        vga.seq_index = data & 7;
        break;
    case 0x3C5: { // Sequencer Data
        const uint8_t mask[8] = {
            // which bits are reserved
            MASK(0b00000000), // 0
            MASK(0b11000010), // 1
            MASK(0b11110000), // 2
            MASK(0b11000000), // 3
            MASK(0b11110001), // 4
            MASK(0b11111111), // 5
            MASK(0b11111111), // 6
            MASK(0b11111111) // 7
        };
        data &= mask[vga.seq_index];
        diffxor = vga.seq[vga.seq_index] ^ data;
        if (diffxor) {
            vga.seq[vga.seq_index] = data;
            switch (vga.seq_index) {
            case 0: // Sequencer Reset
                VGA_LOG("SEQ: Resetting sequencer\n");
                break;
            case 1: // Clocking Mode
                VGA_LOG("SEQ: Setting Clocking Mode to 0x%02x\n", data);
                if (diffxor & 0x20) // Screen Off
                    vga_change_renderer();
                if (diffxor & 0x08) { // Dot Clock Divide (AKA Fat Screen). Each column will be duplicated
                    vga_change_renderer();
                    vga_update_size();
                }
                if (diffxor & 0x01) { // 8/9 Dot Clocks
                    vga.char_width = 9 ^ (data & 1);
                    vga_update_size();
                    vga_complete_redraw();
                }
                break;
            case 2: // Memory Write Access
                VGA_LOG("SEQ: Memory plane write access: 0x%02x\n", data);
                break;
            case 3: // Character Map Select
                // Note these are font addresses in plane 2
                VGA_LOG("SEQ: Memory plane write access: 0x%02x\n", data);
                vga.character_map[0] = vga_char_map_address((data >> 5 & 1) | (data >> 1 & 6));
                vga.character_map[1] = vga_char_map_address((data >> 4 & 1) | (data << 1 & 6));
                break;
            case 4: // Memory Mode
                VGA_LOG("SEQ: Memory Mode: 0x%02x\n", data);
                if (diffxor & 0b1100)
                    vga_update_mem_access();
                break;
            }
        }
        break;
    case 0x3C6: // DAC Palette Mask
        // Used to play around with which colors can be accessed in the 256 DAC cache
        vga.dac_mask = data;
        vga_complete_redraw(); // Doing something as drastic as this deserves a redraw
        break;
    case 0x3C7: // DAC Read Address
        vga.dac_read_address = data;
        vga.dac_color = 0;
        break;
    case 0x3C8: // PEL Address Write Mode
        vga.dac_address = data;
        vga.dac_color = 0;
        break;
    case 0x3C9: // PEL Data Write
        vga.dac_state = 3;
        vga.dac[(vga.dac_address << 2) | vga.dac_color++] = data;
        if (vga.dac_color == 3) { // 0: red, 1: green, 2: blue, 3: ???
            update_one_dac_entry(vga.dac_address);
            vga.dac_address++; // This will wrap around because it is a uint8_t
            vga.dac_color = 0;
        }
        break;
    case 0x3CE: // Graphics Register Index
        vga.gfx_index = data & 15;
        break;
    case 0x3CF: { // Graphics Register Data
        const uint8_t mask[16] = {
            MASK(0b11110000), // 0
            MASK(0b11110000), // 1
            MASK(0b11110000), // 2
            MASK(0b11100000), // 3
            MASK(0b11111100), // 4
            MASK(0b10000100), // 5
            MASK(0b11110000), // 6
            MASK(0b11110000), // 7
            MASK(0b00000000), // 8
            MASK(0b11111111), // 9 - not documented
            MASK(0b00001000), // 10 - ???
            MASK(0b00000000), // 11 - ???
            MASK(0b11111111), // 12 - not documented
            MASK(0b11111111), // 13 - not documented
            MASK(0b11111111), // 14 - not documented
            MASK(0b11111111), // 15 - not documented
            //MASK(0b00000000), // 18 - scratch room vga
        };
        data &= mask[vga.gfx_index];
        diffxor = vga.gfx[vga.gfx_index] ^ data;
        if (diffxor) {
            vga.gfx[vga.gfx_index] = data;
            switch (vga.gfx_index) {
            case 0: // Set/Reset Plane
                VGA_LOG("Set/Reset Plane: %02x\n", data);
                break;
            case 1: // Enable Set/Reset Plane
                VGA_LOG("Enable Set/Reset Plane: %02x\n", data);
                break;
            case 2: // Color Comare
                VGA_LOG("Color Compare: %02x\n", data);
                break;
            case 3: // Data Rotate/ALU Operation Select
                VGA_LOG("Data Rotate: %02x\n", data);
                break;
            case 4: // Read Plane Select
                VGA_LOG("Read Plane Select: %02x\n", data);
                break;
            case 5: //  Graphics Mode
                VGA_LOG("Graphics Mode: %02x\n", data);
                if (diffxor & (3 << 5)) // Shift Register Control
                    vga_change_renderer();
                if (diffxor & ((1 << 3) | (1 << 4) | 3))
                    vga_update_mem_access();
                break;
            case 6: // Miscellaneous Register
                VGA_LOG("Miscellaneous Register: %02x\n", data);
                switch (data >> 2 & 3) {
                case 0:
                    vga.vram_window_base = 0xA0000;
                    vga.vram_window_size = 0x20000;
                    break;
                case 1:
                    vga.vram_window_base = 0xA0000;
                    vga.vram_window_size = 0x10000;
                    break;
                case 2:
                    vga.vram_window_base = 0xB0000;
                    vga.vram_window_size = 0x8000;
                    break;
                case 3:
                    vga.vram_window_base = 0xB8000;
                    vga.vram_window_size = 0x8000;
                    break;
                }
                if (diffxor & 1)
                    vga_change_renderer();
                break;
            case 7:
                VGA_LOG("Color Don't Care: %02x\n", data);
                break;
            case 8:
                VGA_LOG("Bit Mask Register: %02x\n", data);
                break;
            }
        }
        break;
    }
    case 0x3D4:
    case 0x3B4: // CRT index
        vga.crt_index = data/* & 0x3F*/;
        break;
    case 0x3D5:
    case 0x3B5: { // CRT data
        static uint8_t mask[64] = {
            // 0-7 are changed based on CR11 bit 7
            MASK(0b00000000), // 0
            MASK(0b00000000), // 1
            MASK(0b00000000), // 2
            MASK(0b00000000), // 3
            MASK(0b00000000), // 4
            MASK(0b00000000), // 5
            MASK(0b00000000), // 6
            MASK(0b00000000), // 7
            MASK(0b10000000), // 8
            MASK(0b00000000), // 9
            MASK(0b11000000), // A
            MASK(0b10000000), // B
            MASK(0b00000000), // C
            MASK(0b00000000), // D
            MASK(0b00000000), // E
            MASK(0b00000000), // F
            MASK(0b00000000), // 10
            MASK(0b00110000), // 11
            MASK(0b00000000), // 12
            MASK(0b00000000), // 13
            MASK(0b10000000), // 14
            MASK(0b00000000), // 15
            MASK(0b10000000), // 16
            MASK(0b00010000), // 17
            MASK(0b00000000) // 18
        };
        // Don't allow ourselves to go out of bounds
        if(vga.crt_index > 0x3F) break;
        // The extra difficulty here comes from the fact that the mask is used here to allow masking of CR0-7 in addition to keeping out undefined bits
        data &= mask[vga.crt_index];
        // consider the case when we write 0x33 to CR01 (which is currently 0x66) and write protection is own
        // In this case, we would be doing (0x33 & 0) ^ 0x66 which would result in 0x66 being put in diffxor
        // However, if we masked the result, the following would occur: ((0x33 & 0) ^ 0x66) & 0 = 0
        diffxor = (data ^ vga.crt[vga.crt_index]) & mask[vga.crt_index];
        if (diffxor) {
            vga.crt[vga.crt_index] = data | (vga.crt[vga.crt_index] & ~mask[vga.crt_index]);
            switch (vga.crt_index) {
            case 1:
                VGA_LOG("End Horizontal Display: %02x\n", data);
                vga_update_size();
                break;
            case 2:
                VGA_LOG("Start Horizontal Blanking: %02x\n", data);
                vga_update_size();
                break;
            case 7:
                VGA_LOG("CRT Overflow: %02x\n", data);
                vga_update_size();
                break;
            case 9:
                VGA_LOG("Start Horizontal Blanking: %02x\n", data);
                if (diffxor & 0x20)
                    vga_update_size();
                break;
            case 0x11:
                if (diffxor & 0x80) {
                    uint8_t fill_value = (int8_t)(vga.crt[0x11] ^ 0x80) >> 7;
                    //printf("%d: %d [%02x]\n", fill_value, vga.crt_index, data);
                    for (int i = 0; i < 8; i++)
                        mask[i] = fill_value;
                    mask[7] &= ~0x10;
                    data &= mask[vga.crt_index];
                }
                break;
            case 0x12:
                VGA_LOG("Vertical Display End: %02x\n", data);
                vga_update_size();
                break;
            case 0x15:
                VGA_LOG("Start Vertical Blanking: %02x\n", data);
                vga_update_size();
                break;
            }
        }
        break;
    }
    }
    default:
        VGA_LOG("VGA write: 0x%x [data: 0x%02x]\n", port, data);
    }
}

#ifndef VGA_LIBRARY
static
#endif
    uint32_t
    vga_read(uint32_t port)
{
    if ((port >= 0x3B0 && port <= 0x3BF && (vga.misc & 1)) || (port >= 0x3D0 && port <= 0x3DF && !(vga.misc & 1))) {
        return -1;
    }
    switch (port) {
    case 0x1CE:
        return vga.vbe_index;
    case 0x1CF:
        switch (vga.vbe_index) {
        case 0:
            return vga.vbe_version;
        case 1 ... 3: // xres, yres, bpp
            if (vga.vbe_enable & VBE_DISPI_GETCAPS)
                return vbe_maximums[vga.vbe_index - 1];
            else
                return vga.vbe_regs[vga.vbe_index]; // vga.vbe_index - 1 not required at this location
            break;
        case 4:
            return vga.vbe_enable & (VBE_DISPI_ENABLED | VBE_DISPI_GETCAPS | VBE_DISPI_8BIT_DAC);
        case 5:
            return vga.vbe_regs[5] >> 16;
        case 6:
            return vga.vbe_regs[6];
        case 7:
            return vga.vbe_regs[7];
        case 8 ... 9:
            return vga.vbe_regs[vga.vbe_index];
        case 10: // Get VBE RAM size in 64 KB banks
            return vga.vram_size >> 16;
        default:
            VGA_FATAL("VBE read: %d\n", vga.vbe_index);
        }
        break;
    case 0x3C0:
        return vga.attr_index;
    case 0x3C1:
        return vga.attr[vga.attr_index & 0x1F];
    case 0x3C2:
        return vga.misc;
    case 0x3C4:
        return vga.seq_index;
    case 0x3C5:
        return vga.seq[vga.seq_index];
    case 0x3C6:
        return vga.dac_mask;
    case 0x3C7:
        return vga.dac_state;
    case 0x3C8:
        return vga.dac_address;
    case 0x3C9: {
        vga.dac_state = 0;
        uint8_t data = vga.dac[(vga.dac_read_address << 2) | (vga.dac_color++)];
        if (vga.dac_color == 3) {
            vga.dac_read_address++;
            vga.dac_color = 0;
        }
        return data;
    }
    case 0x3CC:
        return vga.misc;
    case 0x3CE:
        return vga.gfx_index;
    case 0x3CF:
        return vga.gfx[vga.gfx_index];
    case 0x3B8:
    case 0x3D8:
    case 0x3CD:
        return -1;
    case 0x3BA:
    case 0x3DA: // Input status Register #1
        // Some programs poll this register to make sure that graphics registers are only being modified during vertical retrace periods
        // Not many programs require this feature to work. For now, we can fake this effect.
        vga.status[1] ^= 9;
        vga.attr_index &= ~0x80; // Also clears attr flip flop
        return vga.status[1];
    case 0x3B5:
    case 0x3D5:
        return vga.crt[vga.crt_index];
    default:
        VGA_LOG("Unknown read: 0x%x\n", port);
        return -1;
    }
}

static inline uint8_t bpp4_to_offset(uint8_t i, uint8_t j, uint8_t k)
{
    return ((i & (0x80 >> j)) != 0) ? 1 << k : 0;
}

static int framectr = 0;
void vga_update(void)
{
    // Note: This function should NOT modify any VGA registers or memory!

    framectr = (framectr + 1) & 0x3F;
    int scanlines_to_update = vga.scanlines_to_update; // XXX

    // Text Mode state
    unsigned int cursor_scanline_start = 0, cursor_scanline_end = 0, cursor_enabled = 0, cursor_address = 0,
                 underline_location = 0, line_graphics = 0;
    // 4BPP renderer
    unsigned int enableMask = 0, address_bit_mapping = 0;

    // All non-VBE renderers
    unsigned int offset_between_lines = (((!vga.crt[0x13]) << 8 | vga.crt[0x13]) * 2) << 2;
    switch (vga.renderer & ~1) {
    case BLANK_RENDERER:
        break;
    case ALPHANUMERIC_RENDERER:
        cursor_scanline_start = vga.crt[0x0A] & 0x1F;
        cursor_scanline_end = vga.crt[0x0B] & 0x1F;
        cursor_enabled = (vga.crt[0x0B] & 0x20) || (framectr >= 0x20);
        cursor_address = (vga.crt[0x0E] << 8 | vga.crt[0x0F]) << 2;
        underline_location = vga.crt[0x14] & 0x1F;
        line_graphics = vga.char_width == 9 ? ((vga.attr[0x10] & 4) ? 0xE0 : 0) : 0;
        break;
    case RENDER_4BPP:
        enableMask = vga.attr[0x12] & 15;
        address_bit_mapping = vga.crt[0x17] & 1;
        break;
    case RENDER_16BPP: // VBE 16-bit BPP mode
        offset_between_lines = vga.total_width * 2;
        break;
    case RENDER_24BPP: // VBE 24-bit BPP mode
        offset_between_lines = vga.total_width * 3;
        break;
    case RENDER_32BPP: // VBE 32-bit BPP mode
        offset_between_lines = vga.total_width * 4;
        break;
    }
    if (!vga.memory_modified)
        return;
    vga.memory_modified &= ~(1 << (vga.current_scanline != 0));

#ifdef ALLEGRO_BUILD
    vga.framebuffer = display_get_pixels();
#endif

    uint32_t
        //current = vga.current_scanline,
        total_scanlines_drawn
        = 0;
    while (scanlines_to_update--) {
        total_scanlines_drawn++;
        // Things to account for here
        //  - Doubling Scanlines
        //  - Character Scanlines
        //  - Line Compare (aka split screen)
        //  - Incrementing & Wrapping Around Scanlines
        //  - Drawing the scanline itself

        // First things first, doubling scanlines
        // On a screen without doubling, the scanlines would look like this:
        //  0: QWERTYUIOPQWERTYUIOPQWERTYUIOP
        //  1: ASDFGHJKLASDFGHJKLASDFGHJKLASD
        //  2: ZXCVBNMZXCVBNMZXCVBNMZXCVBNMZX
        //  3: ...
        // with scanline doubling, however, it looks like this:
        //  0: QWERTYUIOPQWERTYUIOPQWERTYUIOP
        //  1: QWERTYUIOPQWERTYUIOPQWERTYUIOP <-- dupe
        //  2: ASDFGHJKLASDFGHJKLASDFGHJKLASD
        //  3: ASDFGHJKLASDFGHJKLASDFGHJKLASD <-- dupe
        //  4: ZXCVBNMZXCVBNMZXCVBNMZXCVBNMZX
        //  5: ZXCVBNMZXCVBNMZXCVBNMZXCVBNMZX <-- dupe
        //  6: ...
        //  7: (same as #6)
        // Therefore, we can come to the conclusion that if scanline doubling is enabled, then all odd scanlines are simply copies of the one preceding them
        if ((vga.current_scanline & 1) && (vga.crt[9] & 0x80)) {
            // See above for
            memcpy(&vga.framebuffer[vga.framebuffer_offset], &vga.framebuffer[vga.framebuffer_offset - vga.total_width], vga.total_width);
        } else {
            if (vga.current_scanline < vga.total_height) {
                uint32_t fboffset = vga.framebuffer_offset;
                uint32_t vram_addr = vga.vram_addr;
                switch (vga.renderer) {
                case BLANK_RENDERER:
                case BLANK_RENDERER | 1:
                    for (unsigned int i = 0; i < vga.total_width; i++) {
                        vga.framebuffer[fboffset + i] = 255 << 24;
                    }
                    break;
                case ALPHANUMERIC_RENDERER: {
                    // Text Mode Memory Layout (physical)
                    // Plane 0: CC XX CC XX
                    // Plane 1: AA XX AA XX
                    // Plane 2: FF XX FF XX
                    // Plane 3: XX XX XX XX
                    // In a row: CC AA FF XX XX XX XX XX CC AA FF XX XX XX XX XX
                    for (unsigned int i = 0; i < vga.total_width; i += vga.char_width, vram_addr += 4) {
                        uint8_t character = vga.vram[vram_addr << 1];
                        uint8_t attribute = vga.vram[(vram_addr << 1) + 1];
                        uint8_t font = vga.vram[( //
                                                    ( //
                                                        vga.character_scanline // Current character scanline
                                                        + character * 32 // Each character holds 32 bytes of font data in plane 2
                                                        + vga.character_map[~attribute >> 3 & 1]) // Offset in plane to, decided by attribute byte
                                                    << 2)
                            + 2 // Select Plane 2
                        ];
                        // Determine Color
                        uint32_t fg = attribute & 15, bg = attribute >> 4 & 15;

                        // Now we can begin to apply special character effects like:
                        //  - Cursor
                        //  - Blinking
                        //  - Underline
                        if (cursor_enabled && vram_addr == cursor_address) {
                            if ((vga.character_scanline >= cursor_scanline_start) && (vga.character_scanline <= cursor_scanline_end)) {
                                // cursor is enabled
                                bg = fg;
                            }
                        }

                        // TODO: I've noticed that blinking is twice as slow as cursor blinks
                        if ((vga.attr[0x10] & 8) && (framectr >= 32)) {
                            bg &= 7; // last bit is not interpreted
                            if (attribute & 0x80)
                                fg = bg;
                        }
                        // Underline is simple
                        if ((attribute & 0b01110111) == 1) {
                            if (vga.character_scanline == underline_location)
                                bg = fg;
                        }

                        // To draw the character quickly, use a method similar to do_mask
                        fg = vga.dac_palette[vga.dac_mask & vga.attr_palette[fg]];
                        bg = vga.dac_palette[vga.dac_mask & vga.attr_palette[bg]];
                        uint32_t xorvec = fg ^ bg;
                        // The following is equivalent to the following:
                        //  if(font & bit) vga.framebuffer[fboffset] = fg; else vga.framebuffer[fboffset] = bg;
                        vga.framebuffer[fboffset + 0] = ((xorvec & -(font >> 7))) ^ bg;
                        vga.framebuffer[fboffset + 1] = ((xorvec & -(font >> 6 & 1))) ^ bg;
                        vga.framebuffer[fboffset + 2] = ((xorvec & -(font >> 5 & 1))) ^ bg;
                        vga.framebuffer[fboffset + 3] = ((xorvec & -(font >> 4 & 1))) ^ bg;
                        vga.framebuffer[fboffset + 4] = ((xorvec & -(font >> 3 & 1))) ^ bg;
                        vga.framebuffer[fboffset + 5] = ((xorvec & -(font >> 2 & 1))) ^ bg;
                        vga.framebuffer[fboffset + 6] = ((xorvec & -(font >> 1 & 1))) ^ bg;
                        vga.framebuffer[fboffset + 7] = ((xorvec & -(font >> 0 & 1))) ^ bg;

                        if ((character & line_graphics) == 0xC0) {
                            vga.framebuffer[fboffset + 8] = ((xorvec & -(font >> 0 & 1))) ^ bg;
                        } else if (vga.char_width == 9)
                            vga.framebuffer[fboffset + 8] = bg;
                        fboffset += vga.char_width;
                    }
                    break;
                }
                case MODE_13H_RENDERER: {
                    //if(!vga.vbe_scanlines_modified[vga.current_scanline]) break;
                    // CHAIN4 Memory Layout:
                    //  Plane 0: AA 00 00 00 AA 00 00 00
                    //  Plane 1: BB 00 00 00 BB 00 00 00
                    //  Plane 2: CC 00 00 00 CC 00 00 00
                    //  Plane 3: DD 00 00 00 DD 00 00 00
                    // Draw four clumps of pixels together
                    // XXX: What if screen isn't a multiple of four pixels wide?
                    for (unsigned int i = 0; i < vga.total_width; i += 4, vram_addr += 16) {
                        for (int j = 0; j < 4; j++) { // hopefully, compiler unrolls loop
                            vga.framebuffer[fboffset + j] = vga.dac_palette[vga.vram[vram_addr | j] & vga.dac_mask];
                        }
                        fboffset += 4;
                    }
                    //vga.vbe_scanlines_modified[vga.current_scanline] = 0;
                    break;
                }
                case MODE_13H_RENDERER | 1:
                    //if(!vga.vbe_scanlines_modified[vga.current_scanline]) break;
                    for (unsigned int i = 0; i < vga.total_width; i += 8, vram_addr += 4) {
                        for (int j = 0, k = 0; j < 4; j++, k += 2) {
                            vga.framebuffer[fboffset + k] = vga.framebuffer[fboffset + k + 1] = vga.dac_palette[vga.vram[vram_addr | j] & vga.dac_mask];
                        }
                        fboffset += 8;
                    }
                    //vga.vbe_scanlines_modified[vga.current_scanline] = 0;
                    break;
                case RENDER_4BPP: {
                    //if(!vga.vbe_scanlines_modified[vga.current_scanline]) break;
                    uint32_t addr = vram_addr;
                    if (vga.character_scanline & address_bit_mapping)
                        addr |= 0x8000;
                    uint8_t p0 = vga.vram[addr | 0];
                    uint8_t p1 = vga.vram[addr | 1];
                    uint8_t p2 = vga.vram[addr | 2];
                    uint8_t p3 = vga.vram[addr | 3];

                    for (unsigned int x = 0, px = vga.current_pixel_panning; x < vga.total_width; x++, fboffset++, px++) {
                        if (px > 7) {
                            px = 0;
                            addr += 4;
                            p0 = vga.vram[addr | 0];
                            p1 = vga.vram[addr | 1];
                            p2 = vga.vram[addr | 2];
                            p3 = vga.vram[addr | 3];
                        }
                        int pixel = bpp4_to_offset(p0, px, 0) | bpp4_to_offset(p1, px, 1) | bpp4_to_offset(p2, px, 2) | bpp4_to_offset(p3, px, 3);
                        pixel &= enableMask;
                        vga.framebuffer[fboffset] = vga.dac_palette[vga.dac_mask & vga.attr_palette[pixel]];
                    }
                    //vga.vbe_scanlines_modified[vga.current_scanline] = 0;
                    break;
                }
                case RENDER_4BPP | 1: {
                    // 4BPP rendering mode, but lower resolution
                    //if(!vga.vbe_scanlines_modified[vga.current_scanline]) break;
                    uint32_t addr = vram_addr;
                    uint8_t p0 = vga.vram[addr | 0];
                    uint8_t p1 = vga.vram[addr | 1];
                    uint8_t p2 = vga.vram[addr | 2];
                    uint8_t p3 = vga.vram[addr | 3];
                    for (unsigned int x = 0, px = vga.current_pixel_panning; x < vga.total_width; x += 2, fboffset += 2, px++) {
                        if (px > 7) {
                            px = 0;
                            addr += 4;
                            p0 = vga.vram[addr | 0];
                            p1 = vga.vram[addr | 1];
                            p2 = vga.vram[addr | 2];
                            p3 = vga.vram[addr | 3];
                        }
                        int pixel = bpp4_to_offset(p0, px, 0) | bpp4_to_offset(p1, px, 1) | bpp4_to_offset(p2, px, 2) | bpp4_to_offset(p3, px, 3);
                        pixel &= enableMask;
                        uint32_t result = vga.dac_palette[vga.dac_mask & vga.attr_palette[pixel]];
                        vga.framebuffer[fboffset] = result;
                        vga.framebuffer[fboffset + 1] = result;
                    }
                    //vga.vbe_scanlines_modified[vga.current_scanline] = 0;
                    break;
                }
                case RENDER_32BPP:
                    if (!vga.vbe_scanlines_modified[vga.current_scanline])
                        break;
                    for (unsigned int i = 0; i < vga.total_width; i++, vram_addr += 4) {
#ifndef EMSCRIPTEN
                        vga.framebuffer[fboffset++] = *((uint32_t*)&vga.vram[vram_addr]) | 0xFF000000;
#else
                        uint32_t num = *((uint32_t*)&vga.vram[vram_addr]);
                        // Byte-swap framebuffer for easy ImageData blitting
                        vga.framebuffer[fboffset++] = (num >> 16 & 0xFF) | (num << 16 & 0xFF0000) | (num & 0xFF00) | 0xFF000000;
#endif
                    }
                    vga.vbe_scanlines_modified[vga.current_scanline] = 0;
                    break;
                case RENDER_8BPP:
                    if (!vga.vbe_scanlines_modified[vga.current_scanline])
                        break;
                    for (unsigned int i = 0; i < vga.total_width; i++, vram_addr++)
                        vga.framebuffer[fboffset++] = vga.dac_palette[vga.vram[vram_addr]];

                    vga.vbe_scanlines_modified[vga.current_scanline] = 0;
                    break;
                case RENDER_16BPP:
                    if (!vga.vbe_scanlines_modified[vga.current_scanline])
                        break;
                    for (unsigned int i = 0; i < vga.total_width; i++, vram_addr += 2) {
                        uint16_t word = *((uint16_t*)&vga.vram[vram_addr]);
                        int red = word >> 11 << 3,
                            green = (word >> 5 & 63) << 2, // Note: 6 bits for green
                            blue = (word & 31) << 3;
#ifndef EMSCRIPTEN
                        vga.framebuffer[fboffset++] = red << 16 | green << 8 | blue << 0 | 0xFF000000;
#else
                        vga.framebuffer[fboffset++] = red << 0 | green << 8 | blue << 16 | 0xFF000000;
#endif
                    }

                    vga.vbe_scanlines_modified[vga.current_scanline] = 0;
                    break;
                case RENDER_24BPP:
                    if (!vga.vbe_scanlines_modified[vga.current_scanline])
                        break;
                    for (unsigned int i = 0; i < vga.total_width; i++, vram_addr += 3) {
                        uint8_t blue = vga.vram[vram_addr],
                                green = vga.vram[vram_addr + 1],
                                red = vga.vram[vram_addr + 2];
#ifndef EMSCRIPTEN
                        vga.framebuffer[fboffset++] = (blue) | (green << 8) | (red << 16) | 0xFF000000;
#else
                        vga.framebuffer[fboffset++] = (blue << 16) | (green << 8) | (red) | 0xFF000000;
#endif
                    }
                    vga.vbe_scanlines_modified[vga.current_scanline] = 0;
                    break;
                }
                if ((vga.crt[9] & 0x1F) == vga.character_scanline) {
                    vga.character_scanline = 0;
                    vga.vram_addr += offset_between_lines; // TODO: Dword Mode
                } else
                    vga.character_scanline++;
            }
        }
        vga.current_scanline = (vga.current_scanline + 1) & 0x0FFF; // Increment current scan line
        vga.framebuffer_offset += vga.total_width;
        if (vga.current_scanline >= vga.total_height) {
            // Technically, we should draw output to the value specified by the CRT Vertical Total Register, but why bother?

            // Update the display when all the scanlines have been drawn
            //display_update(current, total_scanlines_drawn);
            display_update(0, vga.total_height);

            vga_complete_redraw(); // contrary to its name, it only resets drawing state
            //current = 0;

            total_scanlines_drawn = 0;

            // also, one frame has been completely drawn
            //framectr = (framectr + 1) & 0x3F;
        }
    }
}

static void vga_reset(void)
{
    // No need to reset registers that are "undefined" during bootup; the VGA BIOS will set them anyways
    vga.misc = 0;
    vga.seq_index = 0;
    vga.char_width = 9; // default size of SR01 bit 0 is 0
    vga.character_map[0] = vga.character_map[1] = 0;
    vga_complete_redraw();
}

static void expand32_alt(uint8_t* ptr, int v4)
{
    ptr[0] = v4 & 1 ? 0xFF : 0;
    ptr[1] = v4 & 2 ? 0xFF : 0;
    ptr[2] = v4 & 4 ? 0xFF : 0;
    ptr[3] = v4 & 8 ? 0xFF : 0;
}

static uint32_t expand32(int v4)
{
    uint32_t r = v4 & 1 ? 0xFF : 0;
    r |= v4 & 2 ? 0xFF00 : 0;
    r |= v4 & 4 ? 0xFF0000 : 0;
    r |= v4 & 8 ? 0xFF000000 : 0;
    return r;
}
static uint32_t b8to32(uint8_t x)
{
    uint32_t y = x;
    y |= y << 8;
    return y | (y << 16);
}

#ifndef VGA_LIBRARY
static
#endif
    uint32_t
    vga_mem_readb(uint32_t addr)
{
    if (vga.vbe_enable & VBE_DISPI_ENABLED) {
        //__asm__("int3");
        if (addr & 0x80000000) // read from LFB
            return vga.vram[addr - VBE_LFB_BASE];
        else // banked read
            return vga.vram[vga.vbe_regs[5] + (addr & 0x1FFFF)];
    }

    addr -= vga.vram_window_base;
    if (addr > vga.vram_window_size) { // Note: will catch the case where addr < vram_window_base as well
        //VGA_LOG("Out Of Bounds VRAM read: %08x [original: %08x]\n", addr, addr + vga.vram_window_base);
        return -1;
    }
    // Fill Latches with data from all 4 planes
    // TODO: endianness
    vga.latch32 = ((uint32_t*)(vga.vram))[addr];

    int plane = 0, plane_addr = -1;
    uint8_t color_dont_care[4], color_compare[4];
    switch (vga.read_access) {
    case CHAIN4:
        plane = addr & 3;
        plane_addr = addr >> 2;
        break;
    case ODDEVEN:
        plane = (addr & 1) | (vga.gfx[4] & 2);
        plane_addr = addr & ~1;
        break;
    case NORMAL:
        plane = vga.gfx[4] & 3;
        plane_addr = addr;
        break;
    case READMODE_1:
        expand32_alt(color_dont_care, vga.gfx[7]);
        expand32_alt(color_compare, vga.gfx[2]);
        return ~( //
            ((vga.latch8[0] & color_dont_care[0]) ^ color_compare[0]) | //
            ((vga.latch8[1] & color_dont_care[1]) ^ color_compare[1]) | //
            ((vga.latch8[2] & color_dont_care[2]) ^ color_compare[2]) | //
            ((vga.latch8[3] & color_dont_care[3]) ^ color_compare[3]));
    }
    if (plane_addr > 65536)
        VGA_FATAL("Reading outside plane bounds\n");
    uint8_t data = vga.vram[plane | (plane_addr << 2)];
#if 0
    VGA_LOG("Reading 0x%02x from vram=0x%08x [plane=%d, offset=%x]\n", data, addr, plane, plane_addr);
#endif
    return data;
}

static uint8_t alu_rotate(uint8_t value)
{
    uint8_t rotate_count = vga.gfx[3] & 7;
    return ((value >> rotate_count) | (value << (8 - rotate_count))) & 0xFF;
}

#define DO_MASK(n) xor ^= mask_enabled& n ? value& lut32[n] : mask& lut32[n]
// If a bit in "mask_enabled" is set, then replace value with the data in "mask," otherwise keep the same
// Example: value=0x12345678 mask=0x9ABCDEF0 mask_enabled=0b1010 result=0x9A34DE78
static inline uint32_t do_mask(uint32_t value, uint32_t mask, int mask_enabled)
{
    static uint32_t lut32[9] = { 0, 0xFF, 0xFF00, 0, 0xFF0000, 0, 0, 0, 0xFF000000 };
    // Uses XOR for fast bit replacement
    uint32_t xor = value ^ mask;
    DO_MASK(1);
    DO_MASK(2);
    DO_MASK(4);
    DO_MASK(8);
    return xor;
}

#ifndef VGA_LIBRARY
static
#endif
    void
    vga_mem_writeb(uint32_t addr, uint32_t data)
{
    if (vga.vbe_enable & VBE_DISPI_ENABLED) {
        // The following four scenarios can occur:
        //  1. Write is >= VBE_LFB_BASE and LFB is enabled
        //  2. Write is >= VBE_LFB_BASE and banked mode is enabled
        //  3. Write is 0xA0000 <= addr <= 0xBFFFF and LFB is enabled
        //  4. Write is 0xA0000 <= addr <= 0xBFFFF and banked mode is enabled
        uint32_t vram_offset;
        if (addr & 0x80000000) {
            vram_offset = addr - VBE_LFB_BASE;
            if (vga.vbe_enable & VBE_DISPI_LFB_ENABLED)
                vga.vram[vram_offset] = data;
            else
                return;
        } else {
            vram_offset = vga.vbe_regs[5] + (addr & 0x1FFFF);
            if (vga.vbe_enable & VBE_DISPI_LFB_ENABLED)
                return;
            else
                vga.vram[vram_offset] = data;
        }
        // Determine the scanline that was modified
        uint32_t scanline = vram_offset / (vga.total_width * ((vga.vbe_regs[3] + 7) >> 3));
        if (scanline < vga.total_height)
            vga.vbe_scanlines_modified[scanline] = 1;
        vga.memory_modified = 1;
        return;
    }

    addr -= vga.vram_window_base;
    if (addr > vga.vram_window_size) { // Note: will catch the case where addr < vram_window_base as well
        //VGA_LOG("Out Of Bounds VRAM write: addr=%08x data=%02x\n", addr, data);
        return;
    }
    int plane = 0, plane_addr = -1;
    switch (vga.write_access) {
    case CHAIN4:
        plane = 1 << (addr & 3);
        plane_addr = addr >> 2;
        break;
    case ODDEVEN:
        plane = 5 << (addr & 1);
        plane_addr = addr & ~1;
        break;
    case NORMAL:
        plane = 15; // This will be masked out by SR02 later
        plane_addr = addr;
        break;
    }
    uint32_t data32 = data, and_value = 0xFFFFFFFF; // this will be expanded to 32 bits later, but for now keep it as 8 bits to keep things simple
    int run_alu = 1;
    switch (vga.write_mode) {
    case 0:
        data32 = b8to32(alu_rotate(data));
        data32 = do_mask(data32, expand32(vga.gfx[0]), vga.gfx[1]);
        break;
    case 1:
        data32 = vga.latch32; // TODO: endianness
        run_alu = 0;
        break;
    case 2:
        data32 = expand32(data);
        break;
    case 3:
        and_value = b8to32(alu_rotate(data));
        data32 = expand32(vga.gfx[0]);
        break;
    }
    if (run_alu) {
        uint32_t mask = b8to32(vga.gfx[8]) & and_value;
        switch (vga.gfx[3] & 0x18) {
        case 0: // MOV (Unmodified)
            data32 = (data32 & mask) | (vga.latch32 & ~mask);
            break;

        // TODO: Simplify the rest using Boolean Algebra
        case 0x08: // AND
            // ABC + B~C
            // B(AC + ~C)
            // (A + ~C)B
            data32 = ((data32 & vga.latch32) & mask) | (vga.latch32 & ~mask);
            break;
        case 0x10: // OR
            data32 = ((data32 | vga.latch32) & mask) | (vga.latch32 & ~mask);
            break;
        case 0x18: // XOR
            data32 = ((data32 ^ vga.latch32) & mask) | (vga.latch32 & ~mask);
            break;
        }
    }
    if (plane_addr > 65536)
        VGA_FATAL("Writing outside plane bounds\n");

    // Actually write to memory
    plane &= vga.seq[2];
    uint32_t* vram_ptr = (uint32_t*)&vga.vram[plane_addr << 2];
    *vram_ptr = do_mask(*vram_ptr, data32, plane);

    // Update scanline
    uint32_t offs = (plane_addr << 2) - (((vga.crt[0x0C] << 8) | vga.crt[0x0D]) << 2),
             offset_between_lines = (((!vga.crt[0x13]) << 8 | vga.crt[0x13]) * 2) << 2;

    unsigned int scanline = offs / offset_between_lines;
    if (vga.total_height > scanline) {
        switch (vga.renderer >> 1) {
        case MODE_13H_RENDERER >> 1:
            // Determine the scanline that it has been written to.
            vga.vbe_scanlines_modified[scanline] = 1;
            break;
        case RENDER_4BPP >> 1:
            // todo: what about bit13 replacement?
            vga.vbe_scanlines_modified[scanline] = 1;
            break;
        }
    }

    vga.memory_modified = 3;

#if 0
    VGA_LOG("Writing %02x to vram=0x%08x, phys=%08x [%c%c%c%c, offset: 0x%x] d32: %08x vram: %08x latch: %08x wmode: %d\n", data, addr, vga.vram_window_base + addr,
        plane & 1 ? '0' : ' ',
        plane & 2 ? '1' : ' ',
        plane & 4 ? '2' : ' ',
        plane & 8 ? '3' : ' ',
        plane_addr,
        data32, *vram_ptr, vga.latch32, vga.write_mode);
#endif
}

static const uint8_t pci_config_space[16] = { 0x34, 0x12, 0x11, 0x11, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0 };
static int vga_pci_write(uint8_t* ptr, uint8_t addr, uint8_t data)
{
    switch (addr) {
    case 0x10:
        // Do what Bochs does
        ptr[addr] = (ptr[addr] & 0x0F) | (data & 0xF0);
        return 0;
    case 0x13: // VBE base pointers
        break;
    case 0x33: { // ROM address
        uint32_t new_mmio = ptr[0x30] | (ptr[0x31] << 8) | (ptr[0x32] << 16) | (data << 24);
        new_mmio &= ~1; // TODO: What if the bit is clear (disabling ROM access)?
        // XXX hack

        uint32_t data;
        if (new_mmio == 0xFFFFFFFE) {
            // Load the new pointer with our rom size
            data = -vga.rom_size;
        } else {
            data = 0xFEB00000;
        }
        ptr[0x30] = data;
        ptr[0x31] = data >> 8;
        ptr[0x32] = data >> 16;
        ptr[0x33] = data >> 24;

        // XXX: Don't do this here
        vga.mem = cpu_get_ram_ptr();

        io_remap_mmio_read(vga.vgabios_addr, new_mmio);
        vga.vgabios_addr = new_mmio;
        VGA_LOG("Remapping VGA ROM to: %08x\n", new_mmio);
        break;
    }
    }
    return 0;
}
static uint32_t vga_rom_readb(uint32_t addr)
{
    //printf("%08x --> %08x\n", addr, addr - vga.vgabios_addr + 0xC0000);
    return vga.rom[(addr - vga.vgabios_addr) & 0xFFFF];
}
static void vga_rom_writeb(uint32_t addr, uint32_t data)
{
    UNUSED(addr | data);
}
static void vga_pci_init(struct loaded_file* vgabios)
{
    // Dummy PCI VGA controller.
    uint8_t* dev = pci_create_device(0, 2, 0, vga_pci_write);
    pci_copy_default_configuration(dev, (void*)pci_config_space, 16);
    dev[0x10] = 8; // VBE enabled
    io_register_mmio_read(vga.vgabios_addr = 0xFEB00000, 0x20000, vga_rom_readb, NULL, NULL);
    io_register_mmio_write(vga.vgabios_addr, 0x20000, vga_rom_writeb, NULL, NULL);

    vga.rom = calloc(1, 65536);
    memcpy(vga.rom, vgabios->data, vgabios->length & 65535);
    vga.rom_size = vgabios->length;

    dev[0x30] = vga.vgabios_addr;
    dev[0x31] = vga.vgabios_addr >> 8;
    dev[0x32] = vga.vgabios_addr >> 16;
    dev[0x33] = vga.vgabios_addr >> 24;
}

void vga_init(struct pc_settings* pc)
{
    io_register_reset(vga_reset);
    io_register_read(0x3B0, 48, vga_read, NULL, NULL);
    io_register_write(0x3B0, 48, vga_write, NULL, NULL);
    if (pc->vbe_enabled) {
        io_register_read(0x1CE, 2, NULL, vga_read, NULL);
        io_register_write(0x1CE, 2, NULL, vga_write, NULL);
    }

    state_register(vga_state);

    io_register_mmio_read(0xA0000, 0x20000 - 1, vga_mem_readb, NULL, NULL);
    io_register_mmio_write(0xA0000, 0x20000 - 1, vga_mem_writeb, NULL, NULL);

    int memory_size = pc->vga_memory_size < (256 << 10) ? 256 << 10 : pc->vga_memory_size;
    io_register_mmio_read(VBE_LFB_BASE, memory_size, vga_mem_readb, NULL, NULL);
    io_register_mmio_write(VBE_LFB_BASE, memory_size, vga_mem_writeb, NULL, NULL);

    vga.vram_size = memory_size;
    vga_alloc_mem();

    if (pc->pci_vga_enabled) {
        vga_pci_init(&pc->vgabios);
    }
}

void* vga_get_raw_vram(void)
{
    return vga.vram;
}