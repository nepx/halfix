// Protected mode helper functions
#include "cpu/cpu.h"

int cpu_prot_set_cr(int cr, uint32_t v){
    uint32_t diffxor = v ^ cpu.cr[cr];
    cpu.cr[cr] = v;
    // TODO: #GP on invalid bits
    switch(cr){
        case 0:
            if(diffxor & CR0_PG)
            cpu_mmu_tlb_flush();
            break;
        case 3: // PDBR
            cpu.cr[3] &= ~0xFFF;
            cpu_mmu_tlb_flush();
            break;
        case 4: 
            if(diffxor & CR4_PSE) cpu_mmu_tlb_flush();
    }
    return 0;
}


void cpu_prot_set_dr(int id, uint32_t val)
{
    uint32_t xorvec = cpu.dr[id] ^ val;
    if (xorvec) {
        // TODO...
        switch (id) {
        case 6:
            cpu.dr[6] = (cpu.dr[6] & 0xffff0ff0) | (val & 0xE00F);
            break;
        case 7:
            cpu.dr[7] = (val & 0xffff2fff) | 0x400;
            break;
        default:
            cpu.dr[id] = val;
            break;
        }
    }
}

// Update a few constants
void cpu_prot_update_cpl(void)
{
    if (cpu.cpl == 3) {
        cpu.tlb_shift_read = TLB_USER_READ;
        cpu.tlb_shift_write = TLB_USER_WRITE;
    } else {
        cpu.tlb_shift_read = TLB_SYSTEM_READ;
        cpu.tlb_shift_write = TLB_SYSTEM_WRITE;
    }
}