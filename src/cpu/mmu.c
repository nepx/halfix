// Handles memory mapping
#include "cpu/cpu.h"
#include "cpu/instrument.h"
#include "io.h"

#define EXCEPTION_HANDLER return 1

void cpu_mmu_tlb_flush(void)
{
    for (unsigned int i = 0; i < cpu.tlb_entry_count; i++) {
        uint32_t entry = cpu.tlb_entry_indexes[i];
        cpu.tlb[entry] = NULL;
        cpu.tlb_tags[entry] = 0xFF;
        cpu.tlb_entry_indexes[i] = -1;
    }
    cpu.tlb_entry_count = 0;
}

static void cpu_set_tlb_entry(uint32_t lin, uint32_t phys, int user, int write)
{
    // Mask out the A20 gate line here so that we don't have to do it after every access
    phys &= cpu.a20_mask;

    if (phys >= 0xFFF00000)
        phys &= 0xFFFFF;

    // Fill the mask value with the proper tags.
    int tag = 0, tag_write = 0;
    if (phys >= 0xA0000 && phys < 0x100000) {
        tag = (phys & 0x40000) == 0;
        tag_write = 1;
    }
    if (phys >= cpu.memory_size) {
        tag = 1;
        tag_write = 1;
    }

    if (cpu_smc_page_has_code(phys)) {
        // Make sure that the flag is set.
        tag_write = 1;
    }

    if (cpu.tlb_entry_count >= MAX_TLB_ENTRIES) // Flush TLB
        cpu_mmu_tlb_flush();

    int system_read = tag << TLB_SYSTEM_READ,
        system_write = (tag_write | (!write ? 3 : 0)) << TLB_SYSTEM_WRITE,
        user_read = (tag | (!user ? 3 : 0)) << TLB_USER_READ,
        user_write = (tag_write | (!user | !write ? 3 : 0)) << TLB_USER_WRITE;

    uint32_t entry = lin >> 12;
    cpu.tlb_entry_indexes[cpu.tlb_entry_count++] = entry;
    cpu.tlb[entry] = (void*)(((uintptr_t)cpu.mem + phys) - lin);
    cpu.tlb_tags[entry] = system_read | system_write | user_read | user_write;
}

static uint32_t cpu_read_phys(uint32_t addr){
    if(addr >= cpu.memory_size || (addr >= 0xA0000 && addr < 0xC0000))
        return io_handle_mmio_read(addr, 2);
    else return MEM32(addr);
}
static void cpu_write_phys(uint32_t addr, uint32_t data){
    if(addr >= cpu.memory_size || (addr >= 0xA0000 && addr < 0xC0000))
        io_handle_mmio_write(addr, data, 2);
    else MEM32(addr) = data;
}

