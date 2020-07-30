// x86 decoder

#include "cpu/cpu.h"
#include "cpu/opcodes.h"
#include "cpu/simd.h"

#ifdef LIBCPU
void* get_phys_ram_ptr(uint32_t addr, int write);
#else
#define get_phys_ram_ptr(a, b) (cpu.mem + (a))
#endif

// ============================================================================
// Important state variable used by the emulator
// ============================================================================
static uint8_t* rawp; // Physical pointer used by decoder
static uint8_t prefetch[16];
static int state_hash;
static int seg_prefix[2] = { DS, SS };
#define rb() *rawp++
#define rbs() (int8_t) * rawp++
static inline uint32_t rw(void)
{
    uint32_t val = rawp[0] | rawp[1] << 8;
    rawp += 2;
    return val;
}
static inline uint32_t rd(void)
{
    uint32_t val = rawp[0] | rawp[1] << 8 | rawp[2] << 16 | rawp[3] << 24;
    rawp += 4;
    return val;
}
static inline uint32_t rv(void)
{
    // Read variable sized word/dword based on address size
    if (state_hash & STATE_CODE16)
        return rw();
    else
        return rd();
}
static inline uint32_t rvs(void)
{
    if (state_hash & STATE_CODE16)
        return (int16_t)rw();
    else
        return rd();
}

// ============================================================================
// x86 length decoder
// The tables are reused by the decoder
// ============================================================================

// An 8-bit immediate
#define opcode_imm8 0x10
// A 16-bit or 32-bit immediate
#define opcode_immv 0x20
// A 16-bit immediate (i.e. ret iw)
#define opcode_imm16 0x40
// Whether the opcode is valid with a LOCK prefix
#define opcode_lock_valid 0x80
enum {
    opcode_singlebyte = 0, // must be 0
    opcode_prefix,
    opcode_special,
    opcode_modrm,
    opcode_moffs,
    opcode_invalid
};

static const uint8_t optable[0x100] = {
    // Lower nibble: one of the values in the enum above
    // Upper nibble: immediate size (see opcode_* macros above)
    //       00    01    02    03    04    05    06    07    08    09    0A    0B    0C    0D    0E    0F
    /* 00 */ 0x83, 0x83, 0x03, 0x03, 0x10, 0x20, 0x00, 0x00, 0x83, 0x83, 0x03, 0x03, 0x10, 0x20, 0x00, 0x01,
    /* 10 */ 0x83, 0x83, 0x03, 0x03, 0x10, 0x20, 0x00, 0x00, 0x83, 0x83, 0x03, 0x03, 0x10, 0x20, 0x00, 0x00,
    /* 20 */ 0x83, 0x83, 0x03, 0x03, 0x10, 0x20, 0x01, 0x00, 0x83, 0x83, 0x03, 0x03, 0x10, 0x20, 0x01, 0x00,
    /* 30 */ 0x83, 0x83, 0x03, 0x03, 0x10, 0x20, 0x01, 0x00, 0x03, 0x03, 0x03, 0x03, 0x10, 0x20, 0x01, 0x00,
    /* 40 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* 50 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* 60 */ 0x00, 0x00, 0x03, 0x03, 0x01, 0x01, 0x01, 0x01, 0x20, 0x23, 0x10, 0x13, 0x00, 0x00, 0x00, 0x00,
    /* 70 */ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    /* 80 */ 0x93, 0xA3, 0x13, 0x93, 0x03, 0x03, 0x83, 0x83, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    /* 90 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* A0 */ 0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x10, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* B0 */ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    /* C0 */ 0x13, 0x13, 0x40, 0x00, 0x03, 0x03, 0x13, 0x23, 0x30, 0x00, 0x20, 0x00, 0x00, 0x10, 0x00, 0x00,
    /* D0 */ 0x03, 0x03, 0x03, 0x03, 0x10, 0x10, 0x00, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    /* E0 */ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x20, 0x20, 0x60, 0x10, 0x00, 0x00, 0x00, 0x00,
    /* F0 */ 0x01, 0x05, 0x01, 0x01, 0x00, 0x00, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x83, 0x83
};
static const uint8_t optable0F[0x100] = {
    // 0F 21 and 0F 23 are considered imm8 instructions since they only allow ModR/M.mod==3
    //       00    01    02    03    04    05    06    07    08    09    0A    0B    0C    0D    0E    0F
    /* 00 */ 0x03, 0x03, 0x03, 0x03, 0x05, 0x05, 0x00, 0x05, 0x00, 0x00, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    /* 10 */ 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x05, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    /* 20 */ 0x03, 0x10, 0x03, 0x10, 0x05, 0x05, 0x05, 0x05, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    /* 30 */ 0x00, 0x00, 0x00, 0x00, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    /* 40 */ 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    /* 50 */ 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    /* 60 */ 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    /* 70 */ 0x13, 0x13, 0x13, 0x13, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    /* 80 */ 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    /* 90 */ 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    /* A0 */ 0x00, 0x00, 0x00, 0x03, 0x13, 0x03, 0x05, 0x05, 0x00, 0x00, 0x05, 0x83, 0x13, 0x03, 0x03, 0x03,
    /* B0 */ 0x83, 0x83, 0x03, 0x83, 0x03, 0x03, 0x03, 0x03, 0x05, 0x05, 0x93, 0x83, 0x03, 0x03, 0x03, 0x03,
    /* C0 */ 0x83, 0x83, 0x03, 0x03, 0x03, 0x03, 0x03, 0x83, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* D0 */ 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    /* E0 */ 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    /* F0 */ 0x05, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x05
};
// The following function determines the length of an instruction.
// The address at which to start at is in rawp. Pass the maximum number of bytes that can be translated before it returns -1.
// In the case that an invalid instruction is encountered, it simply returns the minimum number of bytes that need to be read before the decoder realizes it's an invalid instruction
// Returns instruction length (>0) if instruction is shorter than max_bytes.
// Returns 0 if instruction is longer than max_bytes
static int find_instruction_length(int max_bytes)
{
    int opcode, state_hash = cpu.state_hash, initial_max_bytes = max_bytes, opcode_info;
    const uint8_t* tbl = optable;

#define OPCODE_TOO_LONG() \
    if (max_bytes-- < 0)  \
    return -1
top:
    opcode = rawp[initial_max_bytes - max_bytes];
    OPCODE_TOO_LONG();
    switch (opcode) {
    case 0x0F:
        tbl = optable0F;
        opcode = rawp[initial_max_bytes - max_bytes];
        OPCODE_TOO_LONG();
        goto done;
    case 0x66: // Operand size prefix
        if (!((state_hash ^ cpu.state_hash) & STATE_CODE16))
            state_hash ^= STATE_CODE16;
        break;
    case 0x67: // Address size prefix
        if (!((state_hash ^ cpu.state_hash) & STATE_ADDR16))
            state_hash ^= STATE_ADDR16;
        break;
    case 0x26:
    case 0x2E:
    case 0x36:
    case 0x3E:
    case 0x64:
    case 0x65: // Segment prefixes
    case 0xF0: // LOCKs
    case 0xF2:
    case 0xF3: // REP
        break;
    default:
        goto done;
    }
    goto top;

done:
    //opcode = rawp[initial_max_bytes - max_bytes];
    //OPCODE_TOO_LONG();

    opcode_info = tbl[opcode]; // Note: no risk of overflow since rawp is 8-bits in width, and thus opcode will always be < 256
    switch (opcode_info & 15) {
    case opcode_singlebyte:
        break;
    case opcode_special:
        CPU_FATAL("Unknown special opcode: %02x\n", opcode);
    case opcode_modrm: {
        int modrm, sib;
        modrm = rawp[initial_max_bytes - max_bytes];
        OPCODE_TOO_LONG();

        if (tbl == optable && ((opcode & 0xFE) == 0xF6)) {
            if (!(modrm >> 3 & 6)) { // F6/F7 opcode has imm after it if REG is 0 or 1
                if (opcode & 1)
                    opcode_info |= opcode_immv;
                else
                    opcode_info |= opcode_imm8;
            }
        }

        if (modrm < 0xC0) { // NOT pointing to register
            if (state_hash & STATE_ADDR16) {
                switch (modrm >> 6 & 3) {
                case 0:
                    if ((modrm & 7) == 6)
                        max_bytes -= 2;
                    break;
                case 1:
                    max_bytes--;
                    break;
                case 2:
                    max_bytes -= 2;
                    break;
                }
            } else {
                switch ((modrm >> 3 & 0x18) | (modrm & 7)) { // Combine MOD and RM fields
                case 4: { // SIB
                    sib = rawp[initial_max_bytes - max_bytes];
                    OPCODE_TOO_LONG();
                    if ((sib & 7) == 5)
                        max_bytes -= 4;
                    break;
                }
                case 0x0C:
                    max_bytes -= 2;
                    break;
                case 0x14:
                    max_bytes -= 5;
                    break;
                case 5:
                case 16 ... 19:
                case 21 ... 23:
                    max_bytes -= 4;
                    break;
                case 0 ... 3:
                case 6 ... 7:
                case 24 ... 31: // mod=3
                    break; // no disp
                case 8 ... 0x0B:
                case 0x0D ... 0x0F:
                    max_bytes--;
                    break;
                }
            }
        }
        break;
    }
    case opcode_moffs:
        if (state_hash & STATE_ADDR16)
            max_bytes -= 2;
        else
            max_bytes -= 4;
        break;
    case opcode_invalid: // We don't do much with this at the moment
        break;
    }

    // Note: a single opcode may have multiple immediate flags (example: ENTER)
    if (opcode_info & opcode_imm8)
        max_bytes -= 1;
    if (opcode_info & opcode_imm16)
        max_bytes -= 2;
    if (opcode_info & opcode_immv)
        max_bytes -= state_hash & STATE_CODE16 ? 2 : 4;
    // LOCK prefix is handled inside the function opcode_prefix, no need to handle here

    // Check if we prefetched too much, "init - cur" will give wrong values if cur is negative
    if (max_bytes < 0)
        return -1;
    return initial_max_bytes - max_bytes;
}

typedef int (*decode_handler_t)(struct decoded_instruction*);
#define SIZEOP(a16, a32) state_hash& STATE_CODE16 ? a16 : a32
#define REGOP(mem, reg) modrm < 0xC0 ? mem : reg

#define R8(i) ((i)&3) << 2 | (i) >> 2
#define R16(i) (i) << 1
#define R32(i) (i)
#define I_SET_RM8(dest, src) I_SET_RM(dest, R8(src));
#define I_SET_RMv(dest, src)       \
    if (state_hash & STATE_CODE16) \
        I_SET_RM(dest, R16(src));  \
    else                           \
    I_SET_RM(dest, src)
#define I_SET_REG8(dest, src) I_SET_REG(dest, R8(src));
#define I_SET_REGv(dest, src)      \
    if (state_hash & STATE_CODE16) \
        I_SET_REG(dest, R16(src)); \
    else                           \
    I_SET_REG(dest, src)

enum {
    MODRM_SIZE8,
    MODRM_SIZE16,
    MODRM_SIZE32
};

static int addr16_lut[] = {
    EBX, EBX, EBP, EBP, ESI, EDI, EZR, EBX, // First row: Base 1
    ESI, EDI, ESI, EDI, EZR, EZR, EZR, EZR, // Second row: Base 2
    0, 0, 1, 1, 0, 0, 0, 0 // Third row: Segmentation register
};

// This table contains the [reg+reg] combination for mod=1 or 2.
static int addr16_lut2[] = {
    EBX, EBX, EBP, EBP, ESI, EDI, EBP, EBX, //
    ESI, EDI, ESI, EDI, EZR, EZR, EZR, EZR, //
    0, 0, 1, 1, 0, 0, 1, 0 //
};

// Whether a register, by default, selects DS or SS
static int addr32_lut[] = {
    //EAX, ECX, EDX, EBX, EZR, EBP, ESI, EDI,
    0, 0, 0, 0, 0, 1, 0, 0
};
// Same thing as lut2 but for SIB
static int addr32_lut2[] = {
    0, 0, 0, 0, 1, 1, 0, 0 // For SIB, only bases 4 and 5 have a SREG of SS
};

static int parse_modrm(struct decoded_instruction* i, uint8_t modrm, int is8)
{
    int addr16 = state_hash >> 1 & 1, flags = addr16 << 4, rm = modrm & 7, new_modrm = rm | ((modrm & 0xC0) >> 3); // Set ADDR16 bit

    switch (is8 & 3) {
    case 0:
    case 3:
        I_SET_REGv(flags, modrm >> 3 & 7);
        break;
    case 1:
        I_SET_REG8(flags, modrm >> 3 & 7);
        break;
    case 2:
        I_SET_REG(flags, modrm >> 3 & 7);
        break;
    }
    if (addr16) {
        switch (new_modrm) {
        case 0 ... 5:
        case 7: // [bx+si], [bx], etc.
            I_SET_BASE(flags, addr16_lut[rm]);
            I_SET_INDEX(flags, addr16_lut[rm | 8]);
            I_SET_SCALE(flags, 0);
            I_SET_SEG_BASE(flags, seg_prefix[addr16_lut[rm | 16]]);
            i->disp32 = 0; // Set it to zero because there is no displacement
            break;
        case 6: // [disp16]
            I_SET_BASE(flags, EZR);
            I_SET_INDEX(flags, EZR);
            I_SET_SCALE(flags, 0);
            I_SET_SEG_BASE(flags, seg_prefix[0]);
            i->disp32 = rw();
            break;
        case 8 ... 15: // [bx+si+disp8s], [bx+disp8s], etc.
            I_SET_BASE(flags, addr16_lut2[rm]);
            I_SET_INDEX(flags, addr16_lut2[rm | 8]);
            I_SET_SCALE(flags, 0);
            I_SET_SEG_BASE(flags, seg_prefix[addr16_lut2[rm | 16]]);
            //printf("Translated (modrm=%02x prefix=%d idx=%d rm=%d a=%d b=%d)\n", modrm, seg_prefix[addr16_lut2[rm | 16]], addr16_lut2[rm | 16], rm, addr16_lut2[0], addr16_lut2[1]);
            i->disp32 = rbs();
            break;
        case 16 ... 23: // [bx+si+disp16], [bx+disp16], etc.
            I_SET_BASE(flags, addr16_lut2[rm]);
            I_SET_INDEX(flags, addr16_lut2[rm | 8]);
            I_SET_SCALE(flags, 0);
            I_SET_SEG_BASE(flags, seg_prefix[addr16_lut2[rm | 16]]);
            i->disp32 = rw();
            break;
        case 24 ... 31: // mod=3
            if (is8 & 4) {
                I_SET_RM(flags, modrm & 7);
            } else if (is8 & 1) {
                I_SET_RM8(flags, modrm & 7);
            } else {
                I_SET_RMv(flags, modrm & 7);
            }
            break;
        }
    } else {
        int sib, index, base;
        switch (new_modrm) {
        case 0 ... 3:
        case 6 ... 7: // [eax], [ebx]
            I_SET_BASE(flags, rm);
            I_SET_INDEX(flags, EZR);
            I_SET_SCALE(flags, 0);
            I_SET_SEG_BASE(flags, seg_prefix[addr32_lut[rm]]);
            i->disp32 = 0;
            break;
        case 4: // [sib]
            sib = rb();
            index = sib >> 3 & 7;
            base = sib & 7;
            if (base == 5) {
                base = 0;
                I_SET_BASE(flags, EZR);
                i->disp32 = rd();
            } else {
                I_SET_BASE(flags, base);
                i->disp32 = 0;
            }
            if (index != 4) {
                I_SET_INDEX(flags, index);
                I_SET_SCALE(flags, sib >> 6);
            } else
                I_SET_INDEX(flags, EZR);
            I_SET_SEG_BASE(flags, seg_prefix[addr32_lut2[base]]);
            break;
        case 5: // [disp32]
            rm = modrm & 7;
            I_SET_BASE(flags, EZR);
            I_SET_INDEX(flags, EZR);
            I_SET_SCALE(flags, 0);
            I_SET_SEG_BASE(flags, seg_prefix[0]);
            i->disp32 = rd();
            break;
        case 0x08 ... 0x0B:
        case 0x0D ... 0x0F: // [eax+disp8s], [ebx+disp8s]
            rm = modrm & 7;
            I_SET_BASE(flags, rm);
            I_SET_INDEX(flags, EZR);
            I_SET_SCALE(flags, 0);
            I_SET_SEG_BASE(flags, seg_prefix[addr32_lut[rm]]);
            i->disp32 = rbs();
            break;
        case 0x0C: // [sib+disp8s]
            sib = rb();
            index = sib >> 3 & 7;
            base = sib & 7;
            I_SET_BASE(flags, base);
            if (index != 4) {
                I_SET_INDEX(flags, index);
                I_SET_SCALE(flags, sib >> 6);
            } else
                I_SET_INDEX(flags, EZR);
            I_SET_SEG_BASE(flags, seg_prefix[addr32_lut2[base]]);
            i->disp32 = rbs();
            break;
        case 0x10 ... 0x13:
        case 0x15 ... 0x17: // [eax+disp32], [ecx+disp32]
            rm = modrm & 7;
            I_SET_BASE(flags, rm);
            I_SET_INDEX(flags, EZR);
            I_SET_SCALE(flags, 0);
            I_SET_SEG_BASE(flags, seg_prefix[addr32_lut[rm]]);
            i->disp32 = rd();
            break;
        case 0x14: // [sib+disp32]
            sib = rb();
            index = sib >> 3 & 7;
            base = sib & 7;
            I_SET_BASE(flags, base);
            if (index != 4) {
                I_SET_INDEX(flags, index);
                I_SET_SCALE(flags, sib >> 6);
            } else
                I_SET_INDEX(flags, EZR);
            I_SET_SEG_BASE(flags, seg_prefix[addr32_lut2[base]]);
            i->disp32 = rd();
            break;
        case 24 ... 31: // reg3
            if (is8 & 4) {
                I_SET_RM(flags, modrm & 7);
            } else if (is8 & 1) {
                I_SET_RM8(flags, modrm & 7);
            } else {
                I_SET_RMv(flags, modrm & 7);
            }
            break;
        }
    }
    return flags;
}

