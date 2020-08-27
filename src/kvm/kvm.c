// KVM-based CPU emulator
// Uses the same interface as cpu/cpu.c
#define _POSIX_SOURCE

#include "cpuapi.h"
#include "devices.h"
#include "util.h"
#include <alloca.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define CPU_LOG(x, ...) LOG("CPU", x, ##__VA_ARGS__)
#define CPU_DEBUG(x, ...) LOG("CPU", x, ##__VA_ARGS__)
#define CPU_FATAL(x, ...) \
    FATAL("CPU", x, ##__VA_ARGS__)

static int dev_kvm_fd, vm_fd, vcpu_fd, exit_reason = EXIT_STATUS_NORMAL, irq_line_state = 0, fast_return_requested = 0;
static uint32_t memsz;
static struct kvm_run* kvm_run;
static void* mem;

int cpu_get_exit_reason(void)
{
    return exit_reason;
}

static void sig_handler(int signum)
{
    fast_return_requested = 1;
    UNUSED(signum);
}

int cpu_init(void)
{
    // TODO: KVM_SET_TSS_ADDR
    dev_kvm_fd = open("/dev/kvm", O_RDWR);
    if (dev_kvm_fd < 0) {
        fprintf(stderr, "Unable to open open KVM: %s\n", strerror(errno));
        return -1;
    }

    int api_version = ioctl(dev_kvm_fd, KVM_GET_API_VERSION, 0);
    if (api_version < 0) {
        perror("KVM_GET_API_VERSION");
        goto fail1;
    } else if (api_version != KVM_API_VERSION) {
        fprintf(stderr, "Wrong KVM API version: %d\n", api_version);
        goto fail1;
    }

    vm_fd = ioctl(dev_kvm_fd, KVM_CREATE_VM, 0);
    if (vm_fd < 0) {
        perror("KVM_CREATE_VM");
        goto fail1;
    }

    // Initialize TSS
    uint64_t addr = 0xFFF00000 - 0x2000;
    if (ioctl(vm_fd, KVM_SET_IDENTITY_MAP_ADDR, &addr) < 0) {
        perror("KVM_SET_IDENTITY_MAP_ADDR");
        goto fail2;
    }
    if (ioctl(vm_fd, KVM_SET_TSS_ADDR, addr + 0x1000) < 0) {
        perror("KVM_SET_TSS_ADDR");
        goto fail2;
    }

    // init vcpu
    vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
    if (vcpu_fd < 0) {
        perror("KVM_CREATE_VCPU");
        goto fail2;
    }

    int mmap_sz = ioctl(dev_kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (!mmap_sz || mmap_sz < 0 || mmap_sz & 0xFFF) {
        perror("KVM_GET_VCPU_MMAP_SIZE");
        goto fail3;
    }

    kvm_run = mmap(NULL, mmap_sz, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu_fd, 0);
    if (kvm_run == MAP_FAILED) {
        perror("kvm_run");
        goto fail3;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGALRM, &sa, NULL);

    return 0;
fail3:
    close(vcpu_fd);
fail2:
    close(vm_fd);
fail1:
    close(dev_kvm_fd);
    return -1;
}

void cpu_set_a20(int level)
{
    UNUSED(level);
    CPU_LOG("A20 not supported on KVM\n");
}

static int slot = 0;
static int kvm_register_area(int flags, uint64_t guest_addr, void* host_addr, uint64_t size)
{
    struct kvm_userspace_memory_region memreg;
    memreg.slot = slot++;
    memreg.flags = flags;
    memreg.guest_phys_addr = (uint32_t)guest_addr;
    memreg.memory_size = size;
    memreg.userspace_addr = (uintptr_t)host_addr;
    //printf("slot=%d flags=%d phys=%08x sz=%08x p=%p\n", memreg.slot, memreg.flags, (uint32_t)memreg.guest_phys_addr, (uint32_t)memreg.memory_size, host_addr);

    if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &memreg) < 0) {
        perror("KVM_SET_USER_MEMORY_REGION");
        return -1;
    }
    return 0;
}

int cpu_init_mem(int size)
{
    mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int c = kvm_register_area(0, 0, mem, 0xA0000);
    c |= kvm_register_area(0, 1 << 20, mem + (1 << 20), size - (1 << 20));
    memsz = size;
    return c;
}

void* cpu_get_ram_ptr(void) { return mem; }

int cpu_add_rom(int addr, int length, void* data)
{
    if ((uint32_t)addr < memsz) {
        memcpy(mem + addr, data, length);
        return kvm_register_area(KVM_MEM_READONLY, addr, mem + addr, (length + 0xFFF) & ~0xFFF);
    }
    return kvm_register_area(KVM_MEM_READONLY, addr, data, (length + 0xFFF) & ~0xFFF);
}

void cpu_set_break(void)
{
    // TODO: break out of KVM loop
}

