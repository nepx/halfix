#ifndef FPU_H
#define FPU_H

#include "cpu/cpu.h"
#include <stdint.h>
int fpu_op_register(uint32_t opcode, uint32_t flags);
int fpu_op_memory(uint32_t opcode, uint32_t flags, uint32_t linaddr);
int fpu_fwait(void);
void fpu_init(void);

int fpu_fxsave(uint32_t linaddr);
int fpu_fxrstor(uint32_t linaddr);

void fpu_debug(void);

int fpu_mem_op(struct decoded_instruction* i, uint32_t virtaddr, uint32_t seg);
int fpu_reg_op(struct decoded_instruction* i, uint32_t flags);

#ifdef NEED_STRUCT
struct fpu {
    union {
// This is a really nasty union to make MMX instructions work.
#ifdef CFG_BIG_ENDIAN
        struct
        {
            uint16_t dummy;
            union {
                uint8_t r8[8];
                uint16_t r16[4];
                uint32_t r32[2];
                uint64_t r64;
            } reg;
        } mm[8];
#else
        struct
        {
            union {
                uint8_t r8[8];
                uint16_t r16[4];
                uint32_t r32[2];
                uint64_t r64;
            } reg;
            uint16_t dummy;
        } mm[8];
#endif

#ifdef FLOATX80
        floatx80 st[8];
#endif
    };
    // <<< BEGIN STRUCT "struct" >>>
    int ftop;
    uint16_t control_word, status_word, tag_word;
    uint32_t fpu_eip, fpu_data_ptr;
    uint16_t fpu_cs, fpu_opcode, fpu_data_seg;
    // <<< END STRUCT "struct" >>>

    // These are all values used internally. They are regenerated every time fpu.control_word is modified
#ifdef FLOATX80
    float_status_t status;
#endif
};
extern struct fpu fpu;
#endif

#ifdef LIBCPU
void fpu_init_lib(void);
#endif

#endif