static int swap_rm_reg(int flags)
{
    unsigned int x = 15 & ((flags >> 8) ^ (flags >> 12));
    return flags ^ ((x << 8) | (x << 12));
}

static int decode_invalid(struct decoded_instruction* i)
{
    UNUSED(i);
    rawp--;
    for (int i = 0; i < 16; i++)
        printf("%02x ", rawp[i - 16]);
    printf("\n");
    for (int i = 0; i < 16; i++)
        printf("%02x ", rawp[i]);
    printf("\n");
    CPU_LOG("Unknown opcode: %02x\n", rawp[0]);
    i->handler = op_ud_exception;
    i->flags = 0;
    return 1;
}
static int decode_invalid0F(struct decoded_instruction* i)
{
    UNUSED(i);
    rawp--;
    for (int i = 0; i < 16; i++)
        printf("%02x ", rawp[i - 16]);
    printf("\n");
    for (int i = 0; i < 16; i++)
        printf("%02x ", rawp[i]);
    printf("\n");
    CPU_LOG("Unknown opcode: 0F %02x\n", rawp[0]);
    i->handler = op_ud_exception;
    i->flags = 0;
    return 1;
}

static const decode_handler_t table0F[256];
static const decode_handler_t table[256];

static int decode_0F(struct decoded_instruction* i)
{
    return table0F[rb()](i);
}

// A variable set to see what the current SSE prefix is
enum {
    SSE_PREFIX_NONE = 0,
    SSE_PREFIX_66,
    SSE_PREFIX_F2,
    SSE_PREFIX_F3
};

static int sse_prefix = 0;
static int decode_prefix(struct decoded_instruction* i)
{
    uint8_t prefix = rawp[-1];
    int prefix_set = 0, return_value = 0;
    i->flags = 0;
    while (1) {
        switch (prefix) {
        case 0xF3: // repz
            sse_prefix = SSE_PREFIX_F3;
            if (!prefix_set)
                if (find_instruction_length(15) == -1)
                    goto error;
            i->flags |= I_PREFIX_REPZ;
            prefix_set |= 1;
            state_hash |= 4;
            break;
        case 0xF2: // repnz
            sse_prefix = SSE_PREFIX_F2;
            if (!prefix_set)
                if (find_instruction_length(15) == -1)
                    goto error;
            i->flags |= I_PREFIX_REPNZ;
            prefix_set |= 1;
            state_hash |= 4;
            break;
        case 0x66: // Operand size
            sse_prefix = SSE_PREFIX_66;
            if (!prefix_set)
                if (find_instruction_length(15) == -1)
                    goto error;
            if (!(prefix_set & 2)) // If prefix has not been set yet
                state_hash ^= STATE_CODE16;
            prefix_set |= 2;
            break;
        case 0x67: // Address size
            if (!prefix_set)
                if (find_instruction_length(15) == -1)
                    goto error;
            if (!(prefix_set & 4))
                state_hash ^= STATE_ADDR16;
            prefix_set |= 4;
            break;
        case 0xF0: // LOCK
            if (!prefix_set)
                if (find_instruction_length(15) == -1)
                    goto error;
            prefix_set |= 8;
            break;
        case 0x26:
        case 0x2E:
        case 0x36:
        case 0x3E: // Segment prefixes
            if (!prefix_set)
                if (find_instruction_length(15) == -1)
                    goto error;
            seg_prefix[0] = seg_prefix[1] = prefix >> 3 & 3;
            prefix_set |= 16;
            break;
        case 0x64:
        case 0x65: // Segment prefixes, part 2
            if (!prefix_set)
                if (find_instruction_length(15) == -1)
                    goto error;
            seg_prefix[0] = seg_prefix[1] = FS + (prefix & 1);
            prefix_set |= 16;
            break;
        case 0x0F:
            prefix_set |= 32;
            state_hash |= 4; // Set some random bit
            return_value = table0F[prefix = rb()](i);
            goto done;
        default:
            state_hash |= 4; // Set some random bit
            return_value = table[prefix](i);
            goto done;
        }
        prefix = rb();
    }
done:
    if (prefix_set > 0) {
        // Check for LOCK
        if (prefix_set & 8) {
            int valid;
            if (prefix_set & 32)
                valid = optable0F[prefix] & 0x80;
            else
                valid = optable[prefix] & 0x80;
            //CPU_LOG("LOCK opcode=%02x valid=%d\n", prefix, valid);
            if (!valid)
                goto error;
        }

        // Reset all state
        seg_prefix[0] = DS;
        seg_prefix[1] = SS;
        state_hash = cpu.state_hash;
    }
    sse_prefix = 0;
    return return_value;
error:
    sse_prefix = 0;
    i->handler = op_ud_exception;
    return 1;
}

static const insn_handler_t jcc32[16] = {
    op_jo32, // 0
    op_jno32,
    op_jb32, // 2
    op_jnb32,
    op_jz32, // 4
    op_jnz32,
    op_jbe32, // 6
    op_jnbe32,
    op_js32, // 8
    op_jns32,
    op_jp32, // 10
    op_jnp32,
    op_jl32, // 12
    op_jnl32,
    op_jle32, // 14
    op_jnle32
};
static const insn_handler_t jcc16[16] = {
    op_jo16, // 0
    op_jno16,
    op_jb16, // 2
    op_jnb16,
    op_jz16, // 4
    op_jnz16,
    op_jbe16, // 6
    op_jnbe16,
    op_js16, // 8
    op_jns16,
    op_jp16, // 10
    op_jnp16,
    op_jl16, // 12
    op_jnl16,
    op_jle16, // 14
    op_jnle16
};

static int decode_jcc8(struct decoded_instruction* i)
{
    i->flags = 0;
    int cond = rawp[-1] & 15;
    i->handler = SIZEOP(jcc16[cond], jcc32[cond]);
    i->imm32 = rbs();
    return 0;
}
static int decode_jccv(struct decoded_instruction* i)
{
    i->flags = 0;
    int cond = rawp[-1] & 15;
    i->handler = SIZEOP(jcc16[cond], jcc32[cond]);
    i->imm32 = rvs();
    return 0;
}
static int decode_cmov(struct decoded_instruction* i)
{
    uint8_t cond = rawp[-1] & 15, modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_cmov_r16e16, op_cmov_r32e32);
    else
        i->handler = SIZEOP(op_cmov_r16r16, op_cmov_r32r32);
    I_SET_OP(i->flags, cond);
    return 0;
}
static int decode_setcc(struct decoded_instruction* i)
{
    uint8_t cond = rawp[-1] & 15, modrm = rb();
    i->flags = parse_modrm(i, modrm, 1);
    if (modrm < 0xC0)
        i->handler = op_setcc_e8;
    else
        i->handler = op_setcc_r8;
    I_SET_OP(i->flags, cond);
    return 0;
}
static int decode_mov_rbib(struct decoded_instruction* i)
{
    int flags = 0;
    I_SET_RM8(flags, rawp[-1] & 7);
    i->flags = flags;
    i->handler = op_mov_r8i8;
    i->imm32 = rb();
    return 0;
}
static int decode_mov_rviv(struct decoded_instruction* i)
{
    int flags = 0;
    I_SET_RMv(flags, rawp[-1] & 7);
    i->flags = flags;
    i->handler = SIZEOP(op_mov_r16i16, op_mov_r32i32);
    i->imm32 = rv();
    return 0;
}
static int decode_push_rv(struct decoded_instruction* i)
{
    int flags = 0;
    I_SET_RMv(flags, rawp[-1] & 7);
    i->flags = flags;
    i->handler = SIZEOP(op_push_r16, op_push_r32);
    return 0;
}
static int decode_pop_rv(struct decoded_instruction* i)
{
    int flags = 0;
    I_SET_RMv(flags, rawp[-1] & 7);
    i->flags = flags;
    i->handler = SIZEOP(op_pop_r16, op_pop_r32);
    return 0;
}
static int decode_push_sv(struct decoded_instruction* i)
{
    int flags = 0;
    I_SET_RM(flags, rawp[-1] >> 3 & 3);
    i->flags = flags;
    i->handler = SIZEOP(op_push_s16, op_push_s32);
    return 0;
}
static int decode_pop_sv(struct decoded_instruction* i)
{
    int flags = 0;
    I_SET_RM(flags, rawp[-1] >> 3 & 3);
    i->flags = flags;
    i->handler = SIZEOP(op_pop_s16, op_pop_s32);
    return 0;
}
static int decode_inc_rv(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_inc_r16, op_inc_r32);
    i->flags = 0;
    I_SET_RMv(i->flags, rawp[-1] & 7);
    return 0;
}
static int decode_dec_rv(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_dec_r16, op_dec_r32);
    i->flags = 0;
    I_SET_RMv(i->flags, rawp[-1] & 7);
    return 0;
}
static int decode_fpu(struct decoded_instruction* i)
{
    uint8_t opcode = rawp[-1], modrm = rb();
    if (modrm < 0xC0) {
        i->flags = parse_modrm(i, modrm, 2);
        I_SET_OP(i->flags, state_hash & 1);
        i->handler = op_fpu_mem;
    } else {
        int flags = 0;
        I_SET_REG(flags, modrm >> 3 & 7);
        i->flags = flags;
        I_SET_OP(i->flags, state_hash & 1);
        i->handler = op_fpu_reg;
    }
    i->imm32 = (opcode << 8 & 0x700) | modrm; // FPU opcode as featured in Intel manual
    return 0;
}

static int decode_arith_00(struct decoded_instruction* i)
{
    int op = rawp[-1] >> 3 & 7;
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 1);
    I_SET_OP(flags, op);
    i->flags = flags;
    i->handler = REGOP(op_arith_e8r8, op_arith_r8r8);
    return 0;
}
static int decode_arith_01(struct decoded_instruction* i)
{
    int op = rawp[-1] >> 3 & 7;
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 0);
    I_SET_OP(flags, op);
    i->flags = flags;
    i->handler = REGOP(SIZEOP(op_arith_e16r16, op_arith_e32r32), SIZEOP(op_arith_r16r16, op_arith_r32r32));
    return 0;
}
static int decode_arith_02(struct decoded_instruction* i)
{
    int op = rawp[-1] >> 3 & 7;
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 1);
    I_SET_OP(flags, op);
    if (modrm < 0xC0) {
        i->flags = flags;
        i->handler = op_arith_r8e8;
    } else {
        i->flags = swap_rm_reg(flags);
        i->handler = op_arith_r8r8;
    }
    return 0;
}
static int decode_arith_03(struct decoded_instruction* i)
{
    int op = rawp[-1] >> 3 & 7;
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 0);
    I_SET_OP(flags, op);
    if (modrm < 0xC0) {
        i->flags = flags;
        i->handler = SIZEOP(op_arith_r16e16, op_arith_r32e32);
    } else {
        i->flags = swap_rm_reg(flags);
        i->handler = SIZEOP(op_arith_r16r16, op_arith_r32r32);
    }
    return 0;
}
static int decode_arith_04(struct decoded_instruction* i)
{
    i->flags = 0;
    I_SET_OP(i->flags, rawp[-1] >> 3 & 7);
    i->handler = op_arith_r8i8;
    i->imm8 = rb();
    return 0;
}
static int decode_arith_05(struct decoded_instruction* i)
{
    i->flags = 0;
    I_SET_OP(i->flags, rawp[-1] >> 3 & 7);
    i->handler = SIZEOP(op_arith_r16i16, op_arith_r32i32);
    i->imm32 = rv();
    return 0;
}

static int decode_xchg(struct decoded_instruction* i)
{
    i->flags = 0;
    // R/M is already implied to be zero
    I_SET_REGv(i->flags, rawp[-1] & 7);
    i->handler = SIZEOP(op_xchg_r16r16, op_xchg_r32r32);
    return 0;
}
static int decode_bswap(struct decoded_instruction* i)
{
    i->flags = 0;
    I_SET_RMv(i->flags, rawp[-1] & 7);
    i->handler = SIZEOP(op_bswap_r16, op_bswap_r32);
    return 0;
}

static int decode_ud(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_ud_exception;
    return 1;
}

static int decode_27(struct decoded_instruction* i)
{
    i->handler = op_daa;
    i->flags = 0;
    return 0;
}
static int decode_2F(struct decoded_instruction* i)
{
    i->handler = op_das;
    i->flags = 0;
    return 0;
}
static int decode_37(struct decoded_instruction* i)
{
    i->handler = op_aaa;
    i->flags = 0;
    return 0;
}
static int decode_3F(struct decoded_instruction* i)
{
    i->handler = op_aas;
    i->flags = 0;
    return 0;
}
static int decode_38(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 1);
    i->flags = flags;
    i->handler = REGOP(op_cmp_e8r8, op_cmp_r8r8);
    return 0;
}
static int decode_39(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 0);
    i->flags = flags;
    i->handler = REGOP(SIZEOP(op_cmp_e16r16, op_cmp_e32r32), SIZEOP(op_cmp_r16r16, op_cmp_r32r32));
    return 0;
}
static int decode_3A(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 1);
    if (modrm < 0xC0) {
        i->flags = flags;
        i->handler = op_cmp_r8e8;
    } else {
        i->flags = swap_rm_reg(flags);
        i->handler = op_cmp_r8r8;
    }
    return 0;
}
static int decode_3B(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0) {
        i->flags = flags;
        i->handler = SIZEOP(op_cmp_r16e16, op_cmp_r32e32);
    } else {
        i->flags = swap_rm_reg(flags);
        i->handler = SIZEOP(op_cmp_r16r16, op_cmp_r32r32);
    }
    return 0;
}
static int decode_3C(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_cmp_r8i8;
    i->imm8 = rb();
    return 0;
}
static int decode_3D(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = SIZEOP(op_cmp_r16i16, op_cmp_r32i32);
    i->imm32 = rv();
    return 0;
}

