#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define UNUSED(x) (void)(x)

// This is to make Visual Studio Code not complain
#if defined(__linux__) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS 0x20
#endif

void* aalloc(int size, int align);
void afree(void* ptr);

//#define LOGGING_DISABLED

#ifdef EMSCRIPTEN
//#if 0
#define LOGGING_DISABLED
#endif
//#define LOG(component, x, ...) fprintf(stderr, "[" component "] " x, ##__VA_ARGS__)
//#define LOG(component, x, ...) printf("[" component "] " x, ##__VA_ARGS__)
// We have the Halfix abort function (which releases the mouse and optionally writes out the event log) and then the real abort function to appease the compiler.
#define FATAL(component, x, ...)                              \
    do {                                                      \
        fprintf(stderr, "[" component "] " x, ##__VA_ARGS__); \
        ABORT();                                              \
        abort();                                              \
    } while (0)
#define NOP() \
    do {      \
    } while (0)

#define ABORT() util_abort()
#define debugger util_debug()

#ifdef LOGGING_DISABLED
#define LOG(component, x, ...) NOP()
#else
#define LOG(component, x, ...) fprintf(stderr, "[" component "] " x, ##__VA_ARGS__)
#endif

typedef uint64_t itick_t;
itick_t get_now(void);
extern uint32_t ticks_per_second;

// Functions that mess around with timing
void add_now(itick_t a);

// Quick Malloc API
void qmalloc_init(void);
void* qmalloc(int size, int align);
void qfree(void);

void util_debug(void);
void util_abort(void);

#endif