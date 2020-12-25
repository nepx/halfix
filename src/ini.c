// INI file parser.
// Inspired by https://dev.to/dropconfig/making-an-ini-parser-5ejn

#include "net.h"
#include "pc.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

#ifndef EMSCRIPTEN
#include <stdio.h>
#else
#include <emscripten.h>
#endif

static int load_file(struct loaded_file* lf, char* path)
{
#ifndef EMSCRIPTEN
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
#else
    EM_ASM_({
        window["load_file_xhr"]($0, $1, $2);
    },
        &lf->length, &lf->data, path);
    return 0;
#endif
}

struct ini_field {
    char* name;
    char* data;
    void* next;
};
struct ini_section {
    char* name;
    struct ini_field* fields;
    void* next;
};

enum {
    STATE_DEFAULT,
    STATE_KEY,
    STATE_VALUE,
    STATE_SECTION,
    STATE_COMMENT
};

static inline char* slice_string(char* y, int start, int end)
{
    int length = end - start;
    char* result = malloc(length + 1);
    memcpy(result, y + start, length);
    result[length] = 0;
    return result;
}

static struct ini_section* ini_parse(char* x)
{
    int state = STATE_DEFAULT,
        length = strlen(x),
        i = 0, strstart = 0, strend = 0, include_whitespace = 0;
    struct ini_section *result = calloc(1, sizeof(struct ini_section)), *head = result;
    struct ini_field* current_field = NULL;
    while (i < length) {
        int c = x[i++];
        switch (state) {
        case STATE_DEFAULT:
            if (c == '#')
                state = STATE_COMMENT;
            else if (c == '[')
                state = STATE_SECTION;
            else if (c > ' ') {
                i--;
                // Move back one character so that we get the entire key.
                state = STATE_KEY;
                strstart = i;
            }
            strstart = i;
            break;
        case STATE_COMMENT:
            if (c == '\n')
                state = STATE_DEFAULT;
            break;
        case STATE_SECTION:
            if (c == ']') {
                // Add an element to our linked list.
                struct ini_section* sect = calloc(1, sizeof(struct ini_section));
                sect->name = slice_string(x, strstart, i - 1);
                head->next = sect;
                head = sect;
                state = STATE_DEFAULT;
            }
            break;
        case STATE_KEY:
            // keystart[\s]=[\s+]
            if (c == '=') {
                struct ini_field *field = calloc(1, sizeof(struct ini_field)), *temp;
                field->name = slice_string(x, strstart, strend);
                temp = head->fields;
                head->fields = field;
                field->next = temp;
                current_field = field;

                // Move onto the next field
                state = STATE_VALUE;
                include_whitespace = 0;
                break;
            }
            if (c > 32)
                strend = i;
            break;
        case STATE_VALUE:
            // Ignore whitespace in front of any non-space characters.
            if (c == '\n') {
                current_field->data = slice_string(x, strstart, strend);
                state = STATE_DEFAULT;
                current_field = NULL;
                break;
            }
            if (!include_whitespace) {
                if (c <= 32)
                    break;
                strstart = i - 1;
                strend = strstart;
            }
            if (c > 32) {
                strend = i;
            }
            include_whitespace = 1;
            break;
        }
    }
    return result;
}

struct ini_enum {
    const char* name;
    int value;
};

static struct ini_section* get_section(struct ini_section* sect, char* name)
{
    while (sect) {
        if (sect->name) {
            if (!strcmp(sect->name, name))
                return sect;
        }
        sect = sect->next;
    }
    return NULL;
}
static char* get_field_string(struct ini_section* sect, char* name)
{
    struct ini_field* f = sect->fields;
    while (f) {
        if (!strcmp(f->name, name))
            return f->data;
        f = f->next;
    }
    return NULL;
}
static int get_field_enum(struct ini_section* sect, char* name, const struct ini_enum* vals, int def)
{
    char* x = get_field_string(sect, name);
    if (!x)
        return def;
    int i = 0;
    while (vals[i].name) {
        if (!strcmp(vals[i].name, x))
            return vals[i].value;
        i++;
    }
    printf("Unknown value: %s\n", name);
    return def;
}
static int get_field_int(struct ini_section* sect, char* name, int def)
{
    char* str = get_field_string(sect, name);
    int res = 0, i = 0;
    if (!str)
        return def; // If the field isn't there, then simply return the default
    if (str[0] == '0' && str[1] == 'x')
        for (;; ++i) {
            int n;
            if (str[i] >= '0' && str[i] <= '9')
                n = str[i] - '0';
            else if (str[i] >= 'A' && str[i] <= 'F')
                n = str[i] - 'A';
            else if (str[i] >= 'a' && str[i] <= 'f')
                n = str[i] - 'a';
            else
                break;
            res = (res << 4) + n;
        }
    else
        for (;; ++i) {
            if (str[i] < '0' || str[i] > '9')
                break;
            res = res * 10 + str[i] - '0';
        }
    switch (str[i]) {
    case 'K':
    case 'k':
        res <<= 10;
        break;
    case 'M':
    case 'm':
        res <<= 20;
        break;
    case 'G':
    case 'g':
        res <<= 30;
        break;
    }
    return res;
}
static int get_field_long(struct ini_section* sect, char* name, int def)
{
    char* str = get_field_string(sect, name);
    uint64_t res = 0;
    int i = 0;
    if (!str)
        return def; // If the field isn't there, then simply return the default
    for (;; ++i) {
        if (str[i] < '0' || str[i] > '9')
            break;
        res = res * 10 + str[i] - '0';
    }
    return res;
}
static void free_ini(struct ini_section* sect)
{
    while (sect) {
        // Free the name
        free(sect->name);
        // Free the fields
        struct ini_field* f = sect->fields;
        while (f) {
            free(f->name);
            free(f->data);
            struct ini_field* f_next = f->next;
            free(f);
            f = f_next;
        }
        // Now move to the next one
        struct ini_section* next = sect->next;
        free(sect);
        sect = next;
    }
}