static int decode_60(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = SIZEOP(op_pusha, op_pushad);
    return 0;
}
static int decode_61(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = SIZEOP(op_popa, op_popad);
    return 0;
}
static int decode_62(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if(modrm >= 0xC0){
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    }
    i->flags = parse_modrm(i, modrm, 0);
    i->handler = SIZEOP(op_bound_r16e16, op_bound_r32e32);
    return 0;
}
static int decode_63(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    int old_state_hash = cpu.state_hash;
    state_hash |= STATE_CODE16;
    i->flags = parse_modrm(i, modrm, 0);
    state_hash = old_state_hash;
    if (modrm < 0xC0)
        i->handler = op_arpl_e16;
    else
        i->handler = op_arpl_r16;
    return 0;
}
// 64 -- 67 are prefixes
static int decode_68(struct decoded_instruction* i)
{
    i->imm32 = rv();
    i->handler = SIZEOP(op_push_i16, op_push_i32);
    i->flags = 0;
    return 0;
}
static int decode_69(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    i->handler = REGOP(SIZEOP(op_imul_r16e16i16, op_imul_r32e32i32), SIZEOP(op_imul_r16r16i16, op_imul_r32r32i32));
    i->imm32 = rvs();
    return 0;
}
static int decode_6A(struct decoded_instruction* i)
{
    i->imm32 = rbs();
    i->handler = SIZEOP(op_push_i16, op_push_i32);
    i->flags = 0;
    return 0;
}
static int decode_6B(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    i->handler = REGOP(SIZEOP(op_imul_r16e16i16, op_imul_r32e32i32), SIZEOP(op_imul_r16r16i16, op_imul_r32r32i32));
    i->imm32 = rbs();
    return 0;
}
static int decode_6C(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;

    i->handler = state_hash & STATE_ADDR16 ? op_insb16 : op_insb32;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    return 0;
}
static int decode_6D(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0; // Reset flags if no prefix
    static const insn_handler_t atbl[4] = {
        op_insd32, op_insw32, // STATE_CODE16 set, STATE_ADDR16 not set
        op_insd16, op_insw16 // STATE_CODE16 set, STATE_ADDR16 set
    };
    i->handler = atbl[state_hash & 3];
    return 0;
}
static int decode_6E(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    i->handler = state_hash & STATE_ADDR16 ? op_outsb16 : op_outsb32;
    return 0;
}
static int decode_6F(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0; // Reset flags if no prefix
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    static const insn_handler_t atbl[4] = {
        op_outsd32, op_outsw32, // STATE_CODE16 set, STATE_ADDR16 not set
        op_outsd16, op_outsw16 // STATE_CODE16 set, STATE_ADDR16 set
    };
    i->handler = atbl[state_hash & 3];
    return 0;
}
// 70 ~ 7F are jcc opcodes
static int decode_80(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 1);
    i->imm8 = rb();
    if ((modrm & 0x38) == 0x38) {
        i->handler = REGOP(op_cmp_e8i8, op_cmp_r8i8);
    } else {
        I_SET_OP(flags, modrm >> 3 & 7);
        i->handler = REGOP(op_arith_e8i8, op_arith_r8i8);
    }
    i->flags = flags;
    return 0;
}
static int decode_81(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 0);
    i->imm32 = rvs();
    if ((modrm & 0x38) == 0x38) {
        i->handler = SIZEOP(REGOP(op_cmp_e16i16, op_cmp_r16i16), REGOP(op_cmp_e32i32, op_cmp_r32i32));
    } else {
        I_SET_OP(flags, modrm >> 3 & 7);
        i->handler = SIZEOP(REGOP(op_arith_e16i16, op_arith_r16i16), REGOP(op_arith_e32i32, op_arith_r32i32));
    }
    i->flags = flags;
    return 0;
}
static int decode_83(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 0);
    i->imm32 = rbs();
    if ((modrm & 0x38) == 0x38) {
        i->handler = SIZEOP(REGOP(op_cmp_e16i16, op_cmp_r16i16), REGOP(op_cmp_e32i32, op_cmp_r32i32));
    } else {
        I_SET_OP(flags, modrm >> 3 & 7);
        i->handler = SIZEOP(REGOP(op_arith_e16i16, op_arith_r16i16), REGOP(op_arith_e32i32, op_arith_r32i32));
    }
    i->flags = flags;
    return 0;
}
static int decode_84(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 1);
    if (modrm < 0xC0)
        i->handler = op_test_e8r8;
    else
        i->handler = op_test_r8r8;
    return 0;
}
static int decode_85(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_test_e16r16, op_test_e32r32);
    else
        i->handler = SIZEOP(op_test_r16r16, op_test_r32r32);
    return 0;
}

static int decode_86(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 1);
    if (modrm < 0xC0)
        i->handler = op_xchg_r8e8;
    else
        i->handler = op_xchg_r8r8;
    return 0;
}
static int decode_87(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_xchg_r16e16, op_xchg_r32e32);
    else
        i->handler = SIZEOP(op_xchg_r16r16, op_xchg_r32r32);
    return 0;
}

static int decode_88(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 1);
    if (modrm < 0xC0)
        i->handler = op_mov_e8r8;
    else
        i->handler = op_mov_r8r8;
    return 0;
}
static int decode_89(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    i->handler = REGOP(SIZEOP(op_mov_e16r16, op_mov_e32r32), SIZEOP(op_mov_r16r16, op_mov_r32r32));
    return 0;
}
static int decode_8A(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 1);
    if (modrm < 0xC0)
        i->handler = op_mov_r8e8;
    else {
        flags = swap_rm_reg(flags);
        i->handler = op_mov_r8r8;
    }
    i->flags = flags;
    return 0;
}
static int decode_8B(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_mov_r16e16, op_mov_r32e32);
    else {
        flags = swap_rm_reg(flags);
        i->handler = SIZEOP(op_mov_r16r16, op_mov_r32r32);
    }
    i->flags = flags;
    return 0;
}
static int decode_8C(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 2);
    if (modrm < 0xC0)
        i->handler = op_mov_e16s16;
    else
        i->handler = SIZEOP(op_mov_r16s16, op_mov_r32s16);
    return 0;
}
static int decode_8D(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm >= 0xC0) {
        i->handler = op_ud_exception;
        i->flags = 0;
        return 1;
    }
    i->flags = parse_modrm(i, modrm, 0);
    i->handler = SIZEOP(op_lea_r16e16, op_lea_r32e32);
    return 0;
}
static int decode_8E(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    int old_state_hash = cpu.state_hash;
    state_hash |= STATE_CODE16;
    i->flags = parse_modrm(i, modrm, 2);
    state_hash = old_state_hash;
    if (modrm < 0xC0)
        i->handler = op_mov_s16e16;
    else
        i->handler = op_mov_s16r16;
    return 0;
}
static int decode_8F(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm >= 0xC0) {
        i->flags = parse_modrm(i, modrm, 0);
        i->handler = SIZEOP(op_pop_r16, op_pop_r32);
    } else {
        i->flags = parse_modrm(i, modrm, 0);
        i->handler = SIZEOP(op_pop_e16, op_pop_e32);
    }
    return 0;
}
static int decode_90(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_nop;
    return 0;
}

static int decode_98(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = SIZEOP(op_cbw, op_cwde);
    return 0;
}
static int decode_99(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = SIZEOP(op_cwd, op_cdq);
    return 0;
}
static int decode_9A(struct decoded_instruction* i)
{
    // Far call
    i->handler = SIZEOP(op_callf16_ap, op_callf32_ap);
    i->imm32 = rv();
    i->disp16 = rw();
    i->flags = 0;
    return 1;
}
static int decode_9B(struct decoded_instruction* i)
{
    i->handler = op_fwait;
    i->flags = 0;
    return 0;
}
static int decode_9C(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = SIZEOP(op_pushf, op_pushfd);
    return 0;
}
static int decode_9D(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = SIZEOP(op_popf, op_popfd);
    return 0;
}
static int decode_9E(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_sahf;
    return 0;
}
static int decode_9F(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_lahf;
    return 0;
}

static int decode_A0(struct decoded_instruction* i)
{
    i->handler = op_mov_alm8;
    i->imm32 = state_hash & STATE_ADDR16 ? rw() : rd();
    i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    return 0;
}
static int decode_A1(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_mov_axm16, op_mov_eaxm32);
    i->imm32 = state_hash & STATE_ADDR16 ? rw() : rd();
    i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    return 0;
}
static int decode_A2(struct decoded_instruction* i)
{
    i->handler = op_mov_m8al;
    i->imm32 = state_hash & STATE_ADDR16 ? rw() : rd();
    i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    return 0;
}
static int decode_A3(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_mov_m16ax, op_mov_m32eax);
    i->imm32 = state_hash & STATE_ADDR16 ? rw() : rd();
    i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    return 0;
}

static int decode_A4(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    i->handler = state_hash & STATE_ADDR16 ? op_movsb16 : op_movsb32;
    return 0;
}
static int decode_A5(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    switch (state_hash & 3) {
    case 0: // 32 bit address, 32-bit data
        i->handler = op_movsd32;
        break;
    case STATE_CODE16: // 32 bit address, 16-bit data
        i->handler = op_movsw32;
        break;
    case STATE_ADDR16: // 16 bit address, 32-bit data
        i->handler = op_movsd16;
        break;
    case STATE_ADDR16 | STATE_CODE16: // 16 bit address, 16-bit data
        i->handler = op_movsw16;
        break;
    }
    return 0;
}

static int decode_A6(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    i->handler = state_hash & STATE_ADDR16 ? op_cmpsb16 : op_cmpsb32;
    return 0;
}
static int decode_A7(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    switch (state_hash & 3) {
    case 0: // 32 bit address, 32-bit data
        i->handler = op_cmpsd32;
        break;
    case STATE_CODE16: // 32 bit address, 16-bit data
        i->handler = op_cmpsw32;
        break;
    case STATE_ADDR16: // 16 bit address, 32-bit data
        i->handler = op_cmpsd16;
        break;
    case STATE_ADDR16 | STATE_CODE16: // 16 bit address, 16-bit data
        i->handler = op_cmpsw16;
        break;
    }
    return 0;
}

static int decode_A8(struct decoded_instruction* i)
{
    i->handler = op_test_r8i8;
    i->flags = 0; // Set R/M to 0
    i->imm8 = rb();
    return 0;
}
static int decode_A9(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_test_r16i16, op_test_r32i32);
    i->flags = 0;
    i->imm32 = rv();
    return 0;
}

static int decode_AA(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;
    //if(PTR_TO_PHYS(rawp) == 0x108796)__asm__("int3");
    i->handler = state_hash & STATE_ADDR16 ? op_stosb16 : op_stosb32;
    return 0;
}
static int decode_AB(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;
    switch (state_hash & 3) {
    case 0: // 32 bit address, 32-bit data
        i->handler = op_stosd32;
        break;
    case STATE_CODE16: // 32 bit address, 16-bit data
        i->handler = op_stosw32;
        break;
    case STATE_ADDR16: // 16 bit address, 32-bit data
        i->handler = op_stosd16;
        break;
    case STATE_ADDR16 | STATE_CODE16: // 16 bit address, 16-bit data
        i->handler = op_stosw16;
        break;
    }
    return 0;
}
static int decode_AC(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    i->handler = state_hash & STATE_ADDR16 ? op_lodsb16 : op_lodsb32;
    return 0;
}
static int decode_AD(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    switch (state_hash & 3) {
    case 0: // 32 bit address, 32-bit data
        i->handler = op_lodsd32;
        break;
    case STATE_CODE16: // 32 bit address, 16-bit data
        i->handler = op_lodsw32;
        break;
    case STATE_ADDR16: // 16 bit address, 32-bit data
        i->handler = op_lodsd16;
        break;
    case STATE_ADDR16 | STATE_CODE16: // 16 bit address, 16-bit data
        i->handler = op_lodsw16;
        break;
    }
    return 0;
}
static int decode_AE(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;
    i->handler = state_hash & STATE_ADDR16 ? op_scasb16 : op_scasb32;
    return 0;
}
static int decode_AF(struct decoded_instruction* i)
{
    if (!(state_hash & 4))
        i->flags = 0;
    switch (state_hash & 3) {
    case 0:
        i->handler = op_scasd32;
        break;
    case STATE_CODE16:
        i->handler = op_scasw32;
        break;
    case STATE_ADDR16:
        i->handler = op_scasd16;
        break;
    case STATE_ADDR16 | STATE_CODE16:
        i->handler = op_scasw16;
        break;
    }
    return 0;
}

static int decode_C0(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 1);
    I_SET_OP(i->flags, modrm >> 3 & 7);
    if (modrm < 0xC0)
        i->handler = op_shift_e8i8;
    else
        i->handler = op_shift_r8i8;
    i->imm8 = rb();
    return 0;
}
static int decode_C1(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    I_SET_OP(i->flags, modrm >> 3 & 7);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_shift_e16i16, op_shift_e32i32);
    else
        i->handler = SIZEOP(op_shift_r16i16, op_shift_r32i32);
    i->imm8 = rb();
    return 0;
}
static int decode_C2(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_ret16_iw, op_ret32_iw);
    i->imm16 = rw();
    i->flags = 0;
    return 1;
}
static int decode_C3(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_ret16, op_ret32);
    i->flags = 0;
    return 1;
}
static int decode_C4(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm >= 0xC0) {
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    } else {
        i->flags = parse_modrm(i, modrm, 0);
        i->handler = SIZEOP(op_les_r16e16, op_les_r32e32);
    }
    return 0;
}
static int decode_C5(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm >= 0xC0) {
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    } else {
        i->flags = parse_modrm(i, modrm, 0);
        i->handler = SIZEOP(op_lds_r16e16, op_lds_r32e32);
    }
    return 0;
}
static int decode_C6(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if(modrm >> 3 & 7) {
        // TODO: RTX instructions
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    }
    i->flags = parse_modrm(i, modrm, 1);
    if (modrm >= 0xC0)
        i->handler = op_mov_r8i8;
    else
        i->handler = op_mov_e8i8;
    i->imm8 = rb();
    return 0;
}
static int decode_C7(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if(modrm >> 3 & 7) {
        // TODO: RTX instructions
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    }
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm >= 0xC0)
        i->handler = SIZEOP(op_mov_r16i16, op_mov_r32i32);
    else
        i->handler = SIZEOP(op_mov_e16i16, op_mov_e32i32);
    i->imm32 = rv();
    return 0;
}
static int decode_C8(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = SIZEOP(op_enter16, op_enter32);
    i->imm16 = rw();
    i->disp8 = rb();
    return 0;
}
static int decode_C9(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = SIZEOP(op_leave16, op_leave32);
    return 0;
}
static int decode_CA(struct decoded_instruction* i)
{
    i->flags = 0;
    i->imm16 = rw();
    i->handler = SIZEOP(op_retf16, op_retf32);
    return 1;
}
static int decode_CB(struct decoded_instruction* i)
{
    i->flags = 0;
    i->imm16 = 0;
    i->handler = SIZEOP(op_retf16, op_retf32);
    return 1;
}
static int decode_CC(struct decoded_instruction* i)
{
    i->flags = 0;
    i->imm8 = 3;
    i->handler = op_int;
    return 1;
}
static int decode_CD(struct decoded_instruction* i)
{
    i->flags = 0;
    i->imm8 = rb();
    i->handler = op_int;
    return 1;
}
static int decode_CE(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_into;
    return 0;
}
static int decode_CF(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = SIZEOP(op_iret16, op_iret32);
    return 1;
}

static int decode_D0(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 1);
    I_SET_OP(i->flags, modrm >> 3 & 7);
    if (modrm < 0xC0)
        i->handler = op_shift_e8i8;
    else
        i->handler = op_shift_r8i8;
    i->imm8 = 1;
    return 0;
}
static int decode_D1(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    I_SET_OP(i->flags, modrm >> 3 & 7);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_shift_e16i16, op_shift_e32i32);
    else
        i->handler = SIZEOP(op_shift_r16i16, op_shift_r32i32);
    i->imm8 = 1;
    return 0;
}
static int decode_D2(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 1);
    I_SET_OP(i->flags, modrm >> 3 & 7);
    if (modrm < 0xC0)
        i->handler = op_shift_e8cl;
    else
        i->handler = op_shift_r8cl;
    i->imm8 = 1;
    return 0;
}
static int decode_D3(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    I_SET_OP(i->flags, modrm >> 3 & 7);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_shift_e16cl, op_shift_e32cl);
    else
        i->handler = SIZEOP(op_shift_r16cl, op_shift_r32cl);
    return 0;
}
static int decode_D4(struct decoded_instruction* i)
{
    i->flags = 0;
    i->imm8 = rb();
    i->handler = op_aam;
    return 0;
}
static int decode_D5(struct decoded_instruction* i)
{
    i->flags = 0;
    i->imm8 = rb();
    i->handler = op_aad;
    return 0;
}
static int decode_D7(struct decoded_instruction* i)
{
    i->flags = 0;
    I_SET_SEG_BASE(i->flags, seg_prefix[0]);
    i->handler = (state_hash & STATE_ADDR16) ? op_xlat16 : op_xlat32;
    return 0;
}