uint64_t cpu_get_cycles(void)
{
    struct kvm_msrs* msrs = alloca(sizeof(struct kvm_msrs) + sizeof(struct kvm_msr_entry));
    msrs->nmsrs = 1;
    msrs->entries[0].index = 0x10; // TSC, see cpu/ops/misc.c
    if (ioctl(vcpu_fd, KVM_GET_MSRS, msrs) < 0)
        CPU_FATAL("could not get kvm tsc\n");
    return msrs->entries[0].data;
}

void cpu_raise_intr_line(void)
{
    irq_line_state = 1;
}

void cpu_lower_intr_line(void)
{
    irq_line_state = 0;
}

#ifdef __x86_64__
#define R(n) ((uint32_t)(regs.r##n))
#else // i386
#define R(n) regs.e##n
#endif
void cpu_debug(void)
{
    struct kvm_regs regs;
    if (ioctl(vcpu_fd, KVM_GET_REGS, &regs) < 0)
        CPU_FATAL("kvm get regs failed\n");
    fprintf(stderr, "EAX: %08x ECX: %08x EDX: %08x EBX: %08x\n", R(ax), R(cx), R(dx), R(bx));
    fprintf(stderr, "ESP: %08x EBP: %08x ESI: %08x EDI: %08x\n", R(ax), R(cx), R(dx), R(bx));
    fprintf(stderr, "EFLAGS: %08x EIP: %08x\n", R(flags), R(ip));

    struct kvm_sregs sregs;
    if (ioctl(vcpu_fd, KVM_GET_SREGS, &sregs) < 0)
        CPU_FATAL("kvm get regs failed\n");
    fprintf(stderr, "ES.sel=%04x ES.base=%08x, ES.lim=%08x\n", sregs.es.selector, (uint32_t)sregs.es.base, (uint32_t)sregs.es.limit);
    fprintf(stderr, "CS.sel=%04x CS.base=%08x, CS.lim=%08x\n", sregs.cs.selector, (uint32_t)sregs.cs.base, (uint32_t)sregs.cs.limit);
    fprintf(stderr, "SS.sel=%04x SS.base=%08x, SS.lim=%08x\n", sregs.ss.selector, (uint32_t)sregs.ss.base, (uint32_t)sregs.ss.limit);
    fprintf(stderr, "DS.sel=%04x DS.base=%08x, DS.lim=%08x\n", sregs.ds.selector, (uint32_t)sregs.ds.base, (uint32_t)sregs.ds.limit);
    fprintf(stderr, "FS.sel=%04x FS.base=%08x, FS.lim=%08x\n", sregs.fs.selector, (uint32_t)sregs.fs.base, (uint32_t)sregs.fs.limit);
    fprintf(stderr, "GS.sel=%04x GS.base=%08x, GS.lim=%08x\n", sregs.gs.selector, (uint32_t)sregs.gs.base, (uint32_t)sregs.gs.limit);
    fprintf(stderr, "CR0: %08x CR2: %08x CR3: %08x CR4: %08x\n", (uint32_t)sregs.cr0, (uint32_t)sregs.cr2, (uint32_t)sregs.cr3, (uint32_t)sregs.cr4);

    fprintf(stderr, "GDT.base=%08x GDT.limit=%08x\n", (uint32_t)sregs.gdt.base, (uint32_t)sregs.gdt.limit);
    fprintf(stderr, "LDT.base=%08x LDT.limit=%08x\n", (uint32_t)sregs.ldt.base, (uint32_t)sregs.ldt.limit);
    fprintf(stderr, "IDT.base=%08x IDT.limit=%08x\n", (uint32_t)sregs.idt.base, (uint32_t)sregs.idt.limit);
    fprintf(stderr, "TR.base =%08x TR.limit =%08x\n", (uint32_t)sregs.tr.base, (uint32_t)sregs.tr.limit);
}

