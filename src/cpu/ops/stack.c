#include "cpu/cpu.h"
#define EXCEPTION_HANDLER return 1

int cpu_push16(uint32_t data)
{
    uint32_t esp = cpu.reg32[ESP], esp_mask = cpu.esp_mask, esp_minus_two = (esp - 2) & esp_mask;
    cpu_write16(esp_minus_two + cpu.seg_base[SS], data, cpu.tlb_shift_write);
    cpu.reg32[ESP] = esp_minus_two | (esp & ~esp_mask);
    return 0;
}
int cpu_push32(uint32_t data)
{
    uint32_t esp = cpu.reg32[ESP], esp_mask = cpu.esp_mask, esp_minus_four = (esp - 4) & esp_mask;
    cpu_write32(esp_minus_four + cpu.seg_base[SS], data, cpu.tlb_shift_write);
    cpu.reg32[ESP] = esp_minus_four | (esp & ~esp_mask);
    return 0;
}

int cpu_pop16(uint16_t* dest)
{
    uint32_t esp_mask = cpu.esp_mask, esp = cpu.reg32[ESP];
    cpu_read16((esp & esp_mask) + cpu.seg_base[SS], *dest, cpu.tlb_shift_read);
    cpu.reg32[ESP] = ((esp + 2) & esp_mask) | (esp & ~esp_mask);
    return 0;
}

// For when you need to pop a 16-bit quantity and clear the upper 16 bits of the result
// This function is exactly the same as pop16 dest32 except the destination is a uint32_t
int cpu_pop16_dest32(uint32_t* dest)
{
    uint32_t esp_mask = cpu.esp_mask, esp = cpu.reg32[ESP];
    cpu_read16((esp & esp_mask) + cpu.seg_base[SS], *dest, cpu.tlb_shift_read);
    cpu.reg32[ESP] = ((esp + 2) & esp_mask) | (esp & ~esp_mask);
    return 0;
}

int cpu_pop32(uint32_t* dest)
{
    uint32_t esp_mask = cpu.esp_mask, esp = cpu.reg32[ESP];
    cpu_read32((esp & esp_mask) + cpu.seg_base[SS], *dest, cpu.tlb_shift_read);
    cpu.reg32[ESP] = ((esp + 4) & esp_mask) | (esp & ~esp_mask);
    return 0;
}

int cpu_pusha(void)
{
    uint32_t esp_mask = cpu.esp_mask, esp = cpu.reg32[ESP];
    for (int i = 0; i < 16; i += 2) {
        esp = (esp - 2) & esp_mask;
        cpu_write16(esp + cpu.seg_base[SS], cpu.reg16[i], cpu.tlb_shift_write);
    }
    cpu.reg32[ESP] = esp | (cpu.reg32[ESP] & ~esp_mask);
    return 0;
}
int cpu_pushad(void)
{
    uint32_t esp_mask = cpu.esp_mask, esp = cpu.reg32[ESP];
    for (int i = 0; i < 8; i++) {
        esp = (esp - 4) & esp_mask;
        cpu_write32(esp + cpu.seg_base[SS], cpu.reg32[i], cpu.tlb_shift_write);
    }
    cpu.reg32[ESP] = esp | (cpu.reg32[ESP] & ~esp_mask);
    return 0;
}
int cpu_popa(void)
{
    uint32_t esp_mask = cpu.esp_mask, esp = cpu.reg32[ESP] & esp_mask;
    uint16_t temp16[8];
    for (int i = 7; i >= 0; i--) {
        cpu_read16(esp + cpu.seg_base[SS], temp16[i], cpu.tlb_shift_read);
        esp = (esp + 2) & esp_mask;
    }
    for (int i = 0; i < 8; i++) {
        if (i == 4)
            continue;
        cpu.reg16[i << 1] = temp16[i];
    }
    cpu.reg32[ESP] = esp | (cpu.reg32[ESP] & ~esp_mask);
    return 0;
}
int cpu_popad(void)
{
    uint32_t esp_mask = cpu.esp_mask, esp = cpu.reg32[ESP] & esp_mask;
    uint32_t temp32[8];
    for (int i = 7; i >= 0; i--) {
        cpu_read32(esp + cpu.seg_base[SS], temp32[i], cpu.tlb_shift_read);
        esp = (esp + 4) & esp_mask;
    }
    for (int i = 0; i < 8; i++) {
        if (i == 4)
            continue;
        cpu.reg32[i] = temp32[i];
    }
    cpu.reg32[ESP] = esp | (cpu.reg32[ESP] & ~esp_mask);
    return 0;
}