static int decode_E0(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_loopnz_rel16, op_loopnz_rel32);
    i->flags = 0;
    i->disp32 = state_hash & STATE_ADDR16 ? 0xFFFF : -1;
    i->imm32 = rbs();
    return 0;
}
static int decode_E1(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_loopz_rel16, op_loopz_rel32);
    i->flags = 0;
    i->disp32 = state_hash & STATE_ADDR16 ? 0xFFFF : -1;
    i->imm32 = rbs();
    return 0;
}
static int decode_E2(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_loop_rel16, op_loop_rel32);
    i->flags = 0;
    i->disp32 = state_hash & STATE_ADDR16 ? 0xFFFF : -1;
    i->imm32 = rbs();
    return 0;
}
static int decode_E3(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_jecxz_rel16, op_jecxz_rel32);
    i->disp32 = state_hash & STATE_ADDR16 ? 0xFFFF : -1;
    i->flags = 0;
    i->imm32 = rbs();
    return 0;
}
static int decode_E4(struct decoded_instruction* i)
{
    i->handler = op_in_i8al;
    i->flags = 0;
    i->imm8 = rb();
    return 0;
}
static int decode_E5(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_in_i8ax, op_in_i8eax);
    i->flags = 0;
    i->imm8 = rb();
    return 0;
}
static int decode_E6(struct decoded_instruction* i)
{
    i->handler = op_out_i8al;
    i->flags = 0;
    i->imm8 = rb();
    return 0;
}
static int decode_E7(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_out_i8ax, op_out_i8eax);
    i->flags = 0;
    i->imm8 = rb();
    return 0;
}
static int decode_E8(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_call_j16, op_call_j32);
    i->flags = 0;
    i->imm32 = rvs();
    return 1;
}
static int decode_E9(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_jmp_rel16, op_jmp_rel32);
    i->flags = 0;
    i->imm32 = rvs();
    return 1;
}
static int decode_EA(struct decoded_instruction* i)
{
    // Far jump
    i->handler = op_jmpf;
    i->imm32 = rv();
    i->disp16 = rw();
    i->flags = 0;
    return 1;
}
static int decode_EB(struct decoded_instruction* i)
{
    // Far jump
    i->handler = SIZEOP(op_jmp_rel16, op_jmp_rel32);
    i->imm32 = rbs();
    i->flags = 0;
    return 1;
}
static int decode_EC(struct decoded_instruction* i)
{
    i->handler = op_in_dxal;
    i->flags = 0;
    return 0;
}
static int decode_ED(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_in_dxax, op_in_dxeax);
    i->flags = 0;
    return 0;
}
static int decode_EE(struct decoded_instruction* i)
{
    i->handler = op_out_dxal;
    i->flags = 0;
    return 0;
}
static int decode_EF(struct decoded_instruction* i)
{
    i->handler = SIZEOP(op_out_dxax, op_out_dxeax);
    i->flags = 0;
    return 0;
}

static int decode_F4(struct decoded_instruction* i)
{
    i->handler = op_hlt;
    i->flags = 0;
    return 1;
}
static int decode_F5(struct decoded_instruction* i)
{
    i->handler = op_cmc;
    i->flags = 0;
    return 0;
}
static int decode_F6(struct decoded_instruction* i)
{
    uint8_t modrm = rb(), reg = modrm >> 3 & 7;
    i->flags = parse_modrm(i, modrm, 1);
    if (modrm < 0xC0)
        switch (modrm >> 3 & 7) {
        case 0:
        case 1:
            i->handler = op_test_e8i8;
            i->imm8 = rb();
            break;
        case 2:
            i->handler = op_not_e8;
            break;
        case 3:
            i->handler = op_neg_e8;
            break;
        default:
            I_SET_OP(i->flags, reg);
            i->handler = op_muldiv_e8;
            break;
        }
    else
        switch (modrm >> 3 & 7) {
        case 0:
        case 1:
            i->handler = op_test_r8i8;
            i->imm8 = rb();
            break;
        case 2:
            i->handler = op_not_r8;
            break;
        case 3:
            i->handler = op_neg_r8;
            break;
        default:
            I_SET_OP(i->flags, reg);
            i->handler = op_muldiv_r8;
            break;
        }
    return 0;
}

static int decode_F7(struct decoded_instruction* i)
{
    uint8_t modrm = rb(), reg = modrm >> 3 & 7;
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        switch (modrm >> 3 & 7) {
        case 0:
        case 1:
            i->handler = SIZEOP(op_test_e16i16, op_test_e32i32);
            i->imm32 = rv();
            break;
        case 2:
            i->handler = SIZEOP(op_not_e16, op_not_e32);
            break;
        case 3:
            i->handler = SIZEOP(op_neg_e16, op_neg_e32);
            break;
        default:
            I_SET_OP(i->flags, reg);
            i->handler = SIZEOP(op_muldiv_e16, op_muldiv_e32);
            break;
        }
    else
        switch (modrm >> 3 & 7) {
        case 0:
        case 1:
            i->handler = SIZEOP(op_test_r16i16, op_test_r32i32);
            i->imm32 = rv();
            break;
        case 2:
            i->handler = SIZEOP(op_not_r16, op_not_r32);
            break;
        case 3:
            i->handler = SIZEOP(op_neg_r16, op_neg_r32);
            break;
        default:
            I_SET_OP(i->flags, reg);
            i->handler = SIZEOP(op_muldiv_r16, op_muldiv_r32);
            break;
        }
    return 0;
}
static int decode_F8(struct decoded_instruction* i)
{
    i->handler = op_clc;
    i->flags = 0;
    return 0;
}
static int decode_F9(struct decoded_instruction* i)
{
    i->handler = op_stc;
    i->flags = 0;
    return 0;
}

static int decode_FA(struct decoded_instruction* i)
{
    i->handler = op_cli;
    i->flags = 0;
    return 0;
}
static int decode_FB(struct decoded_instruction* i)
{
    i->handler = op_sti;
    i->flags = 0;
    return 0;
}
static int decode_FC(struct decoded_instruction* i)
{
    i->handler = op_cld;
    i->flags = 0;
    return 0;
}
static int decode_FD(struct decoded_instruction* i)
{
    i->handler = op_std;
    i->flags = 0;
    return 0;
}

static int decode_FE(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 1);
    if (modrm < 0xC0) // MOD != 3
        switch (modrm >> 3 & 7) {
        case 0:
            i->handler = op_inc_e8;
            break;
        case 1:
            i->handler = op_dec_e8;
            break;
        default:
            i->handler = op_ud_exception;
            return 1;
        }
    else
        switch (modrm >> 3 & 7) {
        case 0:
            i->handler = op_inc_r8;
            break;
        case 1:
            i->handler = op_dec_r8;
            break;
        default:
            i->handler = op_ud_exception;
            return 1;
        }
    return 0;
}
static int decode_FF(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0) // MOD != 3
        switch (modrm >> 3 & 7) {
        case 0:
            i->handler = SIZEOP(op_inc_e16, op_inc_e32);
            return 0;
        case 1:
            i->handler = SIZEOP(op_dec_e16, op_dec_e32);
            return 0;
        case 2:
            i->handler = SIZEOP(op_call_e16, op_call_e32);
            return 1;
        case 3:
            i->handler = SIZEOP(op_callf_e16, op_callf_e32);
            return 1;
        case 4:
            i->handler = SIZEOP(op_jmp_e16, op_jmp_e32);
            return 1;
        case 5:
            i->handler = SIZEOP(op_jmpf_e16, op_jmpf_e32);
            return 1;
        case 6:
            i->handler = SIZEOP(op_push_e16, op_push_e32);
            return 0;
        case 7:
            i->handler = op_ud_exception;
            return 1;
        }
    else
        switch (modrm >> 3 & 7) {
        case 0:
            i->handler = SIZEOP(op_inc_r16, op_inc_r32);
            return 0;
        case 1:
            i->handler = SIZEOP(op_dec_r16, op_dec_r32);
            return 0;
        case 2:
            i->handler = SIZEOP(op_call_r16, op_call_r32);
            return 1;
        case 4:
            i->handler = SIZEOP(op_jmp_r16, op_jmp_r32);
            return 1;
        case 6:
            i->handler = SIZEOP(op_push_r16, op_push_r32);
            return 0;
        case 3: // callf
        case 5: // jmpf
            i->handler = op_ud_exception;
            return 1;
        case 7:
            i->handler = op_ud_exception;
            return 1;
        }
    CPU_FATAL("unreachable");
}

static int decode_0F00(struct decoded_instruction* i)
{
    uint8_t modrm = rb(), reg = modrm >> 3 & 7;
    if ((modrm & 48) == 32) {
        // Handle VERR/VERW
        int old_state_hash = cpu.state_hash;
        state_hash |= STATE_CODE16;
        i->flags = parse_modrm(i, modrm, 0);
        state_hash = old_state_hash;
        if (modrm & 8) {
            // VERW
            i->handler = modrm < 0xC0 ? op_verw_e16 : op_verw_r16;
        } else {
            // VERR
            i->handler = modrm < 0xC0 ? op_verr_e16 : op_verr_r16;
        }
        return 0;
    }
    i->flags = parse_modrm(i, modrm, 6); // 32-bit R/M + Accurate REG
    if (modrm < 0xC0) {
        switch (reg) {
        case 0:
        case 1:
            i->imm8 = reg == 0 ? SEG_LDTR : SEG_TR;
            i->handler = op_str_sldt_e16;
            break;
        case 2:
            i->handler = op_lldt_e16;
            break;
        case 3:
            i->handler = op_ltr_e16;
            break;
        default:
            CPU_FATAL("Unknown opcode 0F 00 /%d\n", reg);
        }
    } else {
        switch (reg) {
        case 0:
        case 1:
            i->imm8 = reg == 0 ? SEG_LDTR : SEG_TR;
            i->disp32 = state_hash & STATE_CODE16 ? 0xFFFF : -1;
            i->handler = op_str_sldt_r16;
            break;
        case 2:
            i->handler = op_lldt_r16;
            break;
        case 3:
            i->handler = op_ltr_r16;
            break;
        default:
            CPU_FATAL("Unknown opcode 0F 00 /%d\n", reg);
        }
    }
    return 0;
}
static int decode_0F01(struct decoded_instruction* i)
{
    uint8_t modrm = rb(), reg = modrm >> 3 & 7;
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0) {
        switch (reg) {
        case 0:
            i->handler = op_sgdt_e32;
            break;
        case 1:
            i->handler = op_sidt_e32;
            break;
        case 2:
            i->handler = SIZEOP(op_lgdt_e16, op_lgdt_e32);
            break;
        case 3:
            i->handler = SIZEOP(op_lidt_e16, op_lidt_e32);
            break;
        case 4:
            i->handler = op_smsw_e16;
            break;
        case 5: // Note: No such opcode as 0F 01 /5
            i->handler = op_ud_exception;
            return 1;
        case 6:
            i->handler = op_lmsw_e16;
            break;
        case 7:
            i->handler = op_invlpg_e8;
            break;
        }
    } else {
        int lmsw_temp;
        switch (reg) {
        case 4:
            i->handler = SIZEOP(op_smsw_r16, op_smsw_r32);
            break;
        case 1:
            i->flags = 0;
            i->handler = op_nop;
            break;
        case 0:
        case 2:
        case 3:
        case 5:
        case 7:
            i->handler = op_ud_exception;
            return 1;
        case 6:
            lmsw_temp = I_RM(i->flags);
            i->flags &= ~(0xF << I_RM_SHIFT); // XXX extra hacky
            I_SET_RM(i->flags, lmsw_temp << 1); // Make it into a 16-bit register
            i->handler = op_lmsw_r16;
            break;
        }
    }
    return 0;
}

static int decode_0F02(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_lar_r16e16, op_lar_r32e32);
    else
        i->handler = SIZEOP(op_lar_r16r16, op_lar_r32r32);
    return 0;
}
static int decode_0F03(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_lsl_r16e16, op_lsl_r32e32);
    else
        i->handler = SIZEOP(op_lsl_r16r16, op_lsl_r32r32);
    return 0;
}

static int decode_0F06(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_clts;
    return 0;
}
static int decode_0F09(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_wbinvd;
    return 0;
}
static int decode_0F0B(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_ud_exception;
    return 1;
}

static const int decode_sse10_17_tbl[8 * 4] = {
    MOVUPS_XGoXEo, // 0F 10
    MOVUPS_XGoXEo, // 66 0F 10
    MOVSD_XGqXEq, // F2 0F 10
    MOVSS_XGdXEd, // F3 0F 10

    MOVUPS_XEoXGo, // 0F 11
    MOVUPS_XEoXGo, // 66 0F 11
    MOVSD_XEqXGq, // F2 0F 11
    MOVSS_XEdXGd, // F3 0F 11

    MOVHLPS_XGqXEq, // 0F 12 (note: can be changed to MOVLPS)
    MOVLPS_XGqXEq, // 66 0F 12
    MOVHLPS_XGqXEq, // F2 0F 12 - TODO: SSE3
    MOVHLPS_XGqXEq, // F3 0F 12 - TODO: SSE3

    // Technically MOVLPS, but it has the same functionality as MOVSD.
    MOVSD_XEqXGq, // 0F 13
    MOVSD_XEqXGq, // 66 0F 13
    MOVSD_XEqXGq, // F2 0F 13 - invalid
    MOVSD_XEqXGq, // F3 0F 13 - invalid

    UNPCKLPS_XGoXEq, // 0F 14
    UNPCKLPD_XGoXEo, // 66 0F 14
    UNPCKLPS_XGoXEq, // F2 0F 14 - invalid
    UNPCKLPS_XGoXEq, // F3 0F 14 - invalid

    UNPCKHPS_XGoXEq, // 0F 15
    UNPCKHPD_XGoXEo, // 66 0F 15
    UNPCKHPS_XGoXEq, // F2 0F 15 - invalid
    UNPCKHPS_XGoXEq, // F3 0F 15 - invalid

    MOVHPS_XGqXEq, // 0F 16
    MOVHPS_XGqXEq, // 66 0F 16
    MOVHPS_XGqXEq, // F2 0F 16 - invalid
    MOVSHDUP_XGoXEo, // F3 0F 16

    MOVHPS_XEqXGq, // 0F 17
    MOVHPS_XEqXGq, // 66 0F 17
    MOVHPS_XEqXGq, // F2 0F 17 - invalid
    MOVHPS_XEqXGq // F3 0F 17 - invalid
};
static int decode_sse10_17(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_10_17;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_sse10_17_tbl[opcode << 2 | sse_prefix];
    return 0;
}
static int decode_0F18(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    parse_modrm(i, modrm, 0); // We're just parsing ModR/M to find how many bytes to skip
    i->flags = 0;
    i->handler = op_prefetchh;
    return 0;
}

static int decode_0F1F(struct decoded_instruction* i){
    uint8_t modrm = rb();
    parse_modrm(i, modrm, 0);
    i->flags = 0;
    i->handler = op_nop;
    return 0;
}

static int decode_0F20(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm < 0xC0) {
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    } else {
        int flags = 0;
        I_SET_REG(flags, modrm >> 3 & 7);
        I_SET_RM(flags, modrm & 7);
        i->flags = flags;
        i->handler = op_mov_r32cr;
    }
    // End the trace here since we might be flushing the TLB
    return 1;
}
static int decode_0F21(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm < 0xC0) {
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    } else {
        int flags = 0;
        I_SET_REG(flags, modrm >> 3 & 7);
        I_SET_RM(flags, modrm & 7);
        i->flags = flags;
        i->handler = op_mov_r32dr;
    }
    return 0;
}
static int decode_0F22(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm < 0xC0) {
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    } else {
        int flags = 0;
        I_SET_REG(flags, modrm >> 3 & 7);
        I_SET_RM(flags, modrm & 7);
        i->flags = flags;
        i->handler = op_mov_crr32;
    }
    return 1;
}
static int decode_0F23(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm < 0xC0) {
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    } else {
        int flags = 0;
        I_SET_REG(flags, modrm >> 3 & 7);
        I_SET_RM(flags, modrm & 7);
        i->flags = flags;
        i->handler = op_mov_drr32;
    }
    return 0;
}