static jmp_buf top;
int cpu_run(int cycles)
{
    struct itimerval itimer;
    itimer.it_interval.tv_sec = 0;
    itimer.it_interval.tv_usec = 0;
    itimer.it_value.tv_sec = 0;
    if (cycles < 10 * 1000)
        cycles = 10000;
    itimer.it_value.tv_usec = cycles;
#if 1
    setitimer(ITIMER_REAL, &itimer, NULL);
#else
    UNUSED(itimer);
#endif

    UNUSED(top);
    // Just like the emulated cpu_run, we loop until we hit a hlt, a device wants us to exit, or we've run out of cycles
    /*if (setjmp(top) == 0) */
    {
    top:
        if (irq_line_state) {
            if (kvm_run->if_flag) {
                struct kvm_interrupt intr;
                intr.irq = pic_get_interrupt();
                if (ioctl(vcpu_fd, KVM_INTERRUPT, &intr) < 0) {
                    CPU_FATAL("unable to inject interrupt");
                }
                irq_line_state = 0;
            } else
                kvm_run->request_interrupt_window = 1;
        }

        if (fast_return_requested) {
            fast_return_requested = 0;
            goto done;
        }
        int res = ioctl(vcpu_fd, KVM_RUN, 0);
        if (res < 0) {
            if (errno == EINTR)
                goto done;
            perror("KVM_RUN");
            CPU_FATAL("cannot run cpu");
        }

        UNUSED(cycles);

        switch (kvm_run->exit_reason) {
        case KVM_EXIT_IO: { // 2
            void* data = kvm_run->io.data_offset + (void*)kvm_run;
            for (uint32_t i = 0; i < kvm_run->io.count; i++) {
                if (kvm_run->io.direction == KVM_EXIT_IO_OUT) {
                    switch (kvm_run->io.size) {
                    case 1: // byte
                        io_writeb(kvm_run->io.port, *(uint8_t*)data);
                        break;
                    case 2: // word
                        io_writew(kvm_run->io.port, *(uint16_t*)data);
                        break;
                    case 4: // dword
                        io_writed(kvm_run->io.port, *(uint32_t*)data);
                        break;
                    default:
                        CPU_FATAL("unknown io sz=%d\n", kvm_run->io.size);
                    }
                } else {
                    switch (kvm_run->io.size) {
                    case 1: // byte
                        *(uint8_t*)data = io_readb(kvm_run->io.port);
                        break;
                    case 2: // word
                        *(uint16_t*)data = io_readw(kvm_run->io.port);
                        break;
                    case 4: // dword
                        *(uint32_t*)data = io_readd(kvm_run->io.port);
                        break;
                    default:
                        CPU_FATAL("unknown io sz=%d\n", kvm_run->io.size);
                    }
                }
                data += kvm_run->io.size;
            }
            break;
        }
        case KVM_EXIT_MMIO: { // 6
            void* data = kvm_run->mmio.data;
            //printf("ADDR: %08x %d\n", (uint32_t)kvm_run->mmio.phys_addr, kvm_run->mmio.is_write);
            if (kvm_run->mmio.is_write) {
                switch (kvm_run->mmio.len) {
                case 1:
                    io_handle_mmio_write(kvm_run->mmio.phys_addr, *(uint8_t*)data, 0);
                    break;
                case 2:
                    io_handle_mmio_write(kvm_run->mmio.phys_addr, *(uint16_t*)data, 1);
                    break;
                case 4:
                    io_handle_mmio_write(kvm_run->mmio.phys_addr, *(uint32_t*)data, 2);
                    break;
                case 8:
                    io_handle_mmio_write(kvm_run->mmio.phys_addr, *(uint32_t*)data, 2);
                    io_handle_mmio_write(kvm_run->mmio.phys_addr + 4, *(uint32_t*)(data + 4), 2);
                    break;
                }
            } else {
                switch (kvm_run->mmio.len) {
                case 1:
                    *(uint8_t*)data = io_handle_mmio_read(kvm_run->mmio.phys_addr, 0);
                    break;
                case 2:
                    *(uint16_t*)data = io_handle_mmio_read(kvm_run->mmio.phys_addr, 1);
                    break;
                case 4:
                    *(uint32_t*)data = io_handle_mmio_read(kvm_run->mmio.phys_addr, 2);
                    break;
                case 8:
                    *(uint32_t*)data = io_handle_mmio_read(kvm_run->mmio.phys_addr, 2);
                    *(uint32_t*)(data + 4) = io_handle_mmio_read(kvm_run->mmio.phys_addr + 4, 2);
                    break;
                }
            }
            break;
        }
        case KVM_EXIT_IRQ_WINDOW_OPEN: // 7
            kvm_run->request_interrupt_window = 0;
            goto top;
        case KVM_EXIT_HLT: // 5
            printf("HLT CALLED\n");
            exit_reason = EXIT_STATUS_HLT;
            goto done;
        case KVM_EXIT_FAIL_ENTRY:
            CPU_LOG(" == CPU FAILURE ==\n");
            cpu_debug();
            CPU_FATAL("Failed to enter: %llx\n", kvm_run->fail_entry.hardware_entry_failure_reason);
        default:
            printf("todo: exit reason %d\n", kvm_run->exit_reason);
            abort();
            break;
        }
        goto top;
    }
done:
    printf("LOOP EXITING\n");
    return cycles;
}

uint32_t cpu_read_phys(uint32_t addr)
{
    return *(uint32_t*)(mem + addr);
}

void cpu_init_dma(uint32_t x)
{
    UNUSED(x);
    abort();
}

void cpu_write_mem(uint32_t addr, void* data, uint32_t length)
{
    memcpy(data + addr, data, length);
}

void cpu_request_fast_return(int e)
{
    fast_return_requested = 1;
    exit_reason = e;
}
void cpu_cancel_execution_cycle(int r)
{
    cpu_request_fast_return(r);
}