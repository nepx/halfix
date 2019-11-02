#include "devices.h"
#include "display.h"
#include "drive.h"
#include "pc.h"
#include "platform.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

// Halfix entry point

static struct pc_settings pc;

static int load_file(struct loaded_file* lf, char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    int l = ftell(f);
    fseek(f, 0, SEEK_SET);

    lf->length = l;
    lf->data = aalloc(l, 4096);
    if (fread(lf->data, l, 1, f) != 1)
        return -1;

    fclose(f);
    return 0;
}

struct cfg_option {
    const char* name;
    const int value;
};

enum {
    CFG_NONE = 0,
    CFG_MEMORY,
    CFG_CPU,
    CFG_BIOS,
    CFG_VBE,
    CFG_PCI_APIC,
    CFG_FLOPPY_ENABLED,

    CFG_FILE,
    CFG_MEDIA_TYPE,
    CFG_INSERTED,

    CFG_FD_FILE,
    CFG_FD_WRITE_PROTECTED,

    CFG_BOOT,
    CFG_BOOTORDER,
    CFG_BOOTDISK,
    CFG_BOOTCD,
    CFG_BOOTFLOPPY,
    CFG_BOOTNONE
};
static const struct cfg_option general_opts[] = {
    { "memory", CFG_MEMORY },
    { "vgamemory", CFG_MEMORY },
    { "cpu", CFG_CPU },
    { "bios", CFG_BIOS },
    { "vgabios", CFG_BIOS },
    { "vbe", CFG_VBE },
    { "pci", CFG_PCI_APIC },
    { "apic", CFG_PCI_APIC },
    { "floppy", CFG_FLOPPY_ENABLED },
    { NULL, CFG_NONE }
};
static const struct cfg_option drive_opts[] = {
    { "inserted", CFG_INSERTED },
    { "type", CFG_MEDIA_TYPE },
    { "file", CFG_FILE },
    { NULL, CFG_NONE }
};
static const struct cfg_option fd_opts[] = {
    { "file", CFG_FD_FILE },
    { "write_protected", CFG_FD_WRITE_PROTECTED },
    { NULL, CFG_NONE }
};
static const struct cfg_option boot_opts[] = {
    { "a", CFG_BOOTORDER },
    { "b", CFG_BOOTORDER },
    { "c", CFG_BOOTORDER },
    { NULL, CFG_NONE }
};
static const struct cfg_option boot_devices[] = {
    { "hd", CFG_BOOTDISK },
    { "cd", CFG_BOOTCD },
    { "fd", CFG_BOOTFLOPPY },
    { "none", CFG_BOOTNONE },
    { NULL, CFG_NONE }
};

static const struct cfg_option sections[] = {
    // Has the index into section_list array
    { "general", 0 },
    { "ata0-master", 1 },
    { "ata0-slave", 2 },
    { "ata1-master", 3 },
    { "ata1-slave", 4 },
    { "boot", 5 },
    { "fda", 6 },
    { "fdb", 7 },
    { NULL, CFG_NONE }
};
static const struct cfg_option* section_list[] = {
    general_opts,
    drive_opts,
    drive_opts,
    drive_opts,
    drive_opts,
    boot_opts,
    fd_opts,
    fd_opts
};

// Returns 1 if generic whitespace, 2 if newline, 0 otherwise
static inline int is_whitespace(char x)
{
    if (x <= 32) {
        if (x == '\n')
            return 2;
        else
            return 1;
    }
    return 0;
}

static int pos = 0, length = 0;
// Similar to strchr, but with some error checking and length information
static char* scanchr(char* y, char x, int* len, int newline_ok)
{
    int start = pos;
    while (pos < length) {
        if (!newline_ok && *y == '\n')
            break;
        if (*y == x) {
            *len = pos++ - start;
            return y + 1; // Move past char that we were looking for
        }
        y++;
        pos++;
    }
    *len = 0;
    return NULL;
}

