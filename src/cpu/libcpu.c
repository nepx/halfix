// CPU library stubs
// If you want detailed reporting on CPU events, look into CPU instrumentation (see cpu/instrumentation.h)

#include "cpu/libcpu.h"
#include "cpu/cpu.h"
#include "cpuapi.h"
#include "io.h"
#include "state.h"
#include "util.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#define IMPORT __declspec(dllimport)
#elif defined(EMSCRIPTEN)
#include <emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT __attribute__((visibility("default")))
#define IMPORT __attribute__((visibility("default")))
#endif

#define LIBCPUPTR_LOG(x, ...) LOG("LIBCPU", x, ##__VA_ARGS__)
#define LIBCPUPTR_DEBUG(x, ...) LOG("LIBCPU", x, ##__VA_ARGS__)
#define LIBCPUPTR_FATAL(x, ...) \
    FATAL("LIBCPU", x, ##__VA_ARGS__)

static uint32_t dummy_read(uint32_t addr)
{
    return addr | -1;
}
static void dummy_write(uint32_t addr, uint32_t data)
{
    UNUSED(addr | data);
}
static uint32_t dummy_mmio_read(uint32_t addr, int size)
{
    return addr | -1 | size;
}
static void dummy_mmio_write(uint32_t addr, uint32_t data, int size)
{
    UNUSED(addr | data | size);
}
static void dummy(void)
{
    return;
}

static mmio_read_handler mmio_read = dummy_mmio_read;
static mmio_write_handler mmio_write = dummy_mmio_write;
static io_read_handler io_read8 = dummy_read, io_read16 = dummy_read, io_read32 = dummy_read;
static io_write_handler io_write8 = dummy_write, io_write16 = dummy_write, io_write32 = dummy_write;
static abort_handler onabort = dummy, pic_ack = dummy, fpu_irq = dummy;
static mem_refill_handler mrh = NULL, mrh_lin = NULL;
static ptr_to_phys_handler ptph = NULL;
static int apic_enabled;

uint32_t cpulib_ptr_to_phys(void* p)
{
    if (ptph)
        return ptph(p);
    else
        return (uint32_t)((uintptr_t)p - (uintptr_t)cpu.mem);
}

EXPORT
void cpu_register_ptr_to_phys(ptr_to_phys_handler h)
{
    ptph = h;
}

EXPORT
void cpu_register_mem_refill_handler(mem_refill_handler h)
{
    mrh = h;
}
EXPORT
void cpu_register_lin_refill_handler(mem_refill_handler h)
{
    mrh_lin = h;
}

EXPORT
void cpu_register_mmio_read_cb(mmio_read_handler h)
{
    mmio_read = h;
}
EXPORT
void cpu_register_mmio_write_cb(mmio_write_handler h)
{
    mmio_write = h;
}
EXPORT
void cpu_register_io_read_cb(io_read_handler h, int size)
{
    switch (size) {
    case 8:
        io_read8 = h;
        break;
    case 16:
        io_read16 = h;
        break;
    case 32:
        io_read32 = h;
        break;
    }
}
EXPORT
void cpu_register_io_write_cb(io_write_handler h, int size)
{
    switch (size) {
    case 8:
        io_write8 = h;
        break;
    case 16:
        io_write16 = h;
        break;
    case 32:
        io_write32 = h;
        break;
    }
}
EXPORT
void cpu_register_onabort(abort_handler h)
{
    onabort = h;
}
EXPORT
void cpu_register_pic_ack(abort_handler h)
{
    pic_ack = h;
}
EXPORT
void cpu_register_fpu_irq(abort_handler h)
{
    fpu_irq = h;
}

EXPORT
void cpu_enable_apic(int enabled)
{
    apic_enabled = enabled;
}

static int irq_line = -1;

EXPORT
void cpu_raise_irq_line(int irq)
{
    irq_line = irq;
    cpu_raise_intr_line();
}
EXPORT
void cpu_lower_irq_line(void)
{
    irq_line = -1;
}

// io.c functions

// handle mmio read
EXPORT
uint32_t io_handle_mmio_read(uint32_t addr, int size)
{
    return mmio_read(addr, size);
}
// handle mmio write
EXPORT
void io_handle_mmio_write(uint32_t addr, uint32_t data, int size)
{
    mmio_write(addr, data, size);
}
// handle io read
EXPORT
uint8_t io_readb(uint32_t addr)
{
    return io_read8(addr);
}
EXPORT
uint16_t io_readw(uint32_t addr)
{
    return io_read16(addr);
}
EXPORT
uint32_t io_readd(uint32_t addr)
{
    return io_read32(addr);
}
EXPORT
void io_writeb(uint32_t addr, uint8_t data)
{
    io_write8(addr, data);
}
EXPORT
void io_writew(uint32_t addr, uint16_t data)
{
    io_write16(addr, data);
}
EXPORT
void io_writed(uint32_t addr, uint32_t data)
{
    io_write32(addr, data);
}

EXPORT
int cpu_core_run(int cycles)
{
    return cpu_run(cycles);
}

void util_abort(void)
{
    // Notify host and then abort
    onabort();
    abort();
}

// pic.c
int pic_get_interrupt(void)
{
    if (irq_line < 0)
        LIBCPUPTR_FATAL("Error: Spurious IRQ\n");
    pic_ack();
    return irq_line;
}

// apic.c
int apic_is_enabled(void)
{
    return apic_enabled;
}

void state_register(state_handler s)
{
    UNUSED(s);
}

void io_register_reset(io_reset cb)
{
    cb();
}

void* get_phys_ram_ptr(uint32_t addr, int write)
{
    if (mrh == NULL) {
        return cpu.mem + addr;
    } else
        return mrh(addr, write);
}
void* get_lin_ram_ptr(uint32_t addr, int flags, int* fault)
{
    if (mrh_lin == NULL) {
        *fault = 0;
        return NULL;
    } else {
#define EXCEPTION_HANDLER return NULL
        void* dest = mrh_lin(addr, flags);
        *fault = dest == NULL;
        return dest;
#undef EXCEPTION_HANDLER
    }
}

void pic_lower_irq(void)
{
    // Do nothing
}
void pic_raise_irq(int dummy)
{
    UNUSED(dummy);
    fpu_irq();
}
EXPORT
void* cpu_get_state_ptr(int id)
{
    switch (id) {
    case CPUPTR_GPR:
        return cpu.reg32;
    case CPUPTR_XMM:
        return cpu.xmm32;
    case CPUPTR_MXCSR:
        return &cpu.mxcsr;
    case CPUPTR_SEG_DESC:
        return cpu.seg;
    case CPUPTR_SEG_LIMIT:
        return cpu.seg_limit;
    case CPUPTR_SEG_BASE:
        return cpu.seg_base;
    case CPUPTR_SEG_ACCESS:
        return cpu.seg_access;
    case CPUPTR_MTRR_FIXED:
        return cpu.mtrr_fixed;
    case CPUPTR_MTRR_VARIABLE:
        return cpu.mtrr_variable_addr_mask;
    case CPUPTR_MTRR_DEFTYPE:
        return &cpu.mtrr_deftype;
    case CPUPTR_PAT:
        return &cpu.page_attribute_tables;
    case CPUPTR_APIC_BASE:
        return &cpu.apic_base;
    case CPUPTR_SYSENTER_INFO:
        return cpu.sysenter;
    }
    return NULL;
}
EXPORT
uint32_t cpu_get_state(int id)
{
    switch (id & 15) {
    case CPU_EFLAGS:
        return cpu_get_eflags();
    case CPU_EIP:
        return VIRT_EIP();
    case CPU_LINEIP:
        return LIN_EIP();
    case CPU_PHYSEIP:
        return PHYS_EIP();
    case CPU_CPL:
        return cpu.cpl;
    case CPU_STATE_HASH:
        return cpu.state_hash;
    case CPU_SEG:
        return cpu.seg[id >> 4 & 15];
    case CPU_A20:
        return cpu.a20_mask >> 20 & 1;
    case CPU_CR:
        return cpu.cr[id >> 4 & 7];
    }
    return 0;
}
EXPORT
int cpu_set_state(int id, uint32_t value)
{
    switch (id & 15) {
    case CPU_EFLAGS:
        cpu_set_eflags(value);
        break;
    case CPU_EIP:
        SET_VIRT_EIP(value);
        break;
    case CPU_LINEIP:
        LIBCPUPTR_LOG("Cannot set linear EIP directly -- modify CS and EFLAGS\n");
        return 1;
    case CPU_PHYSEIP:
        cpu.phys_eip = value;
        //cpu.eip_phys_bias = value ^ 0x1000;
        break;
    case CPU_CPL:
        cpu.cpl = value & 3;
        cpu_prot_update_cpl();
        break;
    case CPU_STATE_HASH:
        cpu.state_hash = value;
        break;
    case CPU_SEG:
        if (cpu.cr[0] & CR0_PE) {
            if (cpu.eflags & EFLAGS_VM)
                cpu_seg_load_virtual(id >> 4 & 15, value);
            else {
                struct seg_desc desc;
                if (cpu_seg_load_descriptor(value, &desc, EX_GP, value & 0xFFFC))
                    return 1;
                return cpu_seg_load_protected(id >> 4 & 15, value, &desc);
            }
        } else
            cpu_seg_load_real(id >> 4 & 15, value);
        break;
    case CPU_A20:
        cpu_set_a20(value & 1);
        break;
    case CPU_CR:
        return cpu_prot_set_cr(id >> 4 & 15, value);
    }
    return 0;
}

EXPORT
void cpu_init_32bit(void)
{
    cpu.a20_mask = -1;
    cpu.cr[0] = 1; // How you do paging is up to you
    // Initialize EFLAGS with IF
    cpu.eflags = 0x202;
    // Set ESP to 32-bit
    cpu.esp_mask = -1;
    // Set translation mode to 32-bit
    cpu.state_hash = 0;
    cpu.memory_size = -1;

    cpu.seg_base[CS] = cpu.seg_base[DS] = cpu.seg_base[SS] = 0;
    cpu.seg_limit[CS] = cpu.seg_limit[DS] = cpu.seg_limit[SS] = -1;

    // access doesn't matter here.
}
void fpu_init_lib(void);

EXPORT
void cpu_init_fpu(void)
{
    fpu_init_lib();
}

static uint8_t mem[4096];
static void dummy_write2(uint32_t addr, uint32_t data)
{
    printf("Port: %02x Data: %04x\n", addr, data);
}
static void* mem_stuff(uint32_t addr, int write)
{
    UNUSED(write);
    return mem + addr;
}

static void test(void)
{
    cpu_init();
    uint32_t* regs = cpu_get_state_ptr(CPUPTR_GPR);
    mem[0] = 0xB8;
    mem[1] = 0x12;
    mem[2] = 0x34;
    mem[3] = 0xEE;
    cpu_set_state(CPU_PHYSEIP, 0);
    cpu_register_mem_refill_handler(mem_stuff);
    cpu_register_io_write_cb(dummy_write2, 8);
    cpu_run(2);
    printf("EAX: %08x\n", regs[0]);
}

EXPORT
void libcpu_init(void)
{
    cpu_init();
}

int main()
{
    UNUSED(test);
    return cpu_init();
}