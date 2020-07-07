// Handles memory mapping
#include "cpu/cpu.h"
#include "cpu/instrument.h"
#include "io.h"

#define EXCEPTION_HANDLER return 1

#ifdef LIBCPU
void* get_phys_ram_ptr(uint32_t addr, int write);
void* get_lin_ram_ptr(uint32_t addr, int flags, int* fault);
#else
#define get_phys_ram_ptr(a, b) (cpu.mem + a)
#define get_lin_ram_ptr(a, b) NULL
#endif

void cpu_mmu_tlb_flush(void)
{
    for (unsigned int i = 0; i < cpu.tlb_entry_count; i++) {
        uint32_t entry = cpu.tlb_entry_indexes[i];
        if (entry == (uint32_t)-1)
            continue; // Don't flush entries we have already flushed
        cpu.tlb[entry] = NULL;
        cpu.tlb_tags[entry] = 0xFF;
        cpu.tlb_entry_indexes[i] = -1;
        cpu.tlb_attrs[entry] = 0xFF;
    }
    cpu.tlb_entry_count = 0;
}
void cpu_mmu_tlb_flush_nonglobal(void)
{
    for (unsigned int i = 0; i < cpu.tlb_entry_count; i++) {
        uint32_t entry = cpu.tlb_entry_indexes[i];
        if (entry == (uint32_t)-1)
            continue; // Don't flush entries we have already flushed
        if ((cpu.tlb_attrs[entry] & TLB_ATTR_NON_GLOBAL) == 0)
            continue;
        cpu.tlb[entry] = NULL;
        cpu.tlb_tags[entry] = 0xFF;
        cpu.tlb_entry_indexes[i] = -1;
        cpu.tlb_attrs[entry] = 0xFF;
    }
    cpu.tlb_entry_count = cpu.tlb_entry_count; // We may still have global entries.
}

static void cpu_set_tlb_entry(uint32_t lin, uint32_t phys, void* ptr, int user, int write, int global, int nx)
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

    if (cpu.tlb_entry_count >= MAX_TLB_ENTRIES) { // Flush TLB
        cpu_mmu_tlb_flush();
#ifdef INSTRUMENT
        cpu_instrument_tlb_full();
#endif
    }

    int system_read = tag << TLB_SYSTEM_READ,
        system_write = (tag_write | (!write ? 3 : 0)) << TLB_SYSTEM_WRITE,
        user_read = (tag | (!user ? 3 : 0)) << TLB_USER_READ,
        user_write = (tag_write | ((!user | !write) ? 3 : 0)) << TLB_USER_WRITE;

    uint32_t entry = lin >> 12;
    cpu.tlb_entry_indexes[cpu.tlb_entry_count++] = entry;
    cpu.tlb_attrs[entry] = (nx ? TLB_ATTR_NX : 0) | (global ? 0 : TLB_ATTR_NON_GLOBAL);
    if (!ptr)
        ptr = get_phys_ram_ptr(phys, write);
    cpu.tlb[entry] = (void*)(((uintptr_t)ptr) - lin);
    cpu.tlb_tags[entry] = system_read | system_write | user_read | user_write;
}

uint32_t cpu_read_phys(uint32_t addr)
{
    if (addr >= cpu.memory_size || (addr >= 0xA0000 && addr < 0xC0000))
        return io_handle_mmio_read(addr, 2);
    else
        return MEM32(addr);
}
static void cpu_write_phys(uint32_t addr, uint32_t data)
{
    if (addr >= cpu.memory_size || (addr >= 0xA0000 && addr < 0xC0000))
        io_handle_mmio_write(addr, data, 2);
    else
        MEM32(addr) = data;
}

// Checks reserved fields for error. disable for speed.
#define PAE_HANDLE_RESERVED 1

