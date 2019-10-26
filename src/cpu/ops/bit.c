#include "cpu/cpu.h"
#include "cpu/opcodes.h"

void bt16(uint16_t a, int shift){
    cpu_set_cf(a >> (shift & 15) & 1);
}
void bt32(uint32_t a, int shift){
    cpu_set_cf(a >> (shift & 31) & 1);
}
void bts16(uint16_t *a, int shift){
    shift &= 15;
    cpu_set_cf(*a >> shift & 1);
    *a |= 1 << shift;
}
void bts32(uint32_t *a, int shift){
    shift &= 31;
    cpu_set_cf(*a >> shift & 1);
    *a |= 1 << shift;
}
void btc16(uint16_t *a, int shift){
    shift &= 15;
    cpu_set_cf(*a >> shift & 1);
    *a ^= 1 << shift;
}
void btc32(uint32_t *a, int shift){
    shift &= 31;
    cpu_set_cf(*a >> shift & 1);
    *a ^= 1 << shift;
}
void btr16(uint16_t *a, int shift){
    shift &= 15;
    cpu_set_cf(*a >> shift & 1);
    *a &= ~(1 << shift);
}
void btr32(uint32_t *a, int shift){
    shift &= 31;
    cpu_set_cf(*a >> shift & 1);
    *a &= ~(1 << shift);
}

uint16_t bsf16(uint16_t src, uint16_t old){
    if(src){
        cpu_set_zf(0);
        return __builtin_ctz(src & 0xFFFF);
    }else{
        cpu_set_zf(1);
        return old;
    }
}
uint32_t bsf32(uint32_t src, uint32_t old){
    cpu.laux = BIT;
    if(src){
        cpu.lr = 1; // Clear ZF
        return __builtin_ctz(src);
    }else{
        cpu.lr = 0; // Assert ZF
        return old;
    }
}
uint16_t bsr16(uint16_t src, uint16_t old){
    if(src){
        cpu_set_zf(0);
        return __builtin_clz(src & 0xFFFF) ^ 31;
    }else{
        cpu_set_zf(1);
        return old;
    }
}
uint32_t bsr32(uint32_t src, uint32_t old){
    if(src){
        cpu_set_zf(0);
        return __builtin_clz(src) ^ 31;
    }else{
        cpu_set_zf(1);
        return old;
    }
}