static const int decode_sse28_2F_tbl[8 * 4] = {
    MOVAPS_XGoXEo, // 0F 28
    MOVAPS_XGoXEo, // 66 0F 28
    MOVAPS_XGoXEo, // F2 0F 28 - invalid
    MOVAPS_XGoXEo, // F3 0F 28 - invalid

    MOVAPS_XEoXGo, // 0F 29
    MOVAPS_XEoXGo, // 66 0F 29
    MOVAPS_XEoXGo, // F2 0F 29 - invalid
    MOVAPS_XEoXGo, // F3 0F 29 - invalid

    CVTPI2PS_XGqMEq, // 0F 2A
    CVTPI2PD_XGoMEq, // 66 0F 2A
    CVTSI2SD_XGqMEd, // F2 0F 2A - invalid
    CVTSI2SS_XGdEd, // F3 0F 2A - invalid

    // Note: MOVNTPS/MOVNTPD are identical to MOVAPS/MOVAPD on the surface
    MOVAPS_XEoXGo, // 0F 2B
    MOVAPS_XEoXGo, // 66 0F 2B
    MOVAPS_XEoXGo, // F2 0F 2B - invalid
    MOVAPS_XEoXGo, // F3 0F 2B - invalid

    // Note: CVTT* (convert with truncate) are handled in the same opcode
    CVTPS2PI_MGqXEq, // 0F 2C
    CVTPD2PI_MGqXEo, // 66 0F 2C
    CVTSD2SI_GdXEq, // F2 0F 2C
    CVTSS2SI_GdXEd, // F3 0F 2C

    CVTPS2PI_MGqXEq, // 0F 2D
    CVTPD2PI_MGqXEo, // 66 0F 2D
    CVTSD2SI_GdXEq, // F2 0F 2D
    CVTSS2SI_GdXEd, // F3 0F 2D

    UCOMISS_XGdXEd, // 0F 2E
    UCOMISD_XGqXEq, // 66 0F 2E
    UCOMISS_XGdXEd, // F2 0F 2E - invalid
    UCOMISS_XGdXEd, // F3 0F 2E - invalid

    // Ordered compares are handled in the same opcode
    UCOMISS_XGdXEd, // 0F 2F
    UCOMISD_XGqXEq, // 66 0F 2F
    UCOMISS_XGdXEd, // F2 0F 2F - invalid
    UCOMISS_XGdXEd, // F3 0F 2F - invalid
};
static int decode_sse28_2F(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_28_2F;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_sse28_2F_tbl[opcode << 2 | sse_prefix] | ((opcode & 1) << 4);
    return 0;
}

static int decode_0F30(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_wrmsr;
    return 0;
}
static int decode_0F31(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_rdtsc;
    return 0;
}
static int decode_0F32(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_rdmsr;
    return 0;
}

static int decode_0F38(struct decoded_instruction* i)
{
    // Opcode ordering:
    //  [prefixes] 0F 38 <minor opcode> <modr/m> [sib] [disp]
    // The minor opcode we put in imm8
    i->imm8 = rb();

    uint8_t modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    if(sse_prefix == SSE_PREFIX_66) i->handler = op_sse_6638;
    else i->handler = op_sse_38;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    return 0;
}

static int decode_sysenter_sysexit(struct decoded_instruction* i) // 0F34, 0F35
{
    i->flags = 0;
    i->handler = rawp[-1] & 1 ? op_sysexit : op_sysenter;
    return 0;
}

static const int decode_sse50_57_tbl[8 * 4] = {
    MOVMSKPS_GdXEo, // 0F 50
    MOVMSKPD_GdXEo, // 66 0F 50
    MOVMSKPS_GdXEo, // F2 0F 50 - invalid
    MOVMSKPS_GdXEo, // F3 0F 50 - invalid

    SQRTPS_XGoXEo, // 0F 51
    SQRTPD_XGoXEo, // 66 0F 51
    SQRTSD_XGqXEq, // F2 0F 51
    SQRTSS_XGdXEd, // F3 0F 51

    RSQRTPS_XGoXEo, // 0F 52
    RSQRTSS_XGdXEd, // 66 0F 52
    RSQRTPS_XGoXEo, // F2 0F 52 - invalid
    RSQRTPS_XGoXEo, // F3 0F 52 - invalid

    RCPPS_XGoXEo, // 0F 53
    RCPSS_XGdXEd, // 66 0F 53
    RCPPS_XGoXEo, // F2 0F 53 - invalid
    RCPPS_XGoXEo, // F3 0F 53 - invalid

    ANDPS_XGoXEo, // 0F 54
    ANDPS_XGoXEo, // 66 0F 54
    ANDPS_XGoXEo, // F2 0F 54 - invalid
    ANDPS_XGoXEo, // F3 0F 54 - invalid

    ANDNPS_XGoXEo, // 0F 55
    ANDNPS_XGoXEo, // 66 0F 55
    ANDNPS_XGoXEo, // F2 0F 55 - invalid
    ANDNPS_XGoXEo, // F3 0F 55 - invalid

    ORPS_XGoXEo, // 0F 56
    ORPS_XGoXEo, // 66 0F 56
    ORPS_XGoXEo, // F2 0F 56 - invalid
    ORPS_XGoXEo, // F3 0F 56 - invalid

    XORPS_XGoXEo, // 0F 57
    XORPS_XGoXEo, // 66 0F 57
    XORPS_XGoXEo, // F2 0F 57 - invalid
    XORPS_XGoXEo // F3 0F 57 - invalid
};
static int decode_sse50_57(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_50_57;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_sse50_57_tbl[opcode << 2 | sse_prefix] | ((opcode & 1) << 4);
    return 0;
}
static const int decode_sse58_5F_tbl[8 * 4] = {
    ADDPS_XGoXEo, // 0F 58
    ADDPD_XGoXEo, // 66 0F 58
    ADDSD_XGqXEq, // F2 0F 58
    ADDSS_XGdXEd, // F3 0F 58
    
    MULPS_XGoXEo, // 0F 59
    MULPD_XGoXEo, // 66 0F 59
    MULSD_XGqXEq, // F2 0F 59
    MULSS_XGdXEd, // F3 0F 59
    
    CVTPS2PD_XGoXEo, // 0F 5A
    CVTPD2PS_XGoXEo, // 66 0F 5A
    CVTSD2SS_XGoXEq, // F2 0F 5A
    CVTSS2SD_XGoXEd, // F3 0F 5A
    
    CVTDQ2PS_XGoXEo, // 0F 5B
    CVTPS2DQ_XGoXEo, // 66 0F 5B
    CVTDQ2PS_XGoXEo, // F2 0F 5B - invalid
    CVTTPS2DQ_XGoXEo, // F3 0F 5B

    SUBPS_XGoXEo, // 0F 5C
    SUBPD_XGoXEo, // 66 0F 5C
    SUBSD_XGqXEq, // F2 0F 5C
    SUBSS_XGdXEd, // F3 0F 5C

    MINPS_XGoXEo, // 0F 5D
    MINPD_XGoXEo, // 66 0F 5D
    MINSD_XGqXEq, // F2 0F 5D
    MINSS_XGdXEd, // F3 0F 5D

    DIVPS_XGoXEo, // 0F 5E
    DIVPD_XGoXEo, // 66 0F 5E
    DIVSD_XGqXEq, // F2 0F 5E
    DIVSS_XGdXEd, // F3 0F 5E

    MAXPS_XGoXEo, // 0F 5F
    MAXPD_XGoXEo, // 66 0F 5F
    MAXSD_XGqXEq, // F2 0F 5F
    MAXSS_XGdXEd // F3 0F 5F
};
static int decode_sse58_5F(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_58_5F;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_sse58_5F_tbl[opcode << 2 | sse_prefix];
    return 0;
}

static const int decode_sse60_67_tbl[8 * 2] = {
    // 0F 60
    PUNPCKLBW_MGqMEq,
    PUNPCKLBW_XGoXEo,
    // 0F 61
    PUNPCKLWD_MGqMEq,
    PUNPCKLWD_XGoXEo,
    // 0F 62
    PUNPCKLDQ_MGqMEq,
    PUNPCKLDQ_XGoXEo,
    // 0F 63
    PACKSSWB_MGqMEq,
    PACKSSWB_XGoXEo,
    // 0F 64
    PCMPGTB_MGqMEq,
    PCMPGTB_XGoXEo,
    // 0F 65
    PCMPGTW_MGqMEq,
    PCMPGTW_XGoXEo,
    // 0F 66
    PCMPGTD_MGqMEq,
    PCMPGTD_XGoXEo,
    // 0F 67
    PACKUSWB_MGqMEq,
    PACKUSWB_XGoXEo
};
static int decode_sse60_67(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_60_67;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_sse60_67_tbl[opcode << 1 | (sse_prefix == SSE_PREFIX_66)];
    return 0;
}

static const int decode_sse68_6F_tbl[8 * 4] = {
    PUNPCKHBW_MGqMEq, // 0F 68
    PUNPCKHBW_XGoXEo, // 66 0F 68
    PUNPCKHBW_MGqMEq, // F2 0F 68 - invalid
    PUNPCKHBW_MGqMEq, // F3 0F 68 - invalid

    PUNPCKHWD_MGqMEq, // 0F 69
    PUNPCKHWD_XGoXEo, // 66 0F 69
    PUNPCKHWD_MGqMEq, // F2 0F 69 - invalid
    PUNPCKHWD_MGqMEq, // F3 0F 69 - invalid

    PUNPCKHDQ_MGqMEq, // 0F 6A
    PUNPCKHDQ_XGoXEo, // 66 0F 6A
    PUNPCKHDQ_MGqMEq, // F2 0F 6A - invalid
    PUNPCKHDQ_MGqMEq, // F3 0F 6A - invalid

    PACKSSDW_MGqMEq, // 0F 6B
    PACKSSDW_XGoXEo, // 66 0F 6B
    PACKSSDW_MGqMEq, // F2 0F 6B - invalid
    PACKSSDW_MGqMEq, // F3 0F 6B - invalid

    OP_68_6F_INVALID, // 0F 6C - invalid
    PUNPCKLQDQ_XGoXEo, // 66 0F 6C
    OP_68_6F_INVALID, // F2 0F 6C - invalid
    OP_68_6F_INVALID, // F3 0F 6C - invalid

    OP_68_6F_INVALID, // 0F 6D - invalid
    PUNPCKHQDQ_XGoXEo, // 66 0F 6D
    OP_68_6F_INVALID, // F2 0F 6D - invalid
    OP_68_6F_INVALID, // F3 0F 6D - invalid

    MOVD_MGdEd, // 0F 6E
    MOVD_XGdEd, // 66 0F 6E
    MOVD_MGdEd, // F2 0F 6E - invalid
    MOVD_MGdEd, // F3 0F 6E - invalid

    MOVQ_MGqMEq, // 0F 6F
    MOVDQA_XGoXEo, // 66 0F 6F
    MOVQ_MGqMEq, // F2 0F 6F - invalid
    MOVDQU_XGoXEo // F3 0F 6F
};
static int decode_sse68_6F(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_68_6F;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_sse68_6F_tbl[opcode << 2 | sse_prefix] | ((opcode & 1) << 4);
    return 0;
}
static const int decode_sse70_76_tbl[7 * 4] = {
    PSHUFW_MGqMEqIb, // 0F 70
    PSHUFD_XGoXEoIb, // 66 0F 70
    PSHUFLW_XGoXEoIb, // F2 0F 70
    PSHUFHW_XGoXEoIb, // F3 0F 70

    PSHIFT_MGqIb, // 0F 71
    PSHIFT_XEoIb, // 66 0F 71
    PSHIFT_MGqIb, // F2 0F 71 - invalid
    PSHIFT_MGqIb, // F3 0F 71 - invalid

    PSHIFT_MGqIb, // 0F 72
    PSHIFT_XEoIb, // 66 0F 72
    PSHIFT_MGqIb, // F2 0F 72 - invalid
    PSHIFT_MGqIb, // F3 0F 72 - invalid

    PSHIFT_MGqIb, // 0F 73
    PSHIFT_XEoIb, // 66 0F 73
    PSHIFT_MGqIb, // F2 0F 73 - invalid
    PSHIFT_MGqIb, // F3 0F 73 - invalid

    PCMPEQB_MGqMEq, // 0F 74
    PCMPEQB_XGoXEo, // 66 0F 74
    PCMPEQB_MGqMEq, // F2 0F 74 - invalid
    PCMPEQB_MGqMEq, // F3 0F 74 - invalid

    PCMPEQW_MGqMEq, // 0F 75
    PCMPEQW_XGoXEo, // 66 0F 75
    PCMPEQW_MGqMEq, // F2 0F 75 - invalid
    PCMPEQW_MGqMEq, // F3 0F 75 - invalid

    PCMPEQD_MGqMEq, // 0F 76
    PCMPEQD_XGoXEo, // 66 0F 76
    PCMPEQD_MGqMEq, // F2 0F 76 - invalid
    PCMPEQD_MGqMEq // F3 0F 76 - invalid
};
static const int rm_table_pshift_mmx[24] = {
    0, 0, // 0, 1
    PSHIFT_PSRLW, 0, // 2, 3
    PSHIFT_PSRAW, 0, // 4, 5
    PSHIFT_PSLLW, 0, // 6, 7

    0, 0, // 0, 1
    PSHIFT_PSRLD, 0, // 2, 3
    PSHIFT_PSRAD, 0, // 4, 5
    PSHIFT_PSLLD, 0, // 6, 7

    0, 0, // 0, 1
    PSHIFT_PSRLQ, 0, // 2, 3
    0, 0, // 4, 5
    PSHIFT_PSLLQ, 0 // 6, 7
};
static const int rm_table_pshift_sse[24] = {
    0, 0, // 0, 1
    PSHIFT_PSRLW, 0, // 2, 3
    PSHIFT_PSRAW, 0, // 4, 5
    PSHIFT_PSLLW, 0, // 6, 7

    0, 0, // 0, 1
    PSHIFT_PSRLD, 0, // 2, 3
    PSHIFT_PSRAD, 0, // 4, 5
    PSHIFT_PSLLD, 0, // 6, 7

    0, 0, // 0, 1
    PSHIFT_PSRLQ, PSHIFT_PSRLDQ, // 2, 3
    0, 0, // 4, 5
    PSHIFT_PSLLQ, PSHIFT_PSLLDQ // 6, 7
};
static int decode_sse70_76(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_70_76;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    // Get the opcode information from the table
    int op = decode_sse70_76_tbl[opcode << 2 | sse_prefix], combined_op = (modrm >> 3 & 7) | ((opcode - 1) & 3) << 3;
    
    // Get immediate, if necessary
    int or = 0;
    if(op < PCMPEQB_MGqMEq) or = rb();
    if((op & 15) == PSHIFT_MGqIb)
        op |= rm_table_pshift_mmx[combined_op] << 4;
    else if((op & 15) == PSHIFT_XEoIb) {
        int y = rm_table_pshift_sse[combined_op];
        op |= y << 4;
        if(y == PSHIFT_PSRLDQ || y == PSHIFT_PSLLDQ)
            or <<= 3;
    }
    op |= or << 8;
    i->imm16 = op;
    return 0;
}

static int decode_0F77(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_emms;
    return 0;
}

static const int decode_7C_7F[4] = {
    HADDPD_XGoXEo,
    HADDPS_XGoXEo,
    HSUBPD_XGoXEo,
    HSUBPS_XGoXEo
};
static int decode_sse7C_7D(struct decoded_instruction* i)
{
    uint8_t opcode = rawp[-1] & 1, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    if(sse_prefix != SSE_PREFIX_F2 && sse_prefix != SSE_PREFIX_66)
        i->handler = op_ud_exception;
    else 
        i->handler = op_sse_7C_7D;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_7C_7F[opcode << 1 | (sse_prefix == SSE_PREFIX_F2)];
    return 0;
}

