#ifndef CPU_H
#define CPU_H

#include "instruction.h"
#include "util.h"
#include <stdint.h>

#define ES 0
#define CS 1
#define SS 2
#define DS 3
#define FS 4
#define GS 5

#define SEG_TR 6
#define SEG_GDTR 7
#define SEG_LDTR 8
#define SEG_IDTR 9

#define SEG_ACCESS_CACHE_16BIT 0x20000

#define EAX 0
#define ECX 1
#define EDX 2
#define EBX 3
#define ESP 4
#define EBP 5
#define ESI 6
#define EDI 7
#define EZR 8 // 0 register
#define ETMP 9 // temp register
#define AX 0
#define CX 2
#define DX 4
#define BX 6
#define SP 8
#define BP 10
#define SI 12
#define DI 14
#define ZR 16 // 16-bit version
#define TMP 18 // 0xFFFF register
#define AL 0
#define CL 4
#define DL 8
#define BL 12
#define AH 1
#define CH 5
#define DH 9
#define BH 13
#define ZR8 32

#define XMM32(n) cpu.xmm32[(n) << 2]
#define XMM16(n) cpu.xmm16[(n) << 3]
#define XMM8(n) cpu.xmm8[(n) << 4]

#define RESULT_INVALID (uint32_t) - 1

#define CR0_PE 1
#define CR0_MP 2
#define CR0_EM 4
#define CR0_TS 8
#define CR0_ET 16
#define CR0_NE 32
#define CR0_WP 65536
#define CR0_NW (1 << 29)
#define CR0_CD (1 << 30)
#define CR0_PG (1 << 31)

#define CR4_VME (1 << 0)
#define CR4_PVI (1 << 1)
#define CR4_TSD (1 << 2)
#define CR4_DE (1 << 3)
#define CR4_PSE (1 << 4)
#define CR4_PAE (1 << 5)
#define CR4_MCE (1 << 6)
#define CR4_PGE (1 << 7)
#define CR4_PCE (1 << 8)
#define CR4_OSFXSR (1 << 9)
#define CR4_OSXMMEXCPT (1 << 10)
#define CR4_UMIP (1 << 11)
#define CR4_LA57 (1 << 12)
#define CR4_VMXE (1 << 13)
#define CR4_SMXE (1 << 14)
#define CR4_FSGSBASE (1 << 16)
#define CR4_PCIDE (1 << 17)
#define CR4_OSXSAVE (1 << 18)
#define CR4_SMEP (1 << 20)
#define CR4_SMAP (1 << 21)
#define CR4_PKE (1 << 22)

#define EFLAGS_CF 0x01
#define EFLAGS_PF 0x04
#define EFLAGS_AF 0x10
#define EFLAGS_ZF 0x40
#define EFLAGS_SF 0x80
#define EFLAGS_TF 0x100
#define EFLAGS_IF 0x200
#define EFLAGS_DF 0x400
#define EFLAGS_OF 0x800
#define EFLAGS_IOPL 0x3000 // actually a mask
#define EFLAGS_NT 0x4000
#define EFLAGS_RF 0x10000
#define EFLAGS_VM 0x20000
#define EFLAGS_AC 0x40000
#define EFLAGS_VIF 0x80000
#define EFLAGS_VIP 0x100000
#define EFLAGS_ID 0x200000
#define valid_flag_mask (EFLAGS_ID | EFLAGS_VIP | EFLAGS_VIF | EFLAGS_AC | EFLAGS_VM | EFLAGS_RF | EFLAGS_NT | EFLAGS_IOPL | EFLAGS_OF | EFLAGS_DF | EFLAGS_IF | EFLAGS_TF | EFLAGS_SF | EFLAGS_ZF | EFLAGS_AF | EFLAGS_PF | EFLAGS_CF)
#define arith_flag_mask (EFLAGS_OF | EFLAGS_SF | EFLAGS_ZF | EFLAGS_AF | EFLAGS_PF | EFLAGS_CF)
#define get_iopl() (cpu.eflags >> 12 & 3)

#define STATE_CODE16 0x0001
#define STATE_ADDR16 0x0002

#define IS_USER_MODE() cpu.cpl == 3

