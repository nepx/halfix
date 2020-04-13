#include "cpu/cpu.h"
#include "io.h"

int cpu_access_read8(uint32_t addr, uint32_t tag, int shift)
{
    // Check for unmapped addresses
    if (tag & 2) {
        if (cpu_mmu_translate(addr, shift))
            return 1;
        tag = cpu.tlb_tags[addr >> 12] >> shift;
    }
    void* host_ptr = cpu.tlb[addr >> 12] + addr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    // Check for MMIO areas
    if ((phys >= 0xA0000 && phys < 0xC0000) || (phys >= cpu.memory_size)) {
        cpu.read_result = io_handle_mmio_read(phys, 0);
        return 0;
    }
    cpu.read_result = *(uint8_t*)host_ptr;
    return 0;
}
int cpu_access_read16(uint32_t addr, uint32_t tag, int shift)
{
    // Split across page boundaries.
    if (addr & 1) {
        uint32_t res = 0;
        for (int i = 0, j = 0; i < 2; i++, j += 8) {
            if (cpu_access_read8(addr + i, cpu.tlb_tags[(addr + i) >> 12] >> shift, shift))
                return 1;
            res |= cpu.read_result << j;
        }
        cpu.read_result = res;
        return 0;
    }

    if (tag & 2) {
        if (cpu_mmu_translate(addr, shift))
            return 1;
        tag = cpu.tlb_tags[addr >> 12] >> shift;
    }
    void* host_ptr = cpu.tlb[addr >> 12] + addr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    if ((phys >= 0xA0000 && phys < 0xC0000) || (phys >= cpu.memory_size)) {
        cpu.read_result = io_handle_mmio_read(phys, 1);
        return 0;
    }
    cpu.read_result = *(uint16_t*)host_ptr;
    return 0;
}
int cpu_access_read32(uint32_t addr, uint32_t tag, int shift)
{
    if (addr & 3) {
        uint32_t res = 0;
        for (int i = 0, j = 0; i < 4; i++, j += 8) {
            if (cpu_access_read8(addr + i, cpu.tlb_tags[(addr + i) >> 12] >> shift, shift))
                return 1;
            res |= cpu.read_result << j;
        }
        cpu.read_result = res;
        return 0;
    }

    if (tag & 2) {
        if (cpu_mmu_translate(addr, shift))
            return 1;
        tag = cpu.tlb_tags[addr >> 12] >> shift;
    }
    void* host_ptr = cpu.tlb[addr >> 12] + addr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    if ((phys >= 0xA0000 && phys < 0xC0000) || (phys >= cpu.memory_size)) {
        cpu.read_result = io_handle_mmio_read(phys, 2);
        return 0;
    }
    cpu.read_result = *(uint32_t*)host_ptr;
    return 0;
}

