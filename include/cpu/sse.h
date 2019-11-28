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
void punpckl(void* dst, void* src, int size, int copysize);
void pmullw(uint16_t* dest, uint16_t* src, int wordcount);
void cpu_psraw(uint16_t* a, int shift, int mask, int wordcount);
void cpu_psrlw(uint16_t* a, int shift, int mask, int wordcount);
void cpu_psllw(uint16_t* a, int shift, int mask, int wordcount);
void cpu_psrad(uint32_t* a, int shift, int mask, int dwordcount);
void cpu_psrld(uint32_t* a, int shift, int mask, int dwordcount);
void cpu_pslld(uint32_t* a, int shift, int mask, int dwordcount);
void cpu_psraq(uint64_t* a, int shift, int mask, int qwordcount);
void cpu_psrlq(uint64_t* a, int shift, int mask, int qwordcount);
void cpu_psllq(uint64_t* a, int shift, int mask, int qwordcount);

#endif