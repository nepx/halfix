#ifndef OPS_H
#define OPS_H

#include <stdint.h>

// ctrlflow.c
int jmpf(uint32_t eip, uint32_t cs, uint32_t eip_after);
int callf(uint32_t eip, uint32_t cs, uint32_t oldeip, int is32);
int iret(uint32_t tss_eip, int is32);
int retf(int adjust, int is32);
int sysenter(void);
int sysexit(void);

// arith.c
void cpu_arith8(int op, uint8_t* dest, uint8_t src);
void cpu_arith16(int op, uint16_t* dest, uint16_t src);
void cpu_arith32(int op, uint32_t* dest, uint32_t src);
void cpu_shift8(int op, uint8_t* dest, uint8_t src);
void cpu_shift16(int op, uint16_t* dest, uint16_t src);
void cpu_shift32(int op, uint32_t* dest, uint32_t src);
void cpu_inc8(uint8_t* dest_ptr);
void cpu_inc16(uint16_t* dest_ptr);
void cpu_inc32(uint32_t* dest_ptr);
void cpu_dec8(uint8_t* dest_ptr);
void cpu_dec16(uint16_t* dest_ptr);
void cpu_dec32(uint32_t* dest_ptr);
void cpu_not8(uint8_t* dest_ptr);
void cpu_not16(uint16_t* dest_ptr);
void cpu_not32(uint32_t* dest_ptr);
void cpu_neg8(uint8_t* dest_ptr);
void cpu_neg16(uint16_t* dest_ptr);
void cpu_neg32(uint32_t* dest_ptr);

int cpu_muldiv8(int op, uint32_t src);
int cpu_muldiv16(int op, uint32_t src);
int cpu_muldiv32(int op, uint32_t src);
uint8_t cpu_imul8(uint8_t op1, uint8_t op2);
uint16_t cpu_imul16(uint16_t op1, uint16_t op2);
uint32_t cpu_imul32(uint32_t op1, uint32_t op2);

void cpu_shrd16(uint16_t* dest_ptr, uint16_t src, int count);
void cpu_shrd32(uint32_t* dest_ptr, uint32_t src, int count);
void cpu_shld16(uint16_t* dest_ptr, uint16_t src, int count);
void cpu_shld32(uint32_t* dest_ptr, uint32_t src, int count);

void cpu_cmpxchg8(uint8_t* op1, uint8_t op2);
void cpu_cmpxchg16(uint16_t* op1, uint16_t op2);
void cpu_cmpxchg32(uint32_t* op1, uint32_t op2);

void xadd8(uint8_t*, uint8_t*);
void xadd16(uint16_t*, uint16_t*);
void xadd32(uint32_t*, uint32_t*);

void bt16(uint16_t a, int shift);
void bt32(uint32_t a, int shift);
void bts16(uint16_t* a, int shift);
void bts32(uint32_t* a, int shift);
void btc16(uint16_t* a, int shift);
void btc32(uint32_t* a, int shift);
void btr16(uint16_t* a, int shift);
void btr32(uint32_t* a, int shift);

uint16_t bsf16(uint16_t src, uint16_t old);
uint32_t bsf32(uint32_t src, uint32_t old);
uint16_t bsr16(uint16_t src, uint16_t old);
uint32_t bsr32(uint32_t src, uint32_t old);

// io.c
int cpu_io_check_access(uint32_t port, int size);
uint32_t cpu_inb(uint32_t port);
uint32_t cpu_inw(uint32_t port);
uint32_t cpu_ind(uint32_t port);
void cpu_outb(uint32_t port, uint32_t data);
void cpu_outw(uint32_t port, uint32_t data);
void cpu_outd(uint32_t port, uint32_t data);

// stack.c
int cpu_pusha(void);
int cpu_pushad(void);
int cpu_popa(void);
int cpu_popad(void);

// misc.c
void cpuid(void);
int rdmsr(uint32_t index, uint32_t* high, uint32_t* low);
int wrmsr(uint32_t index, uint32_t high, uint32_t low);
int pushf(void);
int pushfd(void);
int popf(void);
int popfd(void);
int ltr(uint32_t x);
int lldt(uint32_t x);
uint32_t lar(uint16_t op1, uint32_t op2);
uint32_t lsl(uint16_t op1, uint32_t op2);
void verify_segment_access(uint16_t sel, int write);
void arpl(uint16_t* ptr, uint16_t reg);

// string.c
// <<< BEGIN AUTOGENERATE "string" >>>
int movsb16(int flags);
int movsb32(int flags);
int movsw16(int flags);
int movsw32(int flags);
int movsd16(int flags);
int movsd32(int flags);
int stosb16(int flags);
int stosb32(int flags);
int stosw16(int flags);
int stosw32(int flags);
int stosd16(int flags);
int stosd32(int flags);
int scasb16(int flags);
int scasb32(int flags);
int scasw16(int flags);
int scasw32(int flags);
int scasd16(int flags);
int scasd32(int flags);
int insb16(int flags);
int insb32(int flags);
int insw16(int flags);
int insw32(int flags);
int insd16(int flags);
int insd32(int flags);
int outsb16(int flags);
int outsb32(int flags);
int outsw16(int flags);
int outsw32(int flags);
int outsd16(int flags);
int outsd32(int flags);
int cmpsb16(int flags);
int cmpsb32(int flags);
int cmpsw16(int flags);
int cmpsw32(int flags);
int cmpsd16(int flags);
int cmpsd32(int flags);
int lodsb16(int flags);
int lodsb32(int flags);
int lodsw16(int flags);
int lodsw32(int flags);
int lodsd16(int flags);
int lodsd32(int flags);

// <<< END AUTOGENERATE "string" >>>

#endif