int cpu_access_write8(uint32_t addr, uint32_t data, uint32_t tag, int shift)
{
    if (tag & 2) {
        if (cpu_mmu_translate(addr, shift))
            return 1;
        tag = cpu.tlb_tags[addr >> 12] >> shift;
    }
    void* host_ptr = cpu.tlb[addr >> 12] + addr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);

    // Check for MMIO areas
    if ((phys >= 0xA0000 && phys < 0x100000) || (phys >= cpu.memory_size)) {
        io_handle_mmio_write(phys, data, 0);
        return 0;
    }
    if (cpu_smc_has_code(phys))
        cpu_smc_invalidate(addr, phys);
    *(uint8_t*)host_ptr = data;
    return 0;
}
int cpu_access_write16(uint32_t addr, uint32_t data, uint32_t tag, int shift)
{
    if (addr & 1) {
        for (int i = 0, j = 0; i < 2; i++, j += 8) {
            if (cpu_access_write8(addr + i, data >> j, cpu.tlb_tags[(addr + i) >> 12] >> shift, shift))
                return 1;
        }
        return 0;
    }
    if (tag & 2) {
        if (cpu_mmu_translate(addr, shift))
            return 1;
        tag = cpu.tlb_tags[addr >> 12] >> shift;
    }
    void* host_ptr = cpu.tlb[addr >> 12] + addr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    if ((phys >= 0xA0000 && phys < 0x100000) || (phys >= cpu.memory_size)) {
        io_handle_mmio_write(phys, data, 1);
        return 0;
    }
    if (cpu_smc_has_code(phys))
        cpu_smc_invalidate(addr, phys);
    *(uint16_t*)host_ptr = data;
    return 0;
}
int cpu_access_write32(uint32_t addr, uint32_t data, uint32_t tag, int shift)
{
    if (addr & 3) {
        for (int i = 0, j = 0; i < 4; i++, j += 8) {
            if (cpu_access_write8(addr + i, data >> j, cpu.tlb_tags[(addr + i) >> 12] >> shift, shift))
                return 1;
        }
        return 0;
    }

    if (tag & 2) {
        if (cpu_mmu_translate(addr, shift))
            return 1;
        tag = cpu.tlb_tags[addr >> 12] >> shift;
    }
    void* host_ptr = cpu.tlb[addr >> 12] + addr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    if ((phys >= 0xA0000 && phys < 0x100000) || (phys >= cpu.memory_size)) {
        io_handle_mmio_write(phys, data, 2);
        return 0;
    }
    if (cpu_smc_has_code(phys))
        cpu_smc_invalidate(addr, phys);
    *(uint32_t*)host_ptr = data;
    return 0;
}


// Verifies an address for read/write
int cpu_access_verify(uint32_t addr, uint32_t end, int shift)
{
    uint32_t tag;
    if ((addr ^ end) & ~0xFFF) {
        // Check two pages
        tag = cpu.tlb_tags[addr >> 12];
        if (tag & 2) {
            if (cpu_mmu_translate(addr, shift))
                return 1;
        }
    } else // Without this case, causes problems during Ubuntu boot
        end = addr;

    // Check the second page, or the first one if it's a single page access
    tag = cpu.tlb_tags[end >> 12];
    if (tag & 2) {
        if (cpu_mmu_translate(end, shift))
            return 1;
    }
    return 0;
}

// For debugging purposes. Call using GDB
#define EXCEPTION_HANDLER                                       \
    do {                                                        \
        printf("Unable to read memory at address %08x\n", lin); \
        return 0;                                               \
    } while (0)
uint8_t read8(uint32_t lin)
{
    uint8_t dest;
    cpu_read8(lin, dest, cpu.tlb_shift_read);
    return dest;
}
uint16_t read16(uint32_t lin)
{
    uint16_t dest;
    cpu_read16(lin, dest, cpu.tlb_shift_read);
    return dest;
}
uint32_t read32(uint32_t lin)
{
    uint32_t dest;
    cpu_read32(lin, dest, cpu.tlb_shift_read);
    return dest;
}

void readmem(uint32_t lin, int bytes)
{
    for (int i = 0; i < bytes; i++) {
        printf("%02x ", read8(lin + i));
    }
    printf("\n");
}
void readphys(uint32_t lin, int bytes)
{
    for (int i = 0; i < bytes; i++) {
        printf("%02x ", cpu.mem8[lin + i]);
    }
    printf("\n");
}

uint32_t lin2phys(uint32_t addr)
{
    uint8_t tag = cpu.tlb_tags[addr >> 12];
    if (tag & 2) {
        if (cpu_mmu_translate(addr, TLB_SYSTEM_READ)) {
            printf("ERROR TRANSLATING ADDRESS %08x\n", addr);
            return 1;
        }
        tag = cpu.tlb_tags[addr >> 12] >> TLB_SYSTEM_READ;
    }
    void* host_ptr = cpu.tlb[addr >> 12] + addr;
    uint32_t phys = PTR_TO_PHYS(host_ptr);
    return phys;
}