// Converts linear to physical address.
int cpu_mmu_translate(uint32_t lin, int shift)
{
#ifdef LIBCPU
    int fault;
    void* ptr = get_lin_ram_ptr(lin & ~0xFFF, shift, &fault);
    if (!ptr) {
        if (fault)
            EXCEPTION_PF(0);
        // Otherwise, continue
    } else {
        int write = shift >> 1 & 1, user = shift >> 2 & 1;
        cpu_set_tlb_entry(lin & ~0xFFF, lin & ~0xFFF, ptr, write, user, 0, 0);

        return 0;
    }
#endif
    if (!(cpu.cr[0] & CR0_PG)) {
        cpu_set_tlb_entry(lin & ~0xFFF, lin & ~0xFFF, NULL, 1, 1, 0, 0);
        return 0; // No page faults at all!
    } else {
        int execute = shift & 8;
        shift &= 7;
        // Determine whether we are reading or writing
        // 0: Supervisor read
        // 2: Supervisor write
        // 4: User read
        // 6: User write
        int write = shift >> 1 & 1, user = shift >> 2 & 1;

        if (!(cpu.cr[4] & CR4_PAE)) {
            // https://wiki.osdev.org/Paging
            // If we do end up page faulting, #PF will push an error code to stack
            int error_code = 0;

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
                cpu_set_tlb_entry(lin & ~0xFFF, phys, NULL, user, write, page_directory_entry & 0x100, 0);
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
                //if(lin == 0xe1001332) __asm__("int3");
                cpu_set_tlb_entry(lin & ~0xFFF, page_table_entry & ~0xFFF, NULL, user, write, page_table_entry & 0x100, 0);
            }
            return 0;
        // A page fault has occurred
        page_fault:
            cpu.cr[2] = lin;
            error_code |= (write << 1) | (user << 2);
            CPU_LOG(" ---- Page fault information dump ----\n");
            CPU_LOG("PDE Entry addr: %08x PDE Entry: %08x\n", page_directory_entry_addr, page_directory_entry);
            CPU_LOG("PTE Entry addr: %08x PTE Entry: %08x\n", page_table_entry_addr, page_table_entry);
            CPU_LOG("Address to translate: %08x [%s %sing]\n", lin, user ? "user" : "kernel", write ? "writ" : "read");
            CPU_LOG("CR3: %08x CPL: %d\n", cpu.cr[3], cpu.cpl);
            CPU_LOG("EIP: %08x ESP: %08x\n", VIRT_EIP(), cpu.reg32[ESP]);
            //if(cpu.cpl == 3 && !user) __asm__("int3");
            EXCEPTION_PF(error_code);
            return -1; // Never reached
        } else {
            // PAE enabled
            // http://www.rcollins.org/ddj/Jul96/
            // https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf (page 117)
            // Note that we only support 3 GB of RAM at max, so we're OK with ignoring the top bits
            uint32_t pdp_addr = (cpu.cr[3] & ~31) | (lin >> 27 & 0x18),
                     pdpte = cpu_read_phys(pdp_addr);
            int fail = (write << 1) | (user << 2);
            if ((pdpte & 1) == 0)
                goto pae_page_fault;
#if PAE_HANDLE_RESERVED
            // "Writing to reserved bits in the PDPT generates a general protection fault (#GP),"
            if (cpu_read_phys(pdp_addr + 4) & ~15)
                EXCEPTION_GP(0);
#endif
            // Now look up page directory entry (which may end up being a page table entry, if we're lucky)
            uint32_t pde_addr = (pdpte & ~0xFFF) | (lin >> 18 & 0xFF8),
                     pde = cpu_read_phys(pde_addr), pde2 = cpu_read_phys(pde_addr + 4);

            // XXX yucky yucky
            uint32_t nx_mask = -1 ^ (cpu.ia32_efer << 20 & 0x80000000);

            // Check if our address is too
            if (cpu_read_phys(pdp_addr + 4) & ~15 & nx_mask)
                EXCEPTION_GP(0);
#if PAE_HANDLE_RESERVED
            if (pde2 & ~15 & nx_mask)
                EXCEPTION_GP(0);
#endif

            int nx_enabled = cpu.ia32_efer >> 11 & 1, nx = (pde2 >> 31) & nx_enabled;
            fail |= (execute && nx_enabled) << 4;

            if ((pde & 1) == 0) {
                fail |= 0;
                goto pae_page_fault;
            }

            uint32_t flags = ~pde;
            if ((write << 1) & flags) {
                if (user || (cpu.cr[0] & CR0_WP)) {
                    CPU_LOG("#PF: [PAE] Illegal write\n");
                    fail |= 1;
                    goto pae_page_fault;
                }
            }
            if ((user << 2) & flags) {
                CPU_LOG("#PF: User trying to write to supervisor page\n");
                fail |= 1;
                goto pae_page_fault;
            }

            if (pde & (1 << 7)) {
                // 2 MB page
                // Write back
                uint32_t new_pde = pde | 0x20 | (write << 6);
                if (new_pde != pde) {
                    cpu_write_phys(pde_addr, new_pde);
#ifdef INSTRUMENT
                    cpu_instrument_paging_modified(pde_addr);
#endif
                }
                uint32_t phys = (pde & 0xFFE00000) | (lin & 0x1FF000);
                cpu_set_tlb_entry(lin & ~0xFFF, phys, NULL, user, write, pde & 0x100, nx);
            } else {
                uint32_t pte_addr = (pde & ~0xFFF) | (lin >> 9 & 0xFF8),
                         pte = cpu_read_phys(pte_addr), pte2 = cpu_read_phys(pte_addr + 4);

                if ((pte & 1) == 0)
                    goto pae_page_fault;
                UNUSED(pte2);

                flags = ~pte;
                if ((write << 1) & flags) {
                    if (user || (cpu.cr[0] & CR0_WP)) {
                        CPU_LOG("#PF: [PAE] Illegal write\n");
                        fail |= 1;
                        goto pae_page_fault;
                    }
                }
                if ((user << 2) & flags) {
                    CPU_LOG("#PF: User trying to write to supervisor page\n");
                    fail |= 1;
                    goto pae_page_fault;
                }
                uint32_t new_pde = pde | 0x20;
                if (new_pde != pde) {
                    cpu_write_phys(pde_addr, new_pde);
#ifdef INSTRUMENT
                    cpu_instrument_paging_modified(pde_addr);
#endif
                }
                uint32_t new_pte = pte | 0x20 | (write << 6);
                if (new_pte != pte) {
                    cpu_write_phys(pte_addr, new_pte);
#ifdef INSTRUMENT
                    cpu_instrument_paging_modified(pte_addr);
#endif
                }
#if 0
                printf("Lin: %08x\n", lin);
                printf("PDTPE: %08x PDTPE.addr: %08x\n", pdpte, pdp_addr);
                printf("PDE: %08x PDE.addr: %08x\n", pde, pde_addr);
                printf("PTE: %08x PTE.addr: %08x\n", pte, pte_addr);
#endif
                cpu_set_tlb_entry(lin & ~0xFFF, pte & ~0xFFF, NULL, user, write, pte & 0x100, nx);
            }
            return 0;
        pae_page_fault:
            cpu.cr[2] = lin;
            //if(lin == 0xbfd8efff) __asm__("int3");
            CPU_LOG("CR2: %08x\n", cpu.cr[2]);
            //if((lin & ~0xFFF) == 0x01058000) __asm__("int3");
            // i have no idea if this is right
            EXCEPTION_PF(fail);
        }
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