static int parse_u32(char* y, uint32_t* dest)
{
    uint32_t res = 0;
    if (!(*y >= '0' && *y <= '9'))
        return -1;
    while (pos < length && *y >= '0' && *y <= '9') {
        res *= 10;
        res += *y - '0';
        y++;
        pos++;
    }
    *dest = res;
    return 0;
}

static int parse_string(char* y, int* len)
{
    int start = pos;
    while (pos < length && *y >= 32) {
        y++;
        pos++;
    }
    *len = pos - start;
    return 0;
}

// Parse configuration file.
int parse_cfg(char* buf)
{
    UNUSED(pc);
    UNUSED(load_file);
    UNUSED(sections);
    UNUSED(section_list);
    const struct cfg_option* current_section = general_opts;
    int id_index = 0; // General options
    length = strlen(buf);
top:
    while (pos < length) {
        // Start reading line.
        int chr = buf[pos++];
        if (is_whitespace(chr))
            continue;
        switch (chr) {
        case '#': // Comment
            while (pos < length && is_whitespace(buf[pos]) != 2)
                pos++;
            break;
        case '[': { // Section header
            int keylen, start = pos - 1;
            char* endbracket = scanchr(&buf[pos], ']', &keylen, 0);
            if (!endbracket) {
                fprintf(stderr, "Unexpected newline or EOF in section header\n");
                return -1;
            }
            int i = 0;
            for (;;) {
                if (!sections[i].name)
                    break;
                int temp_keylen = keylen, temp2;
                if ((temp2 = strlen(sections[i].name)) < temp_keylen)
                    temp_keylen = temp2;
                if (!memcmp(&buf[start + 1], sections[i].name, keylen)) { // Don't include the final ']'
                    current_section = section_list[id_index = sections[i].value];
                    goto top;
                }
                i++;
            }
            endbracket[-1] = 0;
            fprintf(stderr, "Unknown section '%s'\n", &buf[start + 1]);
            return -1;
        }
        default: {
            int keylen, start = pos - 1;
            char* equals = scanchr(&buf[pos], '=', &keylen, 0);
            if (!equals) {
                fprintf(stderr, "Unexpected newline or EOF in key=value pair\n");
                return -1;
            }
            int i = 0;
            for (;;) {
                if (!current_section[i].name)
                    break;
                int temp_keylen = keylen, temp2;
                if ((temp2 = strlen(current_section[i].name)) < temp_keylen)
                    temp_keylen = temp2;
                if (!memcmp(&buf[start], current_section[i].name, temp_keylen)) {
                    switch (current_section[i].value) {
                    case CFG_MEMORY: {
                        uint32_t memsz;
                        if (parse_u32(equals, &memsz) < 0) {
                            fprintf(stderr, "Expected number for 'memory'\n");
                            return -1;
                        }
                        // Right now, pos should be at the character following the digit, to any suffixes
                        if (pos < length)
                            switch (buf[pos]) {
                            case 'K':
                            case 'k':
                                memsz <<= 10;
                                pos++;
                                break;
                            case 'M':
                            case 'm':
                                memsz <<= 20;
                                pos++;
                                break;
                            case 'G': // Don't know why anyone would need this...
                            case 'g':
                                memsz <<= 30;
                                pos++;
                                break;
                            }
                        if (buf[start] == 'v') // VGA BIOS memory
                            pc.vga_memory_size = memsz;
                        else
                            pc.memory_size = memsz;
                        break;
                    }
                    case CFG_BIOS: {
                        char* path;
                        int pathlen;
                        if (parse_string(equals, &pathlen) < 0)
                            return -1;
                        path = alloca(pathlen + 1);
                        memcpy(path, equals, pathlen);
                        path[pathlen] = 0;
                        int retval;
                        if (buf[start] == 'v') // VGA BIOS memory
                            retval = load_file(&pc.vgabios, path);
                        else
                            retval = load_file(&pc.bios, path);
                        if (retval < 0) {
                            fprintf(stderr, "Unable to load BIOS images\n");
                            return -1;
                        }
                        break;
                    }
                    case CFG_PCI_APIC: {
                        uint32_t enabled;
                        if (parse_u32(equals, &enabled) < 0) {
                            fprintf(stderr, "Expected number for 'inserted'\n");
                            return -1;
                        }
                        if (buf[start] == 'p') // pci
                            pc.pci_enabled = enabled;
                        else
                            pc.apic_enabled = enabled;
                        break;
                    }
                    case CFG_FLOPPY_ENABLED: {
                        uint32_t enabled;
                        if (parse_u32(equals, &enabled) < 0) {
                            fprintf(stderr, "Expected number for 'inserted'\n");
                            return -1;
                        }
                        if (enabled)
                            printf("Warning: Floppy drive support is still in development. Some things may break\n");
                        pc.floppy_enabled = enabled;
                        break;
                    }

                    // Disk options
                    case CFG_INSERTED: {
                        uint32_t inserted;
                        if (parse_u32(equals, &inserted) < 0) {
                            fprintf(stderr, "Expected number for 'inserted'\n");
                            return -1;
                        }
                        if (!inserted)
                            pc.drives[id_index - 1].type = DRIVE_TYPE_NONE;
                        else
                            pc.drives[id_index - 1].type = -1; // Set it to an invalid non-zero number
                        break;
                    }
                    case CFG_FILE: {
                        char* path;
                        int pathlen;
                        if (parse_string(equals, &pathlen) < 0)
                            return -1;

                        // Don't do anything if we don't have a disk inserted here
                        if (!pc.drives[id_index - 1].type)
                            break;
                        path = alloca(pathlen + 1);
                        memcpy(path, equals, pathlen);
                        path[pathlen] = 0;
                        int retval = drive_init(&pc.drives[id_index - 1], path);
                        pc.drives[id_index - 1].type = DRIVE_TYPE_DISK;
                        if (retval < 0) {
                            fprintf(stderr, "Unable to load hard drive image\n");
                            return -1;
                        }
                        break;
                    }
                    case CFG_BOOTORDER: {
                        char* dev;
                        int devlen, j = 0, boot_dev = BOOT_NONE;
                        if (parse_string(equals, &devlen) < 0)
                            return -1;
                        dev = alloca(devlen + 1);
                        memcpy(dev, equals, devlen);
                        dev[devlen] = 0;
                        for (;;) {
                            if (!boot_devices[j].name)
                                break;
                            int temp_devlen = devlen, temp3;
                            if ((temp3 = strlen(boot_devices[j].name)) < devlen)
                                temp_devlen = temp3;
                            if (!memcmp(dev, boot_devices[j].name, temp_devlen)) {
                                switch (boot_devices[j].value) {
                                case CFG_BOOTDISK:
                                    boot_dev = BOOT_DISK;
                                    goto done;
                                case CFG_BOOTCD:
                                    boot_dev = BOOT_CDROM;
                                    goto done;
                                case CFG_BOOTFLOPPY:
                                    boot_dev = BOOT_FLOPPY;
                                    goto done;
                                case CFG_BOOTNONE:
                                    boot_dev = BOOT_NONE;
                                    goto done;
                                }
                            }
                            j++;
                        }
                        fprintf(stderr, "Invalid boot device\n");
                        return -1;
                    done:
                        pc.boot_sequence[buf[start] - 'a'] = boot_dev;
                        break;
                    }

                    case CFG_FD_FILE: {
                        char* path;
                        int pathlen;
                        if (parse_string(equals, &pathlen) < 0)
                            return -1;

                        if (!pc.floppy_enabled)
                            break;
                        path = alloca(pathlen + 1);
                        memcpy(path, equals, pathlen);
                        path[pathlen] = 0;
                        int retval = drive_init(&pc.floppy_drives[id_index - 6], path);
                        pc.floppy_drives[id_index - 6].type = DRIVE_TYPE_DISK;
                        if (retval < 0) {
                            fprintf(stderr, "Unable to load floppy drive image\n");
                            return -1;
                        }
                        break;
                    }
                    case CFG_FD_WRITE_PROTECTED: {
                        uint32_t wp;
                        if (parse_u32(equals, &wp) < 0) {
                            fprintf(stderr, "Expected number for 'inserted'\n");
                            return -1;
                        }
                        pc.floppy_settings[id_index - 6].write_protected = wp;
                        break;
                    }
                    } // switch()
                    goto top;
                } // if()
                i++;
            } // for()

            equals[-1] = 0; // Erase previous character
            fprintf(stderr, "Unknown option %s\n", &buf[start]);
            return -1;
        }
        }
    }
    return 0;
}