#define CPU_LOG(x, ...) LOG("CPU", x, ##__VA_ARGS__)
#define CPU_DEBUG(x, ...) LOG("CPU", x, ##__VA_ARGS__)
#define CPU_FATAL(x, ...) \
    FATAL("CPU", x, ##__VA_ARGS__)

// all acronyms: https://css.csail.mit.edu/6.858/2014/readings/i386/s06_03.htm
enum {
    //RESERVED = 0,
    AVAILABLE_TSS_286 = 1,
    LDT = 2,
    BUSY_TSS_286 = 3,
    CALL_GATE_286 = 4,
    TASK_GATE = 5,
    INTERRUPT_GATE_286 = 6,
    TRAP_GATE_286 = 7,
    //RESERVED = 8,
    AVAILABLE_TSS_386 = 9,
    //RESERVED = 10,
    BUSY_TSS_386 = 11,
    CALL_GATE_386 = 12,
    //RESERVED = 13,
    INTERRUPT_GATE_386 = 14,
    TRAP_GATE_386 = 15,
    // 16 ... 23 are application (data) segments
    // 24 ... 31 are code segments
};
#define SELECTOR_RPL(n) (n & 3)
#define SELECTOR_LDT(n) ((n & 4) != 0)
#define SELECTOR_GDT(n) ((n & 4) == 0)

#define ACCESS_P 0x80
#define ACCESS_DPL_MASK 0x60
#define ACCESS_DPL(n) (n >> 5 & 3)
#define ACCESS_S 0x10
#define ACCESS_EX 0x08
#define ACCESS_DC 0x04
#define ACCESS_RW 0x02
#define ACCESS_AC 0x01
#define ACCESS_CODE_ISCONFORMING(n) (((n) & (ACCESS_S | ACCESS_EX | ACCESS_DC)) == (ACCESS_S | ACCESS_EX))
#define ACCESS_CODE_ISNONCONFORMING(n) (((n) & (ACCESS_S | ACCESS_EX | ACCESS_DC)) == (ACCESS_S | ACCESS_EX | ACCESS_DC))
#define ACCESS_TYPE(n) ((n) & (ACCESS_S | ACCESS_EX | ACCESS_DC | ACCESS_RW | ACCESS_AC))
#define ACCESS_G 0x8000
// determines whether size is 32 bit (1) or 16-bit (0)
#define ACCESS_B 0x4000
#define ACCESS_AVL 0x1000

#define MXCSR_MASK 0xFFFF

#define DESC_ACCESS(info) ((info)->raw[1] >> 8 & 0xFFFF)
#define IS_PRESENT(acc) ((acc & ACCESS_P) != 0)

struct seg_desc {
    union {
        // Note: Do NOT use the following struct. It is only here for explanation.
        // There is a possibility that fields.access could result in an unaligned read.
        // Use SEG_ACCESS, cpu_seg_get_base, and cpu_seg_get_limit
        struct __attribute__((__packed__)) {
            uint16_t limit_0_15;
            uint16_t base_0_15;
            uint8_t base_16_23;
            uint16_t access;
            uint8_t base_24_31;
        } fields;
        uint32_t raw[2];
    };
};

enum {
    INTERRUPT_TYPE_EXCEPTION, // i.e. #GP(0)
    INTERRUPT_TYPE_SOFTWARE, // i.e. int 21h
    INTERRUPT_TYPE_HARDWARE // i.e. IRQ8
};

#define TRACE_INFO_ENTRIES (64 * 1024) // TODO: Enlarge?
#define TRACE_CACHE_SIZE (TRACE_INFO_ENTRIES * 8) // TODO: Enlarge?
#define MAX_TRACE_SIZE 32

#define MAX_TLB_ENTRIES 8192

#define TRACE_LENGTH(flags) (flags & 0x3FF)
struct trace_info {
    uint32_t phys, state_hash;
    struct decoded_instruction* ptr;
    uint32_t flags;
#ifdef DYNAREC
    uint32_t calls; // Used by the dynamic recompiler to determine whether the block should be compiled
#endif
};

struct cpu {
    // <<< BEGIN STRUCT "struct" >>>
    /// ignore: mem

    // ========================================================================
    // Registers
    // ========================================================================
    union {
        uint32_t reg32[16];
        uint16_t reg16[16 * 2];
        uint8_t reg8[16 * 4];
    };

    // SSE registers
    union {
        uint32_t xmm32[32];
        uint16_t xmm16[64];
        uint8_t xmm8[128];
        uint64_t xmm64[16];
    } __attribute__((aligned(16)));
    uint32_t mxcsr;

    uint32_t esp_mask;

    // ========================================================================
    // Memory
    // ========================================================================
    union {
        void* mem;
        uint32_t* mem32;
        uint16_t* mem16;
        uint8_t* mem8;
    };
    uint32_t memory_size;

    // ========================================================================
    // EFLAGS and condition codes
    // ========================================================================

    // Holds all non-OSZAPC state (and some other flags, when required)
    uint32_t eflags;
    // Holds a number of auxiliary bits to make flags computation simpler
    uint32_t laux;
    // Last operand 1, last operand 2, and last result
    uint32_t lop1, lop2, lr;

    // ========================================================================
    // Code fetching/EIP
    // ========================================================================

    // The current physical EIP
    uint32_t phys_eip;
    // The current physical EIP page bias. For instance, if eip_phys == 0x1234, then eip_page_base = 0x1000
    uint32_t last_phys_eip;
    // virtual_eip - physical_eip. Allows us to quickly determine the virtual address when needed
    uint32_t eip_phys_bias;
    // Holds information on whether opcodes are 16-bit or 32-bit
    uint32_t state_hash;

    // ========================================================================
    // Cycle counting
    // ========================================================================
    uint64_t cycles, cycle_frame_end; // Number of CPU cycles run, used for internal timing
    int cycles_to_run, // Cycles left in current time slice to run
        refill_counter, // If we have to exit the loop for some reason, cycles_to_run will be set to 1 and refill_counter will be set to old value of cycles_to_run -1
        hlt_counter, // Cycles remaining in the execution frame if we exit out due to a HLT
        cycle_offset;

    // ========================================================================
    // Protected Mode
    // ========================================================================

    // Control registers and debug registers
    uint32_t cr[8], dr[8];
    // Current privilege level
    //  - Real mode: Should be zero
    //  - Protected mode: Should be equal to cpu.seg[CS] & 3
    //  - Virtual 8086 mode: Should be three
    int cpl;

    // ========================================================================
    // Segmentation
    // ========================================================================

    // Value of segment selectors
    uint16_t seg[16];
    // Segment bases
    uint32_t seg_base[16];
    // Segment limit
    uint32_t seg_limit[16];
    // Segment access
    uint32_t seg_access[16];
    // Whether the segment is valid for reads of writes
    uint32_t seg_valid[16];

    // ========================================================================
    // Trace cache
    // ========================================================================

    // The number of instructions we have placed within the trace cache
    int trace_cache_usage;

    // The amount to shift the TLB tag by. See docs/cpu/tlb.md for details.
    int tlb_shift_read, tlb_shift_write;

    // ========================================================================
    // Memory Type Range Register (MTRR)
    // ========================================================================
    // TODO: We do not have data caches, so these are not actually used. 
    // However, some operating systems (i.e. Windows 7) do read/write them, so
    // we save their values to amuse them. 

    // There are two types of MTRRs -- fixed range and variable range. 
    uint64_t mtrr_fixed[32];
    uint64_t mtrr_variable_addr_mask[16]; // Order: addr[0], mask[0], addr[1], mask[1]
    uint64_t mtrr_deftype;

    // Related, but not identical -- Page Attribute Tables
    uint64_t page_attribute_tables;

    // ========================================================================
    // Miscellaneous state
    // ========================================================================

    // A20 mask for paging unit.
    uint32_t a20_mask;
    // APIC base register
    uint64_t apic_base;
    // Amount to fudge the TSC by (see ops/misc.c)
    uint64_t tsc_fudge;
    // Read result from access.c
    uint32_t read_result;
    // State of interrupt line
    int intr_line_state;
    // Set to 1 if interrupts are blocked for the next instruction
    int interrupts_blocked;
    // The reason why we exited from the CPU loop
    int exit_reason;

    // The IA32_EFER register that we don't actually use. 
    uint64_t ia32_efer;

    uint32_t sysenter[3]; // CS, ESP, and EIP for SYSENTER instruction.

    // <<< END STRUCT "struct" >>>

    // ========================================================================
    // Large tables
    // ========================================================================

    uint32_t smc_has_code_length;
    uint32_t* smc_has_code;

    uint32_t tlb_entry_count;
    uint32_t tlb_entry_indexes[MAX_TLB_ENTRIES];

    // TLB entries plus tags
    uint8_t tlb_tags[1 << 20];
#define TLB_ATTR_NX 1
#define TLB_ATTR_NON_GLOBAL 2
    // Interesting information on TLB
    uint8_t tlb_attrs[1 << 20];
    void* tlb[1 << 20];

    // Actual trace cache
    struct decoded_instruction trace_cache[TRACE_CACHE_SIZE];
    struct trace_info trace_info[TRACE_INFO_ENTRIES];
};
extern struct cpu cpu;

#define MEM32(e) *(uint32_t*)(cpu.mem + e)
#define MEM16(e) *(uint16_t*)(cpu.mem + e)
#ifdef LIBCPU
uint32_t cpulib_ptr_to_phys(void*);
#define PTR_TO_PHYS(ptr) cpulib_ptr_to_phys(ptr)
#else
// Converts pointer to a physical address
#define PTR_TO_PHYS(ptr) (uint32_t)(uintptr_t)((void*)ptr - cpu.mem)
#endif

// Based on the linear address, the TLB tag for this entry, and the shift for the current mode
#define TLB_ENTRY_INVALID8(addr, tag, shift) (tag >> shift & 1)
#define TLB_ENTRY_INVALID16(addr, tag, shift) ((addr | tag >> shift) & 1)
#define TLB_ENTRY_INVALID32(addr, tag, shift) ((addr | tag >> shift) & 3)

#define TLB_SYSTEM_READ 0
#define TLB_SYSTEM_WRITE 2
#define TLB_USER_READ 4
#define TLB_USER_WRITE 6

// Macros to get various views of EIP
#define PHYS_EIP() cpu.phys_eip
#define VIRT_EIP() (cpu.phys_eip + cpu.eip_phys_bias)
#define LIN_EIP() (cpu.phys_eip + cpu.eip_phys_bias + cpu.seg_base[CS])
// Modifys phys_eip accordingly
#define SET_VIRT_EIP(eip) \
    cpu.phys_eip += (eip)-VIRT_EIP();

// Exception handling helpers
#define EXCEPTION_HAS_ERROR_CODE 0x10000
#define EXCEPTION(e)         \
    do {                     \
        cpu_exception(e, 0); \
        EXCEPTION_HANDLER;   \
    } while (0)
#define EXCEPTION2(e, c)                                  \
    do {                                                  \
        cpu_exception(e, (c) | EXCEPTION_HAS_ERROR_CODE); \
        EXCEPTION_HANDLER;                                \
    } while (0)

#define LAUX_METHOD_MASK 63

#define EX_TS 10
#define EX_NP 11
#define EX_SS 12
#define EX_GP 13
#define EXCEPTION_DE() EXCEPTION(0) // Divide by zero
#define EXCEPTION_BR() EXCEPTION(5) // Bound range exceeded
#define EXCEPTION_UD() EXCEPTION(6) // Unknown/Undefined opcode
#define EXCEPTION_NM() EXCEPTION(7) // Device not available (usually for FPU)
#define EXCEPTION_DF() EXCEPTION2(8, 0) // Double fault
#define EXCEPTION_TS(c) EXCEPTION2(10, c) // Invalid TSS
#define EXCEPTION_NP(c) EXCEPTION2(11, c) // Descriptor not present
#define EXCEPTION_SS(c) EXCEPTION2(12, c) // Stack Segment fault
#define EXCEPTION_GP(c) EXCEPTION2(13, c) // General protection fault
#define EXCEPTION_PF(c) EXCEPTION2(14, c) // Page fault
#define EXCEPTION_MF(c) EXCEPTION(16) // x87 floating point exception

#define INTERNAL_CPU_LOOP_EXIT()                     \
    do {                                             \
        cpu.cycles += cpu_get_cycles() - cpu.cycles; \
        cpu.refill_counter = cpu.cycles_to_run - 1;  \
        cpu.cycles_to_run = 1;                       \
        cpu.cycle_offset = 1;                        \
    } while (0)

#define cpu_read8(linaddr, dest, shift)                                            \
    do {                                                                           \
        uint32_t addr_ = linaddr, shift_ = shift, tag = cpu.tlb_tags[addr_ >> 12]; \
        if (TLB_ENTRY_INVALID8(addr_, tag, shift_)) {                              \
            if (!cpu_access_read8(addr_, tag >> shift, shift))                     \
                dest = cpu.read_result;                                            \
            else                                                                   \
                EXCEPTION_HANDLER;                                                 \
        } else                                                                     \
            dest = *(uint8_t*)(cpu.tlb[addr_ >> 12] + addr_);                      \
    } while (0)
#define cpu_read16(linaddr, dest, shift)                                           \
    do {                                                                           \
        uint32_t addr_ = linaddr, shift_ = shift, tag = cpu.tlb_tags[addr_ >> 12]; \
        if (TLB_ENTRY_INVALID16(addr_, tag, shift_)) {                             \
            if (!cpu_access_read16(addr_, tag >> shift, shift))                    \
                dest = cpu.read_result;                                            \
            else                                                                   \
                EXCEPTION_HANDLER;                                                 \
        } else                                                                     \
            dest = *(uint16_t*)(cpu.tlb[addr_ >> 12] + addr_);                     \
    } while (0)
#define cpu_read32(linaddr, dest, shift)                                           \
    do {                                                                           \
        uint32_t addr_ = linaddr, shift_ = shift, tag = cpu.tlb_tags[addr_ >> 12]; \
        if (TLB_ENTRY_INVALID32(addr_, tag, shift_)) {                             \
            if (!cpu_access_read32(addr_, tag >> shift, shift))                    \
                dest = cpu.read_result;                                            \
            else                                                                   \
                EXCEPTION_HANDLER;                                                 \
        } else                                                                     \
            dest = *(uint32_t*)(cpu.tlb[addr_ >> 12] + addr_);                     \
    } while (0)
#define cpu_write8(linaddr, data, shift)                              \
    do {                                                              \
        uint32_t addr_ = linaddr, shift_ = shift, data_ = data,       \
                 tag = cpu.tlb_tags[addr_ >> 12];                     \
        if (TLB_ENTRY_INVALID8(addr_, tag, shift_)) {                 \
            if (cpu_access_write8(addr_, data_, tag >> shift, shift)) \
                EXCEPTION_HANDLER;                                    \
        } else                                                        \
            *(uint8_t*)(cpu.tlb[addr_ >> 12] + addr_) = data_;        \
    } while (0)
#define cpu_write16(linaddr, data, shift)                              \
    do {                                                               \
        uint32_t addr_ = linaddr, shift_ = shift, data_ = data,        \
                 tag = cpu.tlb_tags[addr_ >> 12];                      \
        if (TLB_ENTRY_INVALID16(addr_, tag, shift_)) {                 \
            if (cpu_access_write16(addr_, data_, tag >> shift, shift)) \
                EXCEPTION_HANDLER;                                     \
        } else                                                         \
            *(uint16_t*)(cpu.tlb[addr_ >> 12] + addr_) = data_;        \
    } while (0)
#define cpu_write32(linaddr, data, shift)                              \
    do {                                                               \
        uint32_t addr_ = linaddr, shift_ = shift, data_ = data,        \
                 tag = cpu.tlb_tags[addr_ >> 12];                      \
        if (TLB_ENTRY_INVALID32(addr_, tag, shift_)) {                 \
            if (cpu_access_write32(addr_, data_, tag >> shift, shift)) \
                EXCEPTION_HANDLER;                                     \
        } else                                                         \
            *(uint32_t*)(cpu.tlb[addr_ >> 12] + addr_) = data_;        \
    } while (0)

// Macros to help with segmentation

// Goes inside seg_valid array
#define SEG_VALID_READABLE 1
#define SEG_VALID_WRITABLE 2
#define SEG_VALID_INVALID 4

#ifdef SEGMENT_CHECKS
#define VALIDATE_SEG_READ(seg, addr, length)        \
    if (cpu_seg_verify_read(seg, addr, length - 1)) \
        EXCEPTION_HANDLER;
#define VALIDATE_SEG_WRITE(seg, addr, length - 1) \
    if (cpu_seg_verify_write(seg, 0))             \
        EXCEPTION_HANDLER;
#else
// Don't check segment limit and access
#define VALIDATE_SEG_READ(seg, addr, length) NOP()
#define VALIDATE_SEG_WRITE(seg, addr, length) NOP()
#endif

// decoder.c
int cpu_decode(struct trace_info* info, struct decoded_instruction* i);

// General execution
void cpu_execute(void);

// ops/stack.c
int cpu_push16(uint32_t data);
int cpu_push32(uint32_t data);
int cpu_pop16(uint16_t* dest);
int cpu_pop16_dest32(uint32_t* dest);
int cpu_pop32(uint32_t* dest);

// access.c
int cpu_access_read8(uint32_t addr, uint32_t tag, int shift);
int cpu_access_read16(uint32_t addr, uint32_t tag, int shift);
int cpu_access_read32(uint32_t addr, uint32_t tag, int shift);
int cpu_access_write8(uint32_t addr, uint32_t data, uint32_t tag, int shift);
int cpu_access_write16(uint32_t addr, uint32_t data, uint32_t tag, int shift);
int cpu_access_write32(uint32_t addr, uint32_t data, uint32_t tag, int shift);
int cpu_access_verify(uint32_t addr, uint32_t end, int shift);

// seg.c
void cpu_seg_load_virtual(int id, uint16_t sel);
void cpu_seg_load_real(int id, uint16_t sel);
int cpu_seg_load_protected(int id, uint16_t sel, struct seg_desc* info);
int cpu_seg_load_descriptor2(int table, uint32_t selector, struct seg_desc* seg, int exception, int code);
int cpu_seg_load_descriptor(uint32_t selector, struct seg_desc* seg, int exception, int code);
int cpu_seg_get_dpl(int seg);
uint32_t cpu_seg_get_base(struct seg_desc* info);
uint32_t cpu_seg_get_limit(struct seg_desc* info);
uint32_t cpu_seg_gate_target_segment(struct seg_desc* info);
uint32_t cpu_seg_gate_target_offset(struct seg_desc* info);
uint32_t cpu_seg_gate_parameter_count(struct seg_desc* info);
uint32_t cpu_seg_descriptor_address(int tbl, uint16_t sel);
int cpu_load_seg_value_mov(int seg, uint16_t val);
void cpu_load_csip_real(uint16_t cs, uint32_t eip);
void cpu_load_csip_virtual(uint16_t cs, uint32_t eip);
int cpu_load_csip_protected(uint16_t cs, struct seg_desc* info, uint32_t eip);

// smc.c
int cpu_smc_page_has_code(uint32_t phys);
int cpu_smc_has_code(uint32_t phys);
void cpu_smc_invalidate(uint32_t lin, uint32_t phys);
void cpu_smc_invalidate_page(uint32_t phys);
void cpu_smc_set_code(uint32_t phys);

// mmu.c
void cpu_mmu_tlb_flush(void);
void cpu_mmu_tlb_flush_nonglobal(void);
int cpu_mmu_translate(uint32_t lin, int shift);
void cpu_mmu_tlb_invalidate(uint32_t lin);

// trace.c
struct trace_info* cpu_trace_get_entry(uint32_t phys);
struct decoded_instruction* cpu_get_trace(void);
void cpu_trace_flush(void);

// eflags.c
int cpu_get_of(void);
int cpu_get_sf(void);
#define cpu_get_zf() (cpu.lr == 0)
int cpu_get_af(void);
int cpu_get_pf(void);
int cpu_get_cf(void);
void cpu_set_of(int);
void cpu_set_sf(int);
void cpu_set_zf(int);
void cpu_set_af(int);
void cpu_set_pf(int);
void cpu_set_cf(int);
uint32_t cpu_get_eflags(void);
void cpu_set_eflags(uint32_t);
int cpu_cond(int val);

// prot.c
int cpu_prot_set_cr(int cr, uint32_t v);
void cpu_prot_set_dr(int cr, uint32_t v);
void cpu_prot_update_cpl(void);

// ops/ctrlflow.c
void cpu_exception(int vec, int code);
int cpu_interrupt(int vector, int code, int type, int eip_to_push);

// SSE
void cpu_update_mxcsr(void);

// XXX
#ifndef CPUAPI_H
void cpu_debug(void);
uint64_t cpu_get_cycles(void);
#endif

#endif