static const int decode_7E_7F[2 * 4] = {
    MOVD_EdMGd, // 0F 7E
    MOVD_EdXGd, // 66 0F 7E
    MOVD_EdMGd, // F2 0F 7E - invalid
    MOVQ_XGqXEq, // F3 0F 7E

    MOVQ_MEqMGq, // 0F 7F
    MOVDQA_XEqXGq, // 66 0F 7F
    MOVQ_MEqMGq, // F2 0F 7F - invalid
    MOVDQU_XEqXGq // F3 0F 7F
};
static int decode_sse7E_7F(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 1, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_7E_7F;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_7E_7F[opcode << 2 | sse_prefix];
    return 0;
}

static int decode_0FA0(struct decoded_instruction* i)
{
    int flags = 0;
    I_SET_RM(flags, FS);
    i->flags = flags;
    i->handler = SIZEOP(op_push_s16, op_push_s32);
    return 0;
}
static int decode_0FA1(struct decoded_instruction* i)
{
    int flags = 0;
    I_SET_RM(flags, FS);
    i->flags = flags;
    i->handler = SIZEOP(op_pop_s16, op_pop_s32);
    return 0;
}
static int decode_0FA2(struct decoded_instruction* i)
{
    i->flags = 0;
    i->handler = op_cpuid;
    return 0;
}
static int decode_0FA3(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0) {
        I_SET_OP(i->flags, 0);
        i->handler = SIZEOP(op_bt_e16, op_bt_e32);
    } else {
        i->disp32 = -1;
        i->imm32 = 0;
        i->handler = SIZEOP(op_bt_r16, op_bt_r32);
    }
    return 0;
}
static int decode_0FA4(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    i->imm8 = rb();
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_shld_e16r16i8, op_shld_e32r32i8);
    else
        i->handler = SIZEOP(op_shld_r16r16i8, op_shld_r32r32i8);
    return 0;
}
static int decode_0FA5(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_shld_e16r16cl, op_shld_e32r32cl);
    else
        i->handler = SIZEOP(op_shld_r16r16cl, op_shld_r32r32cl);
    return 0;
}

static int decode_0FA8(struct decoded_instruction* i)
{
    int flags = 0;
    I_SET_RM(flags, GS);
    i->flags = flags;
    i->handler = SIZEOP(op_push_s16, op_push_s32);
    return 0;
}
static int decode_0FA9(struct decoded_instruction* i)
{
    int flags = 0;
    I_SET_RM(flags, GS);
    i->flags = flags;
    i->handler = SIZEOP(op_pop_s16, op_pop_s32);
    return 0;
}

static int decode_0FAB(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0) {
        I_SET_OP(i->flags, 0);
        i->handler = SIZEOP(op_bts_e16, op_bts_e32);
    } else {
        i->disp32 = -1;
        i->imm32 = 0;
        i->handler = SIZEOP(op_bts_r16, op_bts_r32);
    }
    return 0;
}
static int decode_0FAC(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    i->imm8 = rb();
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_shrd_e16r16i8, op_shrd_e32r32i8);
    else
        i->handler = SIZEOP(op_shrd_r16r16i8, op_shrd_r32r32i8);
    return 0;
}
static int decode_0FAD(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_shrd_e16r16cl, op_shrd_e32r32cl);
    else
        i->handler = SIZEOP(op_shrd_r16r16cl, op_shrd_r32r32cl);
    return 0;
}
static int decode_0FAE(struct decoded_instruction* i)
{
#if 1
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    switch(modrm >> 3 & 7){
        case 0:
            if(modrm >= 0xC0) {
                i->handler = op_ud_exception;
                return 1;
            }else 
                i->handler = op_fxsave;
            break;
        case 1:
            if(modrm >= 0xC0) {
                i->handler = op_ud_exception;
                return 1;
            }else 
                i->handler = op_fxrstor;
            break;
        case 2:
            if(modrm >= 0xC0){
                i->handler = op_ud_exception;
                return 1;
            } else
                i->handler = op_ldmxcsr;
            break;
        case 3:
            if(modrm >= 0xC0){
                i->handler = op_ud_exception;
                return 1;
            } else
                i->handler = op_stmxcsr;
            break;
        case 4: 
            i->handler = op_ud_exception;
            return 1;
        case 6:
        case 5:
        case 7: // *fence or clflush
            // Whether or not CPUID is supported, we have to support this opcode since Windows 7 crashes if you trigger a #UD here. 
            i->handler = op_mfence;
            break;
        default:
            CPU_FATAL("Unknown opcode: 0F AE /%d\n", modrm >> 3 & 7);
    }
    return 0;
#else
    i->handler = op_ud_exception;
    return 1;
#endif
}
static int decode_0FAF(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_imul_r16e16, op_imul_r32e32);
    else
        i->handler = SIZEOP(op_imul_r16r16, op_imul_r32r32);
    return 0;
}
static int decode_0FB0(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 1);
    i->handler = modrm < 0xC0 ? op_cmpxchg_e8r8 : op_cmpxchg_r8r8;
    return 0;
}
static int decode_0FB1(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (state_hash & STATE_CODE16)
        i->handler = modrm < 0xC0 ? op_cmpxchg_e16r16 : op_cmpxchg_r16r16;
    else
        i->handler = modrm < 0xC0 ? op_cmpxchg_e32r32 : op_cmpxchg_r32r32;
    return 0;
}
static int decode_0FB2(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm >= 0xC0) {
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    } else {
        i->flags = parse_modrm(i, modrm, 0);
        i->handler = SIZEOP(op_lss_r16e16, op_lss_r32e32);
    }
    return 0;
}
static int decode_0FB3(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0) {
        I_SET_OP(i->flags, 0);
        i->handler = SIZEOP(op_btr_e16, op_btr_e32);
    } else {
        i->disp32 = -1;
        i->imm32 = 0;
        i->handler = SIZEOP(op_btr_r16, op_btr_r32);
    }
    return 0;
}
static int decode_0FB4(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm >= 0xC0) {
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    } else {
        i->flags = parse_modrm(i, modrm, 0);
        i->handler = SIZEOP(op_lfs_r16e16, op_lfs_r32e32);
    }
    return 0;
}
static int decode_0FB5(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm >= 0xC0) {
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    } else {
        i->flags = parse_modrm(i, modrm, 0);
        i->handler = SIZEOP(op_lgs_r16e16, op_lgs_r32e32);
    }
    return 0;
}
static int decode_0FB6(struct decoded_instruction* i)
{
    // movzx
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 3);
    static const insn_handler_t movzx[4] = {
        op_movzx_r32r8, op_movzx_r16r8,
        op_movzx_r32e8, op_movzx_r16e8
    };
    i->handler = movzx[(modrm < 0xC0) << 1 | (state_hash & STATE_CODE16)];
    return 0;
}
static int decode_0FB7(struct decoded_instruction* i)
{
    // movzx
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    static const insn_handler_t movzx[4] = {
        op_movzx_r32r16, op_mov_r16r16,
        op_movzx_r32e16, op_mov_r16e16
    };
    i->handler = movzx[(modrm < 0xC0) << 1 | (state_hash & STATE_CODE16)];
    return 0;
}

static int decode_0FBA(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if ((modrm & 0x20) == 0) {
        // REG values 0 ... 3 are invalid
        i->handler = op_ud_exception;
        return 1;
    }
    i->imm8 = rb();
    if (modrm < 0xC0) {
        I_SET_OP(i->flags, 1);
        switch (modrm >> 3 & 7) {
        case 4:
            i->handler = SIZEOP(op_bt_e16, op_bt_e32);
            break;
        case 5:
            i->handler = SIZEOP(op_bts_e16, op_bts_e32);
            break;
        case 6:
            i->handler = SIZEOP(op_btr_e16, op_btr_e32);
            break;
        case 7:
            i->handler = SIZEOP(op_btc_e16, op_btc_e32);
            break;
        }
    } else {
        I_SET_OP(i->flags, 1);
        i->disp32 = 0;
        switch (modrm >> 3 & 7) {
        case 4:
            i->handler = SIZEOP(op_bt_r16, op_bt_r32);
            break;
        case 5:
            i->handler = SIZEOP(op_bts_r16, op_bts_r32);
            break;
        case 6:
            i->handler = SIZEOP(op_btr_r16, op_btr_r32);
            break;
        case 7:
            i->handler = SIZEOP(op_btc_r16, op_btc_r32);
            break;
        }
    }
    return 0;
}
static int decode_0FBB(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0) {
        I_SET_OP(i->flags, 0);
        i->handler = SIZEOP(op_btc_e16, op_btc_e32);
    } else {
        i->disp32 = -1;
        i->imm32 = 0;
        i->handler = SIZEOP(op_btc_r16, op_btc_r32);
    }
    return 0;
}
static int decode_0FBC(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_bsf_r16e16, op_bsf_r32e32);
    else
        i->handler = SIZEOP(op_bsf_r16r16, op_bsf_r32r32);
    return 0;
}
static int decode_0FBD(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm < 0xC0)
        i->handler = SIZEOP(op_bsr_r16e16, op_bsr_r32e32);
    else
        i->handler = SIZEOP(op_bsr_r16r16, op_bsr_r32r32);
    return 0;
}

static int decode_0FBE(struct decoded_instruction* i)
{
    // movzx
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 3);
    static const insn_handler_t movzx[4] = {
        op_movsx_r32r8, op_movsx_r16r8,
        op_movsx_r32e8, op_movsx_r16e8
    };
    i->handler = movzx[(modrm < 0xC0) << 1 | (state_hash & STATE_CODE16)];
    return 0;
}
static int decode_0FBF(struct decoded_instruction* i)
{
    // movzx
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    static const insn_handler_t movzx[4] = {
        op_movsx_r32r16, op_mov_r16r16,
        op_movsx_r32e16, op_mov_r16e16
    };
    i->handler = movzx[(modrm < 0xC0) << 1 | (state_hash & STATE_CODE16)];
    return 0;
}

static int decode_0FC0(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 1);
    if (modrm >= 0xC0)
        i->handler = op_xadd_r8r8;
    else
        i->handler = op_xadd_r8e8;
    return 0;
}
static int decode_0FC1(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    i->flags = parse_modrm(i, modrm, 0);
    if (modrm >= 0xC0)
        i->handler = SIZEOP(op_xadd_r16r16, op_xadd_r32r32);
    else
        i->handler = SIZEOP(op_xadd_r16e16, op_xadd_r32e32);
    return 0;
}
static int decode_0FC7(struct decoded_instruction* i)
{
    uint8_t modrm = rb();
    if (modrm >= 0xC0) {
        i->flags = 0;
        i->handler = op_ud_exception;
        return 1;
    } else {
        i->flags = parse_modrm(i, modrm, 6); // 32-bit reg, 32-bit rm
        i->handler = op_cmpxchg8b_e32;
        return 0;
    }
}
static const int decode_sseC2_C6_tbl[5 * 4] = {
    CMPPS_XGoXEoIb, // 0F C2
    CMPPD_XGoXEoIb, // 66 0F C2
    CMPSD_XGqXEqIb, // F2 0F C2
    CMPSS_XGdXEdIb, // F3 0F C2

    MOVNTI_EdGd, // 0F C3
    MOVNTI_EdGd, // 66 0F C3
    MOVNTI_EdGd, // F2 0F C3 - invalid
    MOVNTI_EdGd, // F3 0F C3 - invalid

    PINSRW_MGqEdIb, // 0F C4
    PINSRW_XGoEdIb, // 66 0F C4
    PINSRW_MGqEdIb, // F2 0F C4 - invalid
    PINSRW_MGqEdIb, // F3 0F C4 - invalid

    PEXTRW_GdMEqIb, // 0F C5
    PEXTRW_GdXEoIb, // 66 0F C5
    PEXTRW_GdMEqIb, // F2 0F C5 - invalid
    PEXTRW_GdMEqIb, // F3 0F C5 - invalid

    SHUFPS_XGoXEoIb, // 0F C6
    SHUFPD_XGoXEoIb, // 66 0F C6
    SHUFPS_XGoXEoIb, // F2 0F C6
    SHUFPS_XGoXEoIb // F3 0F C6
};
static int decode_sseC2_C6(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_C2_C6;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    opcode -= 2; // C2 --> C0 for easy lookup
    int op = decode_sseC2_C6_tbl[opcode << 2 | sse_prefix];
    if(op != MOVNTI_EdGd) op |= rb() << 8;
    i->imm16 = op;
    return 0;
} 
static const int decode_sseD0_D7_tbl[8 * 4] = {
    PSRLW_MGqMEq, // 0F D1
    PSRLW_XGoXEo, // 66 0F D1
    PSRLW_MGqMEq, // F2 0F D1 - invalid
    PSRLW_MGqMEq, // F3 0F D1 - invalid
    
    PSRLD_MGqMEq, // 0F D2
    PSRLD_XGoXEo, // 66 0F D2
    PSRLD_MGqMEq, // F2 0F D2 - invalid
    PSRLD_MGqMEq, // F3 0F D2 - invalid
    
    PSRLQ_MGqMEq, // 0F D3
    PSRLQ_XGoXEo, // 66 0F D3
    PSRLQ_MGqMEq, // F2 0F D3 - invalid
    PSRLQ_MGqMEq, // F3 0F D3 - invalid
    
    PADDQ_MGqMEq, // 0F D4
    PADDQ_XGoXEo, // 66 0F D4
    PADDQ_MGqMEq, // F2 0F D4 - invalid
    PADDQ_MGqMEq, // F3 0F D4 - invalid

    PMULLW_MGqMEq, // 0F D5
    PMULLW_XGoXEo, // 66 0F D5
    PMULLW_MGqMEq, // F2 0F D5 - invalid
    PMULLW_MGqMEq, // F3 0F D5 - invalid
    
    MOVQ_XEqXGq, // 0F D6 - invalid
    MOVQ_XEqXGq, // 66 0F D6
    MOVDQ2Q_MGqXEo, // F2 0F D6
    MOVQ2DQ_XGoMEq, // F3 0F D6
    
    PMOVMSKB_GdMEq, // 0F D7
    PMOVMSKB_GdXEo, // 66 0F D7
    PMOVMSKB_GdMEq, // F2 0F D7 - invalid
    PMOVMSKB_GdMEq // F3 0F D7 - invalid
};
static int decode_sseD0_D7(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_D0_D7;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    opcode--;
    i->imm8 = decode_sseD0_D7_tbl[opcode << 2 | sse_prefix];
    return 0;
}
static const int decode_sseD8_DF_tbl[8 * 2] = {
    // 0F D8
    PSUBUSB_MGqMEq,
    PSUBUSB_XGoXEo,
    // 0F D9
    PSUBUSW_MGqMEq,
    PSUBUSW_XGoXEo,
    // 0F DA
    PMINUB_MGqMEq,
    PMINUB_XGoXEo,
    // 0F DB
    PAND_MGqMEq,
    PAND_XGoXEo,
    // 0F DC
    PADDUSB_MGqMEq,
    PADDUSB_XGoXEo,
    // 0F DD
    PADDUSW_MGqMEq,
    PADDUSW_XGoXEo,
    // 0F DE
    PMAXUB_MGqMEq,
    PMAXUB_XGoXEo,
    // 0F DF
    PANDN_MGqMEq,
    PANDN_XGoXEo
};
static int decode_sseD8_DF(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_D8_DF;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_sseD8_DF_tbl[opcode << 1 | (sse_prefix == SSE_PREFIX_66)];
    return 0;
}
static const int decode_sseE0_E7_tbl[8 * 4] = {
    PAVGB_MGqMEq, // 0F E0
    PAVGB_XGoXEo, // 66 0F E0
    PAVGB_MGqMEq, // F2 0F E0 - invalid
    PAVGB_MGqMEq, // F3 0F E0 - invalid
    
    PSRAW_MGqMEq, // 0F E1
    PSRAW_XGoXEo, // 66 0F E1
    PSRAW_MGqMEq, // F2 0F E1 - invalid
    PSRAW_MGqMEq, // F3 0F E1 - invalid

    PSRAD_MGqMEq, // 0F E2
    PSRAD_XGoXEo, // 66 0F E2
    PSRAD_MGqMEq, // F2 0F E2 - invalid
    PSRAD_MGqMEq, // F3 0F E2 - invalid
    
    PAVGW_MGqMEq, // 0F E3
    PAVGW_XGoXEo, // 66 0F E3
    PAVGW_MGqMEq, // F2 0F E3 - invalid
    PAVGW_MGqMEq, // F3 0F E3 - invalid
    
    PMULHUW_MGqMEq, // 0F E4
    PMULHUW_XGoXEo, // 66 0F E4
    PMULHUW_MGqMEq, // F2 0F E4 - invalid
    PMULHUW_MGqMEq, // F3 0F E4 - invalid
    
    PMULHW_MGqMEq, // 0F E5
    PMULHW_XGoXEo, // 66 0F E5
    PMULHW_MGqMEq, // F2 0F E5 - invalid
    PMULHW_MGqMEq, // F3 0F E5 - invalid
    
    CVTTPD2DQ_XGoXEo, // 0F E6 - invalid
    CVTTPD2DQ_XGoXEo, // 66 0F E6
    CVTPD2DQ_XGoXEo, // F2 0F E6
    CVTDQ2PD_XGoXEq, // F3 0F E6
    
    MOVNTQ_MEqMGq, // 0F E7
    MOVNTDQ_XEoXGo, // 66 0F E7
    MOVNTQ_MEqMGq, // F2 0F E7 - invalid
    MOVNTQ_MEqMGq // F2 0F E7 - invalid
};
static int decode_sseE0_E7(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_E0_E7;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_sseE0_E7_tbl[opcode << 2 | sse_prefix];
    return 0;
}