struct option {
    const char *name, *alias;
    int flags, id;
    const char* help;
};

#define HASARG 1

enum {
    OPTION_HELP,
    OPTION_CONFIG,
    OPTION_REALTIME
};

static const struct option options[] = {
    { "h", "help", 0, OPTION_HELP, "Show available options" },
    { "c", "config", HASARG, OPTION_CONFIG, "Use custom config file [arg]" },
    { "r", "realtime", 0, OPTION_REALTIME, "Try to sync internal emulator clock with wall clock" },
    { NULL, NULL, 0, 0, NULL }
};

static void generic_help(const struct option* options)
{
    int i = 0;
    printf("Halfix x86 PC Emulator\n");
    for (;;) {
        const struct option* o = options + i++;
        if (!o->name)
            return;

        char line[100];
        int linelength = sprintf(line, " -%s", o->name);
        if (o->alias)
            linelength += sprintf(line + linelength, " --%s", o->alias);

        if (o->flags & HASARG)
            linelength += sprintf(line + linelength, " [arg]");

        while (linelength < 40)
            line[linelength++] = ' ';
        line[linelength] = 0;
        printf("%s%s\n", line, o->help);
    }
}

int main(int argc, char** argv)
{
    UNUSED(argc);
    UNUSED(argv);

    char* configfile = "default.conf";
    int filesz, realtime = 0;
    FILE* f;
    char* buf;

    if (argc == 1)
        goto parse_config;
    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];
        int j = 0;
        for (;;) {
            const struct option* o = options + j++;
            int long_ver = arg[1] == '-'; // XXX what if string is only 1 byte long?

            if (!strcmp(long_ver ? o->alias : o->name, arg + (long_ver + 1))) {
                char* data;
                if (o->flags & HASARG) {
                    if (!(data = argv[i++])) {
                        fprintf(stderr, "Expected argument to option %s\n", arg);
                        return 0;
                    }
                } else
                    data = NULL;

                switch (o->id) {
                case OPTION_HELP:
                    generic_help(options);
                    return 0;
                case OPTION_CONFIG:
                    configfile = data;
                    break;
                case OPTION_REALTIME:
                    realtime = -1;
                    break;
                }
                break;
            }
        }
    }

parse_config:
    f = fopen(configfile, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open configuration file %s\n", configfile);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    buf = malloc((filesz = ftell(f)) + 1);
    fseek(f, 0, SEEK_SET);
    if (fread(buf, filesz, 1, f) != 1) {
        perror("fread");
        fprintf(stderr, "Failed to read configuration file\n");
        return -1;
    }

    buf[filesz] = 0;

    fclose(f);

    int result = parse_cfg(buf);
    free(buf);
    if (result < 0)
        return -1;

    if (pc.memory_size < (1 << 20)) {
        fprintf(stderr, "Memory size (0x%x) too small\n", pc.memory_size);
        return -1;
    }
    if (pc.vga_memory_size < (256 << 10)) {
        fprintf(stderr, "VGA memory size (0x%x) too small\n", pc.vga_memory_size);
        return -1;
    }
    if (pc_init(&pc) == -1) {
        fprintf(stderr, "Unable to initialize PC\n");
        return -1;
    }
#if 0
    // Good for debugging
    while(1){
        pc_execute();
    }
#else
    // Good for real-world stuff
    while (1) {
        int ms_to_sleep = pc_execute();
        // Update our screen/devices here
        vga_update();
        display_handle_events();
        ms_to_sleep &= realtime;
        if (ms_to_sleep)
            display_sleep(ms_to_sleep);
        //display_sleep(5);
    }
#endif
}