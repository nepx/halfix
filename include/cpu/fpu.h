#ifndef FPU_H
#define FPU_H

#include "cpu/cpu.h"
#include <stdint.h>
int fpu_op_register(uint32_t opcode, uint32_t flags);
int fpu_op_memory(uint32_t opcode, uint32_t flags, uint32_t linaddr);
int fpu_fwait(void);
void fpu_init(void);

void fpu_debug(void);

int fpu_mem_op(struct decoded_instruction* i, uint32_t virtaddr, uint32_t seg);
int fpu_reg_op(struct decoded_instruction* i, uint32_t flags);

#endif