#ifndef SSE_H
#define SSE_H

#include <stdint.h>
int cpu_sse_exception(void);
void cpu_mov128(void* dest, void* src);
void cpu_mov64(void* dest, void* src);

// Memory access functions
int cpu_write128(uint32_t linaddr, void* x);
int cpu_read128(uint32_t linaddr, void* x);
int cpu_write64(uint32_t linaddr, void* x);
int cpu_read64(uint32_t linaddr, void* x);

// Operations
void cpu_sse_xorps(uint32_t* dest, uint32_t* src);

#endif