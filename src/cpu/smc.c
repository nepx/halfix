// Self-modifying code support
// Note that writes to address beyond cpu.memory_size can be ignored because the translation system forbids translation from MMIO pages.
// Also, this subsystem cannot handle cross 128-byte accesses on its own. All unaligned accesses will be split up in access.c
#include "cpu/cpu.h"
int cpu_smc_page_has_code(uint32_t phys)
{
    phys >>= 12;
    if (phys >= cpu.smc_has_code_length)
        return 0;
    return cpu.smc_has_code[phys];
}

int cpu_smc_has_code(uint32_t phys)
{
    phys >>= 7;
    if ((phys >> 5) >= cpu.smc_has_code_length)
        return 0;
    return cpu.smc_has_code[phys >> 5] & (1 << (phys & 31));
}

void cpu_smc_set_code(uint32_t phys)
{
    phys >>= 7;
    if ((phys >> 5) >= cpu.smc_has_code_length)
        return;
    cpu.smc_has_code[phys >> 5] |= 1 << (phys & 31);
}

// The maximum trace length is 32 instructions, and instructions are a maximum of 15 bytes long. 32 * 15 = 480, and that rounds up to 512 bytes.
// If this value is set to zero, then only the last four 128-byte chunks are invalidated. If set to one, it will invalidate everything on the page until the start address
#define REMOVE_ALL_CODE_TRACES 1

void cpu_smc_invalidate(uint32_t lin, uint32_t phys)
{
    //printf("%08x %08x %08x %08x\n", lin, phys, cpu.smc_has_code_length, cpu.smc_has_code[phys >> 12]);
    uint32_t pageid = phys >> 12, page_info, p128, invmask, pagebase = phys & ~0xFFF;
    int start, end, quit = 0;

    if (pageid >= cpu.smc_has_code_length)
        return;
    page_info = cpu.smc_has_code[pageid];
    p128 = phys >> 7 & 31;
#if REMOVE_ALL_CODE_TRACES == 0
    start = p128 - 4; // Backtrack four 128-byte cache lines.
    if (start < 0)
        start = 0; // In the case that our page offset is < 512, simply clamp the value to zero
#else
    start = 0; // Start at zero, all the time.
#endif
    end = p128;

    {
        int endmask = 1 << end, startmask = 1 << start;
        if (start == 0)
            startmask = 0; // If end is 0 and start is 0, we have a prboelm here.
        endmask = endmask | (endmask - 1); // 8 --> (8 | 7) --> 15
        if (startmask != 0)
            startmask = startmask | (startmask - 1);
        invmask = endmask ^ startmask; // Clear all bits present in startmask and endmask to reveal the range we are clearing

        // If no code needs to be cleared, then don't waste time looping through it all
        if (!(page_info & invmask))
            return;
    }

    for (int i = start; i <= end; i++) {
        uint32_t mask = 1 << i;
        if (page_info & mask) {
            uint32_t physbase = pagebase + (i << 7);
            struct trace_info* info;
            for (int j = 0; j < 128; j++) {
                if ((info = cpu_trace_get_entry(physbase + j))) {
                    // See if trace intersects given physical EIP and if so, exit
                    if (!quit && phys >= info->phys && phys <= (info->phys + TRACE_LENGTH(info->flags)))
                        quit = 1;
                    info->phys = -1;
                }
            }
        }
    }

    page_info &= ~invmask;
    cpu.smc_has_code[pageid] = page_info;
    if (!page_info)
        cpu_mmu_tlb_invalidate(lin); // Retranslate the address so that there's no more code remaining

    if (quit)
        INTERNAL_CPU_LOOP_EXIT();
}
void cpu_smc_invalidate_page(uint32_t phys){
    uint32_t pageid = phys >> 12,
    page_info = cpu.smc_has_code[pageid], pagebase = phys & ~0xFFF, quit = 1;
    for (int i = 0; i < 31; i++) {
        uint32_t mask = 1 << i;
        if (page_info & mask) {
            uint32_t physbase = pagebase + (i << 7);
            struct trace_info* info;
            for (int j = 0; j < 128; j++) {
                if ((info = cpu_trace_get_entry(physbase + j))) {
                    // See if trace intersects given physical EIP and if so, exit
                    if (!quit && phys >= info->phys && phys <= (info->phys + TRACE_LENGTH(info->flags)))
                        quit = 1;
                    info->phys = -1;
                }
            }
        }
    }

    cpu.smc_has_code[pageid] = page_info;
    // TODO: invalidate TLB
    if (quit)
        INTERNAL_CPU_LOOP_EXIT();
}