static const int decode_sseE8_EF_tbl[8 * 2] = {
    // 0F E8
    PSUBSB_MGqMEq,
    PSUBSB_XGoXEo,
    // 0F E9
    PSUBSW_MGqMEq,
    PSUBSW_XGoXEo,
    // 0F EA
    PMINSW_MGqMEq,
    PMINSW_XGoXEo,
    // 0F EB
    POR_MGqMEq,
    POR_XGoXEo,
    // 0F EC
    PADDSB_MGqMEq,
    PADDSB_XGoXEo,
    // 0F ED
    PADDSW_MGqMEq,
    PADDSW_XGoXEo,
    // 0F EE
    PMAXSW_MGqMEq,
    PMAXSW_XGoXEo,
    // 0F EF
    PXOR_MGqMEq,
    PXOR_XGoXEo
};
static int decode_sseE8_EF(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_E8_EF;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_sseE8_EF_tbl[opcode << 1 | (sse_prefix == SSE_PREFIX_66)];
    return 0;
}
static const int decode_sseF1_F7_tbl[7 * 2] = {
    // 0F F1
    PSLLW_MGqMEq,
    PSLLW_XGoXEo,
    // 0F F2
    PSLLD_MGqMEq,
    PSLLD_XGoXEo,
    // 0F F3
    PSLLQ_MGqMEq,
    PSLLQ_XGoXEo,
    // 0F F4
    PMULLUDQ_MGqMEq,
    PMULLUDQ_XGoXEo,
    // 0F F5
    PMADDWD_MGqMEq,
    PMADDWD_XGoXEo,
    // 0F F6
    PSADBW_MGqMEq,
    PSADBW_XGoXEo,
    // 0F F7
    MASKMOVQ_MEqMGq,
    MASKMOVDQ_XEoXGo
};
static int decode_sseF1_F7(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_F1_F7;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    opcode--;
    i->imm8 = decode_sseF1_F7_tbl[opcode << 1 | (sse_prefix == SSE_PREFIX_66)];
    return 0;
}

static const int decode_sseF8_FE_tbl[7 * 2] = {
    // 0F F8
    PSUBB_MGqMEq,
    PSUBB_XGoXEo,
    // 0F F9
    PSUBW_MGqMEq,
    PSUBW_XGoXEo,
    // 0F FA
    PSUBD_MGqMEq,
    PSUBD_XGoXEo,
    // 0F FB
    PSUBQ_MGqMEq,
    PSUBQ_XGoXEo,
    // 0F FC
    PADDB_MGqMEq,
    PADDB_XGoXEo,
    // 0F FD
    PADDW_MGqMEq,
    PADDW_XGoXEo,
    // 0F FE
    PADDD_MGqMEq,
    PADDD_XGoXEo
};
static int decode_sseF8_FE(struct decoded_instruction* i){
    uint8_t opcode = rawp[-1] & 7, modrm = rb();
    int flags = parse_modrm(i, modrm, 6);
    i->handler = op_sse_F8_FE;
    I_SET_OP(flags, modrm >= 0xC0);
    i->flags = flags;
    i->imm8 = decode_sseF8_FE_tbl[opcode << 1 | (sse_prefix == SSE_PREFIX_66)];
    return 0;
}

static void set_smc(int length, uint32_t lin)
{
    cpu.tlb_tags[lin >> 12] |= 0x44; // Mark both user and supervisor write TLBs as SMC
    int b128 = ((cpu.phys_eip + length) >> 7) - (cpu.phys_eip >> 7) + 1;
    for (int i = 0; i < b128; i++)
        cpu_smc_set_code(cpu.phys_eip + (i << 7));
}

// Returns number of instructions translated that should be cached.
int cpu_decode(struct trace_info* info, struct decoded_instruction* i)
{
    state_hash = cpu.state_hash;
    rawp = get_phys_ram_ptr(cpu.phys_eip, 0);
    uint8_t* rawp_base = rawp;
    uintptr_t high_mark = (uintptr_t)(get_phys_ram_ptr ((cpu.phys_eip & ~0xFFF) + 0xFF0, 0));
    void* original = i;
    //if(cpu.phys_eip == 0x1102b8) __asm__("int3");

    int instructions_translated = 0, instructions_mask = -1;
    while (1) {
        if ((uintptr_t)rawp > high_mark) {
            // Determine instruction length and see if goes off the end of the page
            uint32_t current_phys_eip = PTR_TO_PHYS(rawp), maximum_insn_length = 0x1000 - (current_phys_eip & 0xFFF);
            //if(current_phys_eip == 0x4d9ffd) __asm__("int3");
            //printf("[pos: %08x] Instruction length: %d\n", current_phys_eip, find_instruction_length(maximum_insn_length));
            //if(current_phys_eip == 0x01ef8ffb) __asm__("int3");
            //if (LIN_EIP() == 0x5ad72fff)
            //    __asm__("int3");
            if (maximum_insn_length > 15 || find_instruction_length(maximum_insn_length) == -1) {
                if (instructions_translated != 0) {
                    // End the trace here
                    i->handler = op_trace_end;
                    instructions_translated++;
                    int length = (uintptr_t)rawp - (uintptr_t)rawp_base;
                    if(instructions_mask != 0){ 
                    info->phys = cpu.phys_eip;
                    info->state_hash = cpu.state_hash;
                    info->flags = length;
                    info->ptr = original;
                    set_smc(length, LIN_EIP());
                    }
                    return instructions_translated & instructions_mask;
                }
                uint32_t lin_eip = LIN_EIP();

#define EXCEPTION_HANDLER          \
    do {                           \
        i->handler = op_trace_end; \
        return 0;                  \
    } while (0)
                    uint32_t next_page = (lin_eip + 15) & ~0xFFF;
                    uint8_t tlb_tag = cpu.tlb_tags[next_page >> 12];
                    if (TLB_ENTRY_INVALID8(next_page, tlb_tag, cpu.tlb_shift_read) || cpu.tlb_attrs[next_page >> 12] & TLB_ATTR_NX) {
                        if (cpu_mmu_translate(next_page, cpu.tlb_shift_read | 8)) 
                            EXCEPTION_HANDLER;
                    }
                // This is the only point at which an exception can be raised.
                for (int j = 0; j < 15; j++)
                    cpu_read8(lin_eip + j, prefetch[j], cpu.tlb_shift_read);

                rawp = prefetch;
                instructions_translated = 1000; // Set this to an absurdly high value
                instructions_mask = 0; // Don't cache this
            }
            // Otherwise, the entire instruction can be read.
        }

        // Now, we have established that we will not fault while we fetch the opcode from memory.

        uint8_t *prev_rawp = rawp, opcode = *rawp++;
#ifdef TEST_CLEAR_FLAGS
        i->flags = 0x12345678;
#endif
        int end_of_trace = table[opcode](i);
#ifdef TEST_CLEAR_FLAGS
        if (i->flags == 0x12345678)
            CPU_FATAL("Opcode %02x is buggy\n", opcode);
#endif
        instructions_translated++;
        i->flags = (i->flags & ~15) | ((uintptr_t)rawp - (uintptr_t)prev_rawp);
        ++i;

        if (end_of_trace || instructions_translated >= (MAX_TRACE_SIZE-1)) {
            if (!end_of_trace) {
                // Handles the case where trace is too long or is a single-instruction trace.
                i->handler = op_trace_end;
                instructions_translated++;
            }
            int length = (uintptr_t)rawp - (uintptr_t)rawp_base;
            if (instructions_mask != 0) { // Don't commit page split traces
                info->phys = cpu.phys_eip;
                info->state_hash = cpu.state_hash;
                info->flags = length;
                info->ptr = original;
                set_smc(length, LIN_EIP());
            }
            return instructions_translated & instructions_mask;
        }
    }
}

static const decode_handler_t table[256] = {
    /* 00 */ decode_arith_00,
    /* 01 */ decode_arith_01,
    /* 02 */ decode_arith_02,
    /* 03 */ decode_arith_03,
    /* 04 */ decode_arith_04,
    /* 05 */ decode_arith_05,
    /* 06 */ decode_push_sv,
    /* 07 */ decode_pop_sv,
    /* 08 */ decode_arith_00,
    /* 09 */ decode_arith_01,
    /* 0A */ decode_arith_02,
    /* 0B */ decode_arith_03,
    /* 0C */ decode_arith_04,
    /* 0D */ decode_arith_05,
    /* 0E */ decode_push_sv,
    /* 0F */ decode_0F,
    /* 10 */ decode_arith_00,
    /* 11 */ decode_arith_01,
    /* 12 */ decode_arith_02,
    /* 13 */ decode_arith_03,
    /* 14 */ decode_arith_04,
    /* 15 */ decode_arith_05,
    /* 16 */ decode_push_sv,
    /* 17 */ decode_pop_sv,
    /* 18 */ decode_arith_00,
    /* 19 */ decode_arith_01,
    /* 1A */ decode_arith_02,
    /* 1B */ decode_arith_03,
    /* 1C */ decode_arith_04,
    /* 1D */ decode_arith_05,
    /* 1E */ decode_push_sv,
    /* 1F */ decode_pop_sv,
    /* 20 */ decode_arith_00,
    /* 21 */ decode_arith_01,
    /* 22 */ decode_arith_02,
    /* 23 */ decode_arith_03,
    /* 24 */ decode_arith_04,
    /* 25 */ decode_arith_05,
    /* 26 */ decode_prefix,
    /* 27 */ decode_27,
    /* 28 */ decode_arith_00,
    /* 29 */ decode_arith_01,
    /* 2A */ decode_arith_02,
    /* 2B */ decode_arith_03,
    /* 2C */ decode_arith_04,
    /* 2D */ decode_arith_05,
    /* 2E */ decode_prefix,
    /* 2F */ decode_2F,
    /* 30 */ decode_arith_00,
    /* 31 */ decode_arith_01,
    /* 32 */ decode_arith_02,
    /* 33 */ decode_arith_03,
    /* 34 */ decode_arith_04,
    /* 35 */ decode_arith_05,
    /* 36 */ decode_prefix,
    /* 37 */ decode_37,
    /* 38 */ decode_38,
    /* 39 */ decode_39,
    /* 3A */ decode_3A,
    /* 3B */ decode_3B,
    /* 3C */ decode_3C,
    /* 3D */ decode_3D,
    /* 3E */ decode_prefix,
    /* 3F */ decode_3F,
    /* 40 */ decode_inc_rv,
    /* 41 */ decode_inc_rv,
    /* 42 */ decode_inc_rv,
    /* 43 */ decode_inc_rv,
    /* 44 */ decode_inc_rv,
    /* 45 */ decode_inc_rv,
    /* 46 */ decode_inc_rv,
    /* 47 */ decode_inc_rv,
    /* 48 */ decode_dec_rv,
    /* 49 */ decode_dec_rv,
    /* 4A */ decode_dec_rv,
    /* 4B */ decode_dec_rv,
    /* 4C */ decode_dec_rv,
    /* 4D */ decode_dec_rv,
    /* 4E */ decode_dec_rv,
    /* 4F */ decode_dec_rv,
    /* 50 */ decode_push_rv,
    /* 51 */ decode_push_rv,
    /* 52 */ decode_push_rv,
    /* 53 */ decode_push_rv,
    /* 54 */ decode_push_rv,
    /* 55 */ decode_push_rv,
    /* 56 */ decode_push_rv,
    /* 57 */ decode_push_rv,
    /* 58 */ decode_pop_rv,
    /* 59 */ decode_pop_rv,
    /* 5A */ decode_pop_rv,
    /* 5B */ decode_pop_rv,
    /* 5C */ decode_pop_rv,
    /* 5D */ decode_pop_rv,
    /* 5E */ decode_pop_rv,
    /* 5F */ decode_pop_rv,
    /* 60 */ decode_60,
    /* 61 */ decode_61,
    /* 62 */ decode_62,
    /* 63 */ decode_63,
    /* 64 */ decode_prefix,
    /* 65 */ decode_prefix,
    /* 66 */ decode_prefix,
    /* 67 */ decode_prefix,
    /* 68 */ decode_68,
    /* 69 */ decode_69,
    /* 6A */ decode_6A,
    /* 6B */ decode_6B,
    /* 6C */ decode_6C,
    /* 6D */ decode_6D,
    /* 6E */ decode_6E,
    /* 6F */ decode_6F,
    /* 70 */ decode_jcc8,
    /* 71 */ decode_jcc8,
    /* 72 */ decode_jcc8,
    /* 73 */ decode_jcc8,
    /* 74 */ decode_jcc8,
    /* 75 */ decode_jcc8,
    /* 76 */ decode_jcc8,
    /* 77 */ decode_jcc8,
    /* 78 */ decode_jcc8,
    /* 79 */ decode_jcc8,
    /* 7A */ decode_jcc8,
    /* 7B */ decode_jcc8,
    /* 7C */ decode_jcc8,
    /* 7D */ decode_jcc8,
    /* 7E */ decode_jcc8,
    /* 7F */ decode_jcc8,
    /* 80 */ decode_80,
    /* 81 */ decode_81,
    /* 82 */ decode_80, // Note: 82 is an alias of 80
    /* 83 */ decode_83,
    /* 84 */ decode_84,
    /* 85 */ decode_85,
    /* 86 */ decode_86,
    /* 87 */ decode_87,
    /* 88 */ decode_88,
    /* 89 */ decode_89,
    /* 8A */ decode_8A,
    /* 8B */ decode_8B,
    /* 8C */ decode_8C,
    /* 8D */ decode_8D,
    /* 8E */ decode_8E,
    /* 8F */ decode_8F,
    /* 90 */ decode_90,
    /* 91 */ decode_xchg,
    /* 92 */ decode_xchg,
    /* 93 */ decode_xchg,
    /* 94 */ decode_xchg,
    /* 95 */ decode_xchg,
    /* 96 */ decode_xchg,
    /* 97 */ decode_xchg,
    /* 98 */ decode_98,
    /* 99 */ decode_99,
    /* 9A */ decode_9A,
    /* 9B */ decode_9B,
    /* 9C */ decode_9C,
    /* 9D */ decode_9D,
    /* 9E */ decode_9E,
    /* 9F */ decode_9F,
    /* A0 */ decode_A0,
    /* A1 */ decode_A1,
    /* A2 */ decode_A2,
    /* A3 */ decode_A3,
    /* A4 */ decode_A4,
    /* A5 */ decode_A5,
    /* A6 */ decode_A6,
    /* A7 */ decode_A7,
    /* A8 */ decode_A8,
    /* A9 */ decode_A9,
    /* AA */ decode_AA,
    /* AB */ decode_AB,
    /* AC */ decode_AC,
    /* AD */ decode_AD,
    /* AE */ decode_AE,
    /* AF */ decode_AF,
    /* B0 */ decode_mov_rbib,
    /* B1 */ decode_mov_rbib,
    /* B2 */ decode_mov_rbib,
    /* B3 */ decode_mov_rbib,
    /* B4 */ decode_mov_rbib,
    /* B5 */ decode_mov_rbib,
    /* B6 */ decode_mov_rbib,
    /* B7 */ decode_mov_rbib,
    /* B8 */ decode_mov_rviv,
    /* B9 */ decode_mov_rviv,
    /* BA */ decode_mov_rviv,
    /* BB */ decode_mov_rviv,
    /* BC */ decode_mov_rviv,
    /* BD */ decode_mov_rviv,
    /* BE */ decode_mov_rviv,
    /* BF */ decode_mov_rviv,
    /* C0 */ decode_C0,
    /* C1 */ decode_C1,
    /* C2 */ decode_C2,
    /* C3 */ decode_C3,
    /* C4 */ decode_C4,
    /* C5 */ decode_C5,
    /* C6 */ decode_C6,
    /* C7 */ decode_C7,
    /* C8 */ decode_C8,
    /* C9 */ decode_C9,
    /* CA */ decode_CA,
    /* CB */ decode_CB,
    /* CC */ decode_CC,
    /* CD */ decode_CD,
    /* CE */ decode_CE,
    /* CF */ decode_CF,
    /* D0 */ decode_D0,
    /* D1 */ decode_D1,
    /* D2 */ decode_D2,
    /* D3 */ decode_D3,
    /* D4 */ decode_D4,
    /* D5 */ decode_D5,
    /* D6 */ decode_invalid,
    /* D7 */ decode_D7,
    /* D8 */ decode_fpu,
    /* D9 */ decode_fpu,
    /* DA */ decode_fpu,
    /* DB */ decode_fpu,
    /* DC */ decode_fpu,
    /* DD */ decode_fpu,
    /* DE */ decode_fpu,
    /* DF */ decode_fpu,
    /* E0 */ decode_E0,
    /* E1 */ decode_E1,
    /* E2 */ decode_E2,
    /* E3 */ decode_E3,
    /* E4 */ decode_E4,
    /* E5 */ decode_E5,
    /* E6 */ decode_E6,
    /* E7 */ decode_E7,
    /* E8 */ decode_E8,
    /* E9 */ decode_E9,
    /* EA */ decode_EA,
    /* EB */ decode_EB,
    /* EC */ decode_EC,
    /* ED */ decode_ED,
    /* EE */ decode_EE,
    /* EF */ decode_EF,
    /* F0 */ decode_prefix,
    /* F1 */ decode_invalid,
    /* F2 */ decode_prefix,
    /* F3 */ decode_prefix,
    /* F4 */ decode_F4,
    /* F5 */ decode_F5,
    /* F6 */ decode_F6,
    /* F7 */ decode_F7,
    /* F8 */ decode_F8,
    /* F9 */ decode_F9,
    /* FA */ decode_FA,
    /* FB */ decode_FB,
    /* FC */ decode_FC,
    /* FD */ decode_FD,
    /* FE */ decode_FE,
    /* FF */ decode_FF
};