// Converts linear to physical address.
int cpu_mmu_translate(uint32_t lin, int shift)
{
    if (!(cpu.cr[0] & CR0_PG)) {
        cpu_set_tlb_entry(lin & ~0xFFF, lin & ~0xFFF, 1, 1);
        return 0; // No page faults at all!
    } else {
        // https://wiki.osdev.org/Paging
        // If we do end up page faulting, #PF will push an error code to stack
        int error_code = 0;

        // Determine whether we are reading or writing
        // 0: Supervisor read
        // 2: Supervisor write
        // 4: User read
        // 6: User write
        int write = shift >> 1 & 1, user = shift >> 2 & 1;

        uint32_t page_directory_entry_addr = cpu.cr[3] + (lin >> 20 & 0xFFC),
                 page_directory_entry = -1, page_table_entry_addr = -1, page_table_entry = -1;

        page_directory_entry = cpu_read_phys(page_directory_entry_addr);
        
        if (!(page_directory_entry & 1)) {
            // Not present
            CPU_LOG("#PF: PDE not present\n");
            error_code = 0;
            goto page_fault;
        }

        // Page directory entry seems to be OK, so now check page table entry
        page_table_entry_addr = ((lin >> 10 & 0xFFC) + (page_directory_entry & ~0xFFF));
        page_table_entry = -1;

        // If PSE, then return a single large page
        if (page_directory_entry & 0x80 && cpu.cr[4] & CR4_PSE) {
                uint32_t new_page_dierctory_entry = page_directory_entry | 0x20 | (write << 6);
            if (new_page_dierctory_entry != page_directory_entry) {
                cpu_write_phys(page_directory_entry_addr, new_page_dierctory_entry);
#ifdef INSTRUMENT
                cpu_instrument_paging_modified(page_directory_entry_addr);
#endif
            }
            uint32_t phys = (page_directory_entry & 0xFFC00000) | (lin & 0x3FF000);
            cpu_set_tlb_entry(lin & ~0xFFF, phys, user, write);
        } else {
            page_table_entry = cpu_read_phys(page_table_entry_addr);

            // Check for existance
            if ((page_table_entry & 1) == 0) {
                CPU_LOG("#PF: PTE not present\n");
                error_code = 0;
                goto page_fault;
            }

            // The page directory and page entry are almost identical, so why not just find the bits that are set?
            // Also, find the NOT at the same time to make finding bits easier.
            // Normally, we check if bits are not set, but if we NOT the value, then we can simply find out which bits are set.
            uint32_t combined = ~page_table_entry | ~page_directory_entry;

            int write_mask = write << 1;
            if (combined & write_mask) { // Normally (combined & 2) == 0
                // So far, we have determined that
                //  - Bit 1 of either PTE or PDE is 0 (because bit 1 of inverse is 1)
                //  - Write is set (because write_mask != 0)

                // Supervisor can write to read-only pages if and only if CR0.WP is clear
                if (user || (cpu.cr[0] & CR0_WP)) {
                    CPU_LOG("#PF: Illegal write\n");
                    error_code = 1;
                    goto page_fault;
                }

                // If we are still here, then that means that we are a supervisor and CR0.WP is not set, so we're allowed to write to the page
            }

            int user_mask = user << 2;
            if (combined & user_mask) { // Normally (combined & 4) == 0
                // So far, we have determined that
                //  - Bit 2 of either PTE or PDE is 0 (because bit 2 of inverse is 4)
                //  - User is set (because user_mask != 0)
                CPU_LOG("#PF: User trying to write to supervisor page\n");
                error_code = 1;
                goto page_fault;
            }

            // Note: Accessed bits should only be set if there wasn't any page fault.
            if ((page_directory_entry & 0x20) == 0) {
                cpu_write_phys(page_directory_entry_addr, page_directory_entry | 0x20);
#ifdef INSTRUMENT
                cpu_instrument_paging_modified(page_directory_entry_addr);
#endif
            }
            uint32_t new_page_table_entry = page_table_entry | (write << 6) | 0x20; // Set dirty bit and accessed bit, if needed
            if (new_page_table_entry != page_table_entry) {
                cpu_write_phys(page_table_entry_addr, new_page_table_entry);
#ifdef INSTRUMENT
                cpu_instrument_paging_modified(page_table_entry_addr);
#endif
            }
        cpu_set_tlb_entry(lin & ~0xFFF, page_table_entry & ~0xFFF, user, write);
        }
        return 0;
    // A page fault has occurred
    page_fault:
        cpu.cr[2] = lin;
        error_code |= (write << 1) | (user << 2);
        CPU_LOG(" ---- Page fault information dump ----\n");
        CPU_LOG("PDE Entry addr: %08x PDE Entry: %08x\n", page_directory_entry_addr << 2, page_directory_entry);
        CPU_LOG("PTE Entry addr: %08x PTE Entry: %08x\n", page_table_entry_addr << 2, page_table_entry);
        CPU_LOG("Address to translate: %08x [%s %sing]\n", lin, user ? "user" : "kernel", write ? "writ" : "read");
        CPU_LOG("CR3: %08x CPL: %d\n", cpu.cr[3], cpu.cpl);
        CPU_LOG("EIP: %08x ESP: %08x\n", VIRT_EIP(), cpu.reg32[ESP]);
        //if(lin == 0x77f7f000/* || lin == 0x77f7efff*/)
        //    __asm__("int3");
        //if(cpu.cpl == 3 && !user) __asm__("int3");
        EXCEPTION_PF(error_code);
        return -1; // Never reached
    }
}

void cpu_mmu_tlb_invalidate(uint32_t lin)
{
    lin >>= 12;
#if 0
    if(cpu.cr[4] & CR4_PSE){
        uint32_t linbase = lin & ~1023;
        for(int i=0;i<1024;i++){
            cpu.tlb[i + linbase] = NULL;
            cpu.tlb_tags[i + linbase] = 0xFF;
        }
        return;
    }
#endif
    cpu.tlb[lin] = NULL;
    cpu.tlb_tags[lin] = 0xFF;
}