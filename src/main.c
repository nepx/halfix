#include "cpuapi.h"
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
int parse_cfg(struct pc_settings* pc, char* data);

struct option {
    const char *alias, *name;
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
        int linelength = sprintf(line, " -%s", o->alias);
        if (o->alias)
            linelength += sprintf(line + linelength, " --%s", o->name);

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

            if (!o->name)
                break;
            if (!strcmp(long_ver ? o->name : o->alias, arg + (long_ver + 1))) {
                char* data;
                if (o->flags & HASARG) {
                    if (!(data = argv[++i])) {
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
                    continue;
                case OPTION_REALTIME:
                    realtime = -1;
                    continue;
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

    int result = parse_cfg(&pc, buf);
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
            display_sleep(ms_to_sleep * 5);
        //display_sleep(5);
    }
#endif
}