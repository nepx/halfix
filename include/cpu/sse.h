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
void punpckh(void* dst, void* src, int size, int copysize);
void pmullw(uint16_t* dest, uint16_t* src, int wordcount, int shift);
void paddusb(uint8_t* dest, uint8_t* src, int bytecount);
void paddusw(uint16_t* dest, uint16_t* src, int wordcount);
void paddssb(uint8_t* dest, uint8_t* src, int bytecount);
void paddssw(uint16_t* dest, uint16_t* src, int wordcount);
void psubusb(uint8_t* dest, uint8_t* src, int bytecount);
void psubusw(uint16_t* dest, uint16_t* src, int wordcount);
void psubssb(uint8_t* dest, uint8_t* src, int bytecount);
void psubssw(uint16_t* dest, uint16_t* src, int wordcount);
void cpu_psraw(uint16_t* a, int shift, int mask, int wordcount);
void cpu_psrlw(uint16_t* a, int shift, int mask, int wordcount);
void cpu_psllw(uint16_t* a, int shift, int mask, int wordcount);
void cpu_psrad(uint32_t* a, int shift, int mask, int dwordcount);
void cpu_psrld(uint32_t* a, int shift, int mask, int dwordcount);
void cpu_pslld(uint32_t* a, int shift, int mask, int dwordcount);
void cpu_psraq(uint64_t* a, int shift, int mask, int qwordcount);
void cpu_psrlq(uint64_t* a, int shift, int mask, int qwordcount);
void cpu_psllq(uint64_t* a, int shift, int mask, int qwordcount);
void packuswb(void* dest, void* src, int wordcount);
void packsswb(void* dest, void* src, int wordcount);
void packssdw(void* dest, void* src, int dwordcount);
void pshuf(void* dest, void* src, int imm, int shift);
void pmaddwd(void* dest, void* src, int dwordcount);
void paddb(uint8_t* dest, uint8_t* src, int);
void paddw(uint16_t* dest, uint16_t* src, int);
void paddd(uint32_t* dest, uint32_t* src, int);
void paddq(uint64_t* dest, uint64_t* src, int);
void psubb(uint8_t* dest, uint8_t* src, int);
void psubw(uint16_t* dest, uint16_t* src, int);
void psubd(uint32_t* dest, uint32_t* src, int);
void psubq(uint64_t* dest, uint64_t* src, int);

#endif