#ifndef DISABLE_SSE
#define SSE(x) x
#else
#define SSE(x) decode_ud
#endif

static const decode_handler_t table0F[256] = {
    /* 0F 00 */ decode_0F00,
    /* 0F 01 */ decode_0F01,
    /* 0F 02 */ decode_0F02,
    /* 0F 03 */ decode_0F03,
    /* 0F 04 */ decode_ud,
    /* 0F 05 */ decode_ud,
    /* 0F 06 */ decode_0F06,
    /* 0F 07 */ decode_ud,
    /* 0F 08 */ decode_invalid0F,
    /* 0F 09 */ decode_0F09,
    /* 0F 0A */ decode_ud,
    /* 0F 0B */ decode_0F0B,
    /* 0F 0C */ decode_ud,
    /* 0F 0D */ decode_invalid0F,
    /* 0F 0E */ decode_ud,
    /* 0F 0F */ decode_ud,
    /* 0F 10 */ decode_sse10_17,
    /* 0F 11 */ decode_sse10_17,
    /* 0F 12 */ decode_sse10_17,
    /* 0F 13 */ decode_sse10_17,
    /* 0F 14 */ decode_sse10_17,
    /* 0F 15 */ decode_sse10_17,
    /* 0F 16 */ decode_sse10_17,
    /* 0F 17 */ decode_sse10_17,
    /* 0F 18 */ decode_0F18,
    /* 0F 19 */ decode_invalid0F,
    /* 0F 1A */ decode_invalid0F,
    /* 0F 1B */ decode_invalid0F,
    /* 0F 1C */ decode_invalid0F,
    /* 0F 1D */ decode_invalid0F,
    /* 0F 1E */ decode_invalid0F,
    /* 0F 1F */ decode_0F1F,
    /* 0F 20 */ decode_0F20,
    /* 0F 21 */ decode_0F21,
    /* 0F 22 */ decode_0F22,
    /* 0F 23 */ decode_0F23,
    /* 0F 24 */ decode_invalid0F,
    /* 0F 25 */ decode_invalid0F,
    /* 0F 26 */ decode_invalid0F,
    /* 0F 27 */ decode_invalid0F,
    /* 0F 28 */ decode_sse28_2F,
    /* 0F 29 */ decode_sse28_2F,
    /* 0F 2A */ decode_sse28_2F,
    /* 0F 2B */ decode_sse28_2F,
    /* 0F 2C */ decode_sse28_2F,
    /* 0F 2D */ decode_sse28_2F,
    /* 0F 2E */ decode_sse28_2F,
    /* 0F 2F */ decode_sse28_2F,
    /* 0F 30 */ decode_0F30,
    /* 0F 31 */ decode_0F31,
    /* 0F 32 */ decode_0F32,
    /* 0F 33 */ decode_invalid0F,
    /* 0F 34 */ decode_sysenter_sysexit,
    /* 0F 35 */ decode_sysenter_sysexit,
    /* 0F 36 */ decode_invalid0F,
    /* 0F 37 */ decode_invalid0F,
    /* 0F 38 */ decode_0F38,
    /* 0F 39 */ decode_invalid0F,
    /* 0F 3A */ decode_invalid0F,
    /* 0F 3B */ decode_invalid0F,
    /* 0F 3C */ decode_invalid0F,
    /* 0F 3D */ decode_invalid0F,
    /* 0F 3E */ decode_invalid0F,
    /* 0F 3F */ decode_invalid0F,
    /* 0F 40 */ decode_cmov,
    /* 0F 41 */ decode_cmov,
    /* 0F 42 */ decode_cmov,
    /* 0F 43 */ decode_cmov,
    /* 0F 44 */ decode_cmov,
    /* 0F 45 */ decode_cmov,
    /* 0F 46 */ decode_cmov,
    /* 0F 47 */ decode_cmov,
    /* 0F 48 */ decode_cmov,
    /* 0F 49 */ decode_cmov,
    /* 0F 4A */ decode_cmov,
    /* 0F 4B */ decode_cmov,
    /* 0F 4C */ decode_cmov,
    /* 0F 4D */ decode_cmov,
    /* 0F 4E */ decode_cmov,
    /* 0F 4F */ decode_cmov,
    /* 0F 50 */ decode_sse50_57,
    /* 0F 51 */ decode_sse50_57,
    /* 0F 52 */ decode_sse50_57,
    /* 0F 53 */ decode_sse50_57,
    /* 0F 54 */ decode_sse50_57,
    /* 0F 55 */ decode_sse50_57,
    /* 0F 56 */ decode_sse50_57,
    /* 0F 57 */ decode_sse50_57,
    /* 0F 58 */ decode_sse58_5F,
    /* 0F 59 */ decode_sse58_5F,
    /* 0F 5A */ decode_sse58_5F,
    /* 0F 5B */ decode_sse58_5F,
    /* 0F 5C */ decode_sse58_5F,
    /* 0F 5D */ decode_sse58_5F,
    /* 0F 5E */ decode_sse58_5F,
    /* 0F 5F */ decode_sse58_5F,
    /* 0F 60 */ decode_sse60_67,
    /* 0F 61 */ decode_sse60_67,
    /* 0F 62 */ decode_sse60_67,
    /* 0F 63 */ decode_sse60_67,
    /* 0F 64 */ decode_sse60_67,
    /* 0F 65 */ decode_sse60_67,
    /* 0F 66 */ decode_sse60_67,
    /* 0F 67 */ decode_sse60_67,
    /* 0F 68 */ decode_sse68_6F,
    /* 0F 69 */ decode_sse68_6F,
    /* 0F 6A */ decode_sse68_6F,
    /* 0F 6B */ decode_sse68_6F,
    /* 0F 6C */ decode_sse68_6F,
    /* 0F 6D */ decode_sse68_6F,
    /* 0F 6E */ decode_sse68_6F,
    /* 0F 6F */ decode_sse68_6F,
    /* 0F 70 */ decode_sse70_76,
    /* 0F 71 */ decode_sse70_76,
    /* 0F 72 */ decode_sse70_76,
    /* 0F 73 */ decode_sse70_76,
    /* 0F 74 */ decode_sse70_76,
    /* 0F 75 */ decode_sse70_76,
    /* 0F 76 */ decode_sse70_76,
    /* 0F 77 */ decode_0F77,
    /* 0F 78 */ decode_invalid0F,
    /* 0F 79 */ decode_invalid0F,
    /* 0F 7A */ decode_invalid0F,
    /* 0F 7B */ decode_invalid0F,
    /* 0F 7C */ decode_sse7C_7D,
    /* 0F 7D */ decode_sse7C_7D,
    /* 0F 7E */ decode_sse7E_7F,
    /* 0F 7F */ decode_sse7E_7F,
    /* 0F 80 */ decode_jccv,
    /* 0F 81 */ decode_jccv,
    /* 0F 82 */ decode_jccv,
    /* 0F 83 */ decode_jccv,
    /* 0F 84 */ decode_jccv,
    /* 0F 85 */ decode_jccv,
    /* 0F 86 */ decode_jccv,
    /* 0F 87 */ decode_jccv,
    /* 0F 88 */ decode_jccv,
    /* 0F 89 */ decode_jccv,
    /* 0F 8A */ decode_jccv,
    /* 0F 8B */ decode_jccv,
    /* 0F 8C */ decode_jccv,
    /* 0F 8D */ decode_jccv,
    /* 0F 8E */ decode_jccv,
    /* 0F 8F */ decode_jccv,
    /* 0F 90 */ decode_setcc,
    /* 0F 91 */ decode_setcc,
    /* 0F 92 */ decode_setcc,
    /* 0F 93 */ decode_setcc,
    /* 0F 94 */ decode_setcc,
    /* 0F 95 */ decode_setcc,
    /* 0F 96 */ decode_setcc,
    /* 0F 97 */ decode_setcc,
    /* 0F 98 */ decode_setcc,
    /* 0F 99 */ decode_setcc,
    /* 0F 9A */ decode_setcc,
    /* 0F 9B */ decode_setcc,
    /* 0F 9C */ decode_setcc,
    /* 0F 9D */ decode_setcc,
    /* 0F 9E */ decode_setcc,
    /* 0F 9F */ decode_setcc,
    /* 0F A0 */ decode_0FA0,
    /* 0F A1 */ decode_0FA1,
    /* 0F A2 */ decode_0FA2,
    /* 0F A3 */ decode_0FA3,
    /* 0F A4 */ decode_0FA4,
    /* 0F A5 */ decode_0FA5,
    /* 0F A6 */ decode_ud, // XBTS - OS/2 uses it for processor detection
    /* 0F A7 */ decode_ud, // IBTS - OS/2 uses it for processor detection
    /* 0F A8 */ decode_0FA8,
    /* 0F A9 */ decode_0FA9,
    /* 0F AA */ decode_invalid0F,
    /* 0F AB */ decode_0FAB,
    /* 0F AC */ decode_0FAC,
    /* 0F AD */ decode_0FAD,
    /* 0F AE */ decode_0FAE,
    /* 0F AF */ decode_0FAF,
    /* 0F B0 */ decode_0FB0,
    /* 0F B1 */ decode_0FB1,
    /* 0F B2 */ decode_0FB2,
    /* 0F B3 */ decode_0FB3,
    /* 0F B4 */ decode_0FB4,
    /* 0F B5 */ decode_0FB5,
    /* 0F B6 */ decode_0FB6,
    /* 0F B7 */ decode_0FB7,
    /* 0F B8 */ decode_invalid0F,
    /* 0F B9 */ decode_invalid0F,
    /* 0F BA */ decode_0FBA,
    /* 0F BB */ decode_0FBB,
    /* 0F BC */ decode_0FBC,
    /* 0F BD */ decode_0FBD,
    /* 0F BE */ decode_0FBE,
    /* 0F BF */ decode_0FBF,
    /* 0F C0 */ decode_0FC0,
    /* 0F C1 */ decode_0FC1,
    /* 0F C2 */ decode_sseC2_C6,
    /* 0F C3 */ decode_sseC2_C6,
    /* 0F C4 */ decode_sseC2_C6,
    /* 0F C5 */ decode_sseC2_C6,
    /* 0F C6 */ decode_sseC2_C6,
    /* 0F C7 */ decode_0FC7,
    /* 0F C8 */ decode_bswap,
    /* 0F C9 */ decode_bswap,
    /* 0F CA */ decode_bswap,
    /* 0F CB */ decode_bswap,
    /* 0F CC */ decode_bswap,
    /* 0F CD */ decode_bswap,
    /* 0F CE */ decode_bswap,
    /* 0F CF */ decode_bswap,
    /* 0F D0 */ decode_sseD0_D7,
    /* 0F D1 */ decode_sseD0_D7,
    /* 0F D2 */ decode_sseD0_D7,
    /* 0F D3 */ decode_sseD0_D7,
    /* 0F D4 */ decode_sseD0_D7,
    /* 0F D5 */ decode_sseD0_D7,
    /* 0F D6 */ decode_sseD0_D7,
    /* 0F D7 */ decode_sseD0_D7,
    /* 0F D8 */ decode_sseD8_DF,
    /* 0F D9 */ decode_sseD8_DF,
    /* 0F DA */ decode_sseD8_DF,
    /* 0F DB */ decode_sseD8_DF,
    /* 0F DC */ decode_sseD8_DF,
    /* 0F DD */ decode_sseD8_DF,
    /* 0F DE */ decode_sseD8_DF,
    /* 0F DF */ decode_sseD8_DF,
    /* 0F E0 */ decode_sseE0_E7,
    /* 0F E1 */ decode_sseE0_E7,
    /* 0F E2 */ decode_sseE0_E7,
    /* 0F E3 */ decode_sseE0_E7,
    /* 0F E4 */ decode_sseE0_E7,
    /* 0F E5 */ decode_sseE0_E7,
    /* 0F E6 */ decode_sseE0_E7,
    /* 0F E7 */ decode_sseE0_E7,
    /* 0F E8 */ decode_sseE8_EF,
    /* 0F E9 */ decode_sseE8_EF,
    /* 0F EA */ decode_sseE8_EF,
    /* 0F EB */ decode_sseE8_EF,
    /* 0F EC */ decode_sseE8_EF,
    /* 0F ED */ decode_sseE8_EF,
    /* 0F EE */ decode_sseE8_EF,
    /* 0F EF */ decode_sseE8_EF,
    /* 0F F0 */ decode_invalid0F,
    /* 0F F1 */ decode_sseF1_F7,
    /* 0F F2 */ decode_sseF1_F7,
    /* 0F F3 */ decode_sseF1_F7,
    /* 0F F4 */ decode_sseF1_F7,
    /* 0F F5 */ decode_sseF1_F7,
    /* 0F F6 */ decode_sseF1_F7,
    /* 0F F7 */ decode_sseF1_F7,
    /* 0F F8 */ decode_sseF8_FE,
    /* 0F F9 */ decode_sseF8_FE,
    /* 0F FA */ decode_sseF8_FE,
    /* 0F FB */ decode_sseF8_FE,
    /* 0F FC */ decode_sseF8_FE,
    /* 0F FD */ decode_sseF8_FE,
    /* 0F FE */ decode_sseF8_FE,
    /* 0F FF */ decode_ud // Windows 3.1 and Windows 95 use this opcode
};