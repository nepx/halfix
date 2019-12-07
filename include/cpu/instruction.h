#ifndef INSTRUCTION_H
#define INSTRUCTION_H

#include <stdint.h>

typedef struct decoded_instruction __insn_t;
typedef __insn_t* (*insn_handler_t)(__insn_t*);

#define I_LENGTH(i) (i & 15)
#define I_LENGTH2(i) i // For instances where there is nothing else in i->flags
#define I_ADDR16_SHIFT 4
// Prefixes. Note that REPZ and REPNZ cannot be set at the same time.
#define I_PREFIX_SHIFT 6
#define I_PREFIX_NONE (0 << 6)
#define I_PREFIX_REPZ (1 << 6)
#define I_PREFIX_REPNZ (2 << 6)
#define I_PREFIX_MASK (3 << 6)

#define I_RM_SHIFT 8
#define I_BASE_SHIFT 8
#define I_REG_SHIFT 12
#define I_INDEX_SHIFT 16
#define I_SCALE_SHIFT 20
#define I_SEG_SHIFT 22
#define I_OP_SHIFT 25

#define I_RM(i) i >> I_RM_SHIFT & 15
#define I_BASE(i) i >> I_BASE_SHIFT & 15 // Same thing as R/M, but with 4 bits
#define I_REG(i) i >> I_REG_SHIFT & 15
#define I_INDEX(i) i >> I_INDEX_SHIFT & 15
#define I_SCALE(i) i >> I_SCALE_SHIFT & 3
#define I_SEG_BASE(i) i >> I_SEG_SHIFT & 7
#define I_OP(i) i >> I_OP_SHIFT & 7
#define I_OP2(i) (i&(1 << I_OP_SHIFT))
#define I_OP3(i) i >> I_OP_SHIFT & 15

#define I_SET_ADDR16(i, j) i |= (j) << I_ADDR16_SHIFT
#define I_SET_RM(i, j) i |= (j) << I_RM_SHIFT
#define I_SET_BASE(i, j) i |= (j) << I_BASE_SHIFT
#define I_SET_REG(i, j) i |= (j) << I_REG_SHIFT
#define I_SET_INDEX(i, j) i |= (j) << I_INDEX_SHIFT
#define I_SET_SCALE(i, j) i |= (j) << I_SCALE_SHIFT
#define I_SET_OP(i, j) i |= (j) << I_OP_SHIFT
#define I_SET_SEG_BASE(i, j) i |= (j) << I_SEG_SHIFT

// Represents one decoded CPU instruction. Takes up 16 bytes on 32-bit, 20 bytes (padded out to 24 bytes) on 64-bit
struct decoded_instruction {
    // Various flags holding x86 instruction operands like effective address, length, and source/dest
    uint32_t flags;

    // Immediate values
    union {
        uint32_t imm32;
        uint16_t imm16;
        uint8_t imm8;
    };

    // Displacement for effective addresses. Can also be used as an auxiliary immediate value
    union {
        uint32_t disp32;
        uint16_t disp16;
        uint8_t disp8;

        // Signed displacement
        int16_t disp16s;
        int8_t disp8s;
    };

    insn_handler_t handler;
};

#endif