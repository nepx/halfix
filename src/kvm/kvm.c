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
static struct kvm_run *kvm_run, *mem;

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
    memreg.guest_phys_addr = guest_addr;
    memreg.memory_size = size;
    memreg.userspace_addr = (uintptr_t)host_addr;
    printf("%08x sz=%x\n", (uint32_t)guest_addr, (uint32_t)size);
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
    return c;
}

void* cpu_get_ram_ptr(void) { return mem; }

int cpu_add_rom(int addr, int length, void* data)
{
    if(addr < )
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

static jmp_buf top;
int cpu_run(int cycles)
{
    if (irq_line_state) {
        struct kvm_interrupt intr;
        intr.irq = pic_get_interrupt();
        if (ioctl(vcpu_fd, KVM_INTERRUPT, &intr) < 0) {
            CPU_FATAL("unable to inject interrupt");
        }
    }

    struct itimerval itimer;
    itimer.it_interval.tv_sec = 0;
    itimer.it_interval.tv_usec = 0;
    itimer.it_value.tv_sec = 0;
    itimer.it_value.tv_usec = cycles;
    setitimer(ITIMER_REAL, &itimer, NULL);

    // Just like the emulated cpu_run, we loop until we hit a hlt, a device wants us to exit, or we've run out of cycles
    if (setjmp(top) != 0) {
        if (fast_return_requested)
            goto done;
        int res = ioctl(vcpu_fd, KVM_RUN, 0);
        if (res < 0) {
            perror("KVM_RUN");
            CPU_FATAL("cannot run cpu");
        }

        UNUSED(cycles);

        switch (kvm_run->exit_reason) {
        case KVM_EXIT_HLT:
            exit_reason = EXIT_STATUS_HLT;
            break;
        default:
            printf("todo: exit reason %d\n", kvm_run->exit_reason);
            break;
        }
    }
done:
    exit(1);
    return 0;
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
    cpu_cancel_execution_cycle(r);
}