// The following is the Halfix-specific part.
static const struct ini_enum drive_types[] = {
    { "cd", DRIVE_TYPE_CDROM },
    { "hd", DRIVE_TYPE_DISK },
    { "none", DRIVE_TYPE_NONE },
    { NULL, 0 }
};
static const struct ini_enum boot_types[] = {
    { "cd", BOOT_CDROM },
    { "hd", BOOT_DISK },
    { "fd", BOOT_FLOPPY },
    { "none", BOOT_NONE },
    { NULL, 0 }
};
static const struct ini_enum driver_types[] = {
    { "sync", 1 },
    { "raw", 1 },
    { "chunked", 0 },
    { "normal", 0 },
    { "network", 2 },
    { "net", 2 },
    { NULL, 0 }
};
static const struct ini_enum virtio_types[] = {
    { "9p", VIRTIO_9P },
    { "p9", VIRTIO_9P },
    { "9pfs", VIRTIO_9P },
    { "p9fs", VIRTIO_9P },
    { NULL, 0 }
};

static int parse_disk(struct drive_info* drv, struct ini_section* s, int id)
{
    if (s == NULL) {
        drv->type = DRIVE_TYPE_NONE;
        return 0;
    }

    // Determine the media type
    drv->type = get_field_enum(s, "type", drive_types, DRIVE_TYPE_DISK);
    int driver = get_field_enum(s, "driver", driver_types, -1), inserted = get_field_int(s, "inserted", 0), wb = get_field_int(s, "writeback", 0);
    char* path = get_field_string(s, "file");

    if (driver < 0 && inserted) {
#ifndef EMSCRIPTEN
        // Try auto-detecting driver type if not specified.
        driver = drive_autodetect_type(path);
        if (driver < 0)
            FATAL("INI", "Unable to determine driver to use for ata%d-%s!\n", id >> 1, id & 1 ? "slave" : "master");

#else
        // The wrapper code already knows what driver we have. It knows best.
        driver = 0;
#endif
    }

    if (driver == 0 && wb)
        printf("WARNING: Disk %d uses async (chunked) driver but writeback is not supported!!\n", id);
    drv->modify_backing_file = wb;
    if (path && inserted) {
#ifndef EMSCRIPTEN
        UNUSED(id);
        if (driver == 0)
            return drive_init(drv, path);
        else
            return drive_simple_init(drv, path);
#else
        UNUSED(driver);
        EM_ASM_({ window["drive_init"]($0, $1, $2); }, drv, path, id);
#endif
    }

    return 0;
}

static char* dupstr(char* src)
{
    if (!src)
        return NULL;
    int len = strlen(src);
    char* res = malloc(len + 1);
    strcpy(res, src);
    return res;
}

#ifdef EMSCRIPTEN
EMSCRIPTEN_KEEPALIVE
#endif
int parse_cfg(struct pc_settings* pc, char* data)
{
    struct ini_section* global = ini_parse(data);

    // Determine BIOS/VGABIOS paths
    char *bios = get_field_string(global, "bios"), *vgabios = get_field_string(global, "vgabios");
    if (!bios || !vgabios) {
        fprintf(stderr, "No BIOS/VGABIOS!\n");
        goto fail;
    }

    if (load_file(&pc->bios, bios) || load_file(&pc->vgabios, vgabios)) {
        fprintf(stderr, "Unable to load BIOS/VGABIOS image\n");
        goto fail;
    }

    // Determine memory size
    pc->memory_size = get_field_int(global, "memory", 32 * 1024 * 1024);
    pc->vga_memory_size = get_field_int(global, "vgamemory", 4 * 1024 * 1024);

    // Set emulator time
    pc->current_time = get_field_long(global, "now", 0);

    // Enable/disable features
    pc->pci_enabled = get_field_int(global, "pci", 1);
    pc->acpi_enabled = get_field_int(global, "acpi", 1);
    pc->apic_enabled = get_field_int(global, "apic", 1);
    pc->floppy_enabled = get_field_int(global, "floppy", 1);
    pc->vbe_enabled = get_field_int(global, "vbe", 1);
    pc->pci_vga_enabled = get_field_int(global, "pcivga", 0);
    pc->boot_kernel = get_field_int(global, "kernel", 0);

    // Now figure out disk image information
    int res = parse_disk(&pc->drives[0], get_section(global, "ata0-master"), 0);
    res |= parse_disk(&pc->drives[1], get_section(global, "ata0-slave"), 1);
    res |= parse_disk(&pc->drives[2], get_section(global, "ata1-master"), 2);
    res |= parse_disk(&pc->drives[3], get_section(global, "ata1-slave"), 3);
    if (res) {
        fprintf(stderr, "Unable to initialize disk drive images\n");
        goto fail;
    }

    // Now check for floppy drive information
    res = parse_disk(&pc->floppy_drives[0], get_section(global, "fda"), 4);
    res |= parse_disk(&pc->floppy_drives[1], get_section(global, "fdb"), 5);
    if (res) {
        fprintf(stderr, "Unable to initialize floppy drive images\n");
        goto fail;
    }

    // Check for network
    struct ini_section* net = get_section(global, "ne2000");
    if (net) {
        pc->ne2000.enabled = get_field_int(net, "enabled", 1);
        pc->ne2000.pci = get_field_int(net, "pci", pc->pci_enabled);
        pc->ne2000.port_base = get_field_int(net, "iobase", 0x300);
        pc->ne2000.irq = get_field_int(net, "irq", 3);
        char* mac = get_field_string(net, "mac");
        if (!mac) {
            for (int i = 0; i < 6; i++)
                pc->ne2000.mac_address[i] = 0; // Setting address to all zeros will let the controller decide.
        } else {
            // XXX - messy loop
            for (int k = 0, i = 0; k < 6; k++) {
                // MAC addresses must be in the form AA:AA:AA:AA:AA:AA
                int mac_part = 0;
                if (k != 0)
                    if (mac[i++] != ':')
                        fprintf(stderr, "Malformed MAC address\n");

                for (int j = 0; j < 2; j++) {
                    int n;
                    if (mac[j + i] >= '0' && mac[j + i] <= '9')
                        n = mac[j + i] - '0';
                    else if (mac[j + i] >= 'A' && mac[j + i] <= 'F')
                        n = mac[j + i] - 'A';
                    else if (mac[j + i] >= 'a' && mac[j + i] <= 'f')
                        n = mac[j + i] - 'a';
                    else
                        FATAL("INI", "Malformed MAC address\n");
                    mac_part = (mac_part << 4) | n;
                }
                i += 2;
            }
        }
#ifndef EMSCRIPTEN
        if (pc->ne2000.enabled) {
            // Emscripten network configuration is done in libhalfix.js -- there's nothing to do here.
            char* cfg = get_field_string(net, "arg"); // "arg" is a parameter to the network driver
            net_init(cfg);
        }
#endif
    } else {
        pc->ne2000.enabled = 0;
    }

    if (pc->boot_kernel) {
        struct ini_section* kernel = get_section(global, "kernel");
        pc->kernel_cmdline = dupstr(get_field_string(kernel, "cmdline"));
        pc->kernel_img = dupstr(get_field_string(kernel, "image"));
    } else {
        pc->kernel_cmdline = NULL;
        pc->kernel_img = NULL;
    }

    char sid[50];
    for (int i = 0; i < MAX_VIRTIO_DEVICES; i++) {
        sprintf(sid, "virtio%d", i);
        struct ini_section* virtio = get_section(global, sid);
        struct virtio_cfg* cfg = &pc->virtio[i];
        cfg->type = -1;
        if (!virtio)
            continue;

        int x = get_field_enum(virtio, "type", virtio_types, -1);
        if (x == -1) {
            fprintf(stderr, "Unknown virtio%d type - ignoring\n", i);
            continue;
        }
        cfg->type = x;
        switch (x) {
        case VIRTIO_9P:
            cfg->fs9p.path = dupstr(get_field_string(virtio, "path"));
            cfg->fs9p.ro = get_field_int(virtio, "readonly", 1);
            break;
        }
    }

    // Determine boot order
    struct ini_section* boot = get_section(global, "boot");
    if (boot == NULL) {
        pc->boot_sequence[0] = BOOT_DISK;
        pc->boot_sequence[1] = BOOT_CDROM;
        pc->boot_sequence[2] = BOOT_FLOPPY;
    } else {
        pc->boot_sequence[0] = get_field_enum(boot, "a", boot_types, BOOT_DISK);
        pc->boot_sequence[1] = get_field_enum(boot, "b", boot_types, BOOT_CDROM);
        pc->boot_sequence[2] = get_field_enum(boot, "c", boot_types, BOOT_FLOPPY);
    }

    // Get CPU information
    struct ini_section* cpu = get_section(global, "cpu");
    if (cpu == NULL) {
        pc->cpu.cpuid_limit_winnt = 0;
    } else {
        pc->cpu.cpuid_limit_winnt = get_field_int(cpu, "cpuid_limit_winnt", 0);
    }

    UNUSED(get_section);

    free_ini(global);
    return 0;
fail:
    free_ini(global);
    return -1;
}