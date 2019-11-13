// Advanced Programmable Interrupt Controller emulation. Technically part of the CPU, but independent enough that it can go in its own folder
//  Can be simulated optionally by setting pc_settings.apic_enabled to 1.
//  Otherwise, the emulated system is functionally identical to a system without an APIC.
// https://wiki.osdev.org/APIC
// https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf (page 363)
// TODO:
//  - More sophisticated interrupt delivery
//  - Timer (currently, XP doesn't use this so we don't support it either)

#include "cpuapi.h"
#include "devices.h"
#include "io.h"
#include "pc.h"

#define APIC_LOG(x, ...) LOG("APIC", x, ##__VA_ARGS__)
#define APIC_FATAL(x, ...) FATAL("APIC", x, ##__VA_ARGS__)

#define APIC_ERROR_SEND_CHECKSUM 1
#define APIC_ERROR_RECV_CHECKSUM 2
#define APIC_SEND_ACCEPT_ERROR 4
#define APIC_RECV_ACCEPT_ERROR 8
#define APIC_REDIRECT_IPI 16
#define APIC_SEND_INVALID_VECTOR 32
#define APIC_RECV_INVALID_VECTOR 64
#define APIC_ILLEGAL_REGISTER_ACCESS 128

#define LVT_DISABLED (1 << 16)

#define EDGE_TRIGGERED 0
#define LEVEL_TRIGGERED 1

enum {
    LVT_INDEX_CMCI = 0,
    LVT_INDEX_TIMER,
    LVT_INDEX_THERMAL,
    LVT_INDEX_PERFORMANCE_COUNTER,
    LVT_LINT0,
    LVT_LINT1,
    LVT_ERROR,
    LVT_END // Not an LVT entry
};

enum {
    LVT_DELIVERY_FIXED = 0,
    LVT_DELIVERY_SMI = 2,
    LVT_DELIVERY_LOWEST_PRIORITY = 3,
    LVT_DELIVERY_NMI = 4,
    LVT_DELIVERY_INIT = 5,
    LVT_DELIVERY_EXT_INT = 7
};

static struct apic_info {
    // <<< BEGIN STRUCT "struct" >>>
    uint32_t base;

    // The value to hand over the CPU in case of a spurious interrupt
    uint32_t spurious_interrupt_vector;

    uint32_t lvt[7]; // Seven total, but only six are used

    uint32_t isr[8]; // 32 bits * 8 = 256 total
    uint32_t tmr[8]; // Trigger mode register
    uint32_t irr[8]; // Interrupt request register

    uint32_t icr[2];

    uint32_t id; // APIC ID. Usually zero.

    uint32_t error, cached_error;

    uint32_t timer_divide, timer_initial_count;
    itick_t timer_reload_time, timer_next;

    uint32_t destination_format, logical_destination;
    int dest_format_physical;

    int intr_line_state;

    uint32_t task_priority, processor_priority;

    int enabled;

    uint32_t temp_data;
    // <<< END STRUCT "struct" >>>
} apic;

static void apic_state(void)
{
    // <<< BEGIN AUTOGENERATE "state" >>>
    struct bjson_object* obj = state_obj("apic", 22 + 0);
    state_field(obj, 4, "apic.base", &apic.base);
    state_field(obj, 4, "apic.spurious_interrupt_vector", &apic.spurious_interrupt_vector);
    state_field(obj, 28, "apic.lvt", &apic.lvt);
    state_field(obj, 32, "apic.isr", &apic.isr);
    state_field(obj, 32, "apic.tmr", &apic.tmr);
    state_field(obj, 32, "apic.irr", &apic.irr);
    state_field(obj, 8, "apic.icr", &apic.icr);
    state_field(obj, 4, "apic.id", &apic.id);
    state_field(obj, 4, "apic.error", &apic.error);
    state_field(obj, 4, "apic.cached_error", &apic.cached_error);
    state_field(obj, 4, "apic.timer_divide", &apic.timer_divide);
    state_field(obj, 4, "apic.timer_initial_count", &apic.timer_initial_count);
    state_field(obj, 8, "apic.timer_reload_time", &apic.timer_reload_time);
    state_field(obj, 8, "apic.timer_next", &apic.timer_next);
    state_field(obj, 4, "apic.destination_format", &apic.destination_format);
    state_field(obj, 4, "apic.logical_destination", &apic.logical_destination);
    state_field(obj, 4, "apic.dest_format_physical", &apic.dest_format_physical);
    state_field(obj, 4, "apic.intr_line_state", &apic.intr_line_state);
    state_field(obj, 4, "apic.task_priority", &apic.task_priority);
    state_field(obj, 4, "apic.processor_priority", &apic.processor_priority);
    state_field(obj, 4, "apic.enabled", &apic.enabled);
    state_field(obj, 4, "apic.temp_data", &apic.temp_data);
// <<< END AUTOGENERATE "state" >>>
}

static inline void set_bit(uint32_t* ptr, int bitpos, int bit)
{
    int ptr_loc = bitpos >> 5 & 7, bit_loc = bitpos & 0x1F;
    if (!bit)
        ptr[ptr_loc] &= ~(1 << bit_loc);
    else
        ptr[ptr_loc] |= 1 << bit_loc;
}
static inline int get_bit(uint32_t* ptr, int bit)
{
    return (ptr[bit >> 5 & 7] & 1 << (bit & 0x1F)) != 0;
}
static inline int highest_set_bit(uint32_t* ptr)
{
    int x = 256 - 32;
    for (int i = 7; i >= 0; i--) {
        if (!ptr[i])
            x -= 32;
        else {
            return (31 - __builtin_clz(ptr[i])) + x;
        }
    }
    return -1; // No bits set
}
static inline int vector_invalid(int vector)
{
    return !(vector & 0xF0) || vector >= 0xFF;
}

static void apic_error(void)
{
    // ??
}

static void apic_send_highest_priority_interrupt(void)
{
    // See section 10.8 of Intel SDM

    // Ignore if INTR is already high -- this means that we've signalled the CPU but it doesn't want to respond. In that case, it's the CPU's fault!
    if (apic.intr_line_state == 1)
        return;

    // Send the highest priority interrupt
    int highest_interrupt_requested = highest_set_bit(apic.irr), highest_interrupt_in_service = highest_set_bit(apic.isr);
    if (highest_interrupt_requested == -1)
        return; // No interrupts were requested, so don't send any!

    // Check if the interrupt being serviced at the moment has a lower priority than the interrupt requested.
    // If the interrupt requested has a greater priority, then send another one.
    if (highest_interrupt_in_service < highest_interrupt_requested) {
        // "The processor will deliver only those interrupts that have an interrupt-priority class higher than the processor-priority class in the PPR." (page 391)
        if ((highest_interrupt_requested & 0xF0) > (apic.task_priority & 0xF0)) {
            // At this point, the interrupt will be serviced, so set all approriate fields
            apic.processor_priority = highest_interrupt_requested & 0xF0; // "PPR[7:4] (the processor-priority class) the maximum of TPR[7:4] (the task- priority class) and ISRV[7:4] (the priority of the highest priority interrupt in service)."

            //if(highest_interrupt_requested != 0xD1) {printf("Sending interrupt: %02x\n", highest_interrupt_requested); __asm__("int3"); }
            // At this point, we simply need to kick the CPU out from its loop and wait for it to acknowledge the interrupt.
            // The next function that will be called is apic_get_interrupt()
            apic.intr_line_state = 1;
            cpu_raise_intr_line();
            cpu_request_fast_return(EXIT_STATUS_IRQ);
        } else // Task priority is too high -- refrain from sending interrupt
            return;
    } else // Nothing changes -- wait for completion
        return;
}

int apic_get_interrupt(void)
{
    // Acknowledges the interrupt, lowers the INTR line, modifies appropriate bits, and sends interrupt vector back to CPU.

    int highest_irr = highest_set_bit(apic.irr);
    if (highest_irr == -1) {
        APIC_FATAL("TODO: spurious interrupts\n");
    }
    // TODO: check PPR for spurious interrupt

    set_bit(apic.irr, highest_irr, 0);
    set_bit(apic.isr, highest_irr, 1);

    apic.intr_line_state = 0;
    cpu_lower_intr_line();

    APIC_LOG("Sending interrupt %x\n", highest_irr);

    return highest_irr;
}

int apic_has_interrupt(void)
{
    return apic.intr_line_state;
}

void apic_receive_bus_message(int vector, int type, int trigger_mode)
{
    APIC_LOG("Received bus message: vector=%02x type=%d trigger=%d\n", vector, type, trigger_mode);
    // Section 10.8
    switch (type) {
    case LVT_DELIVERY_INIT:
        APIC_FATAL("TODO: INIT delivery\n");
        //io_trigger_reset();
        break;
    case LVT_DELIVERY_NMI:
        APIC_FATAL("TODO: NMI delivery\n");
        break;
    case LVT_DELIVERY_SMI:
        APIC_FATAL("TODO: SMI delivery\n");
        break;
    case LVT_DELIVERY_EXT_INT:
        // Set IRR -- no further action required
        set_bit(apic.irr, vector, 1);
        apic_send_highest_priority_interrupt();
        break;
    case LVT_DELIVERY_FIXED:
    case LVT_DELIVERY_LOWEST_PRIORITY:
        // Check if vector is invalid
        if (vector_invalid(vector)) {
            apic.error |= APIC_RECV_INVALID_VECTOR;
            apic_error();
        }
        // Check if interrupt has already been sent
        if (get_bit(apic.irr, vector))
            return;
        set_bit(apic.irr, vector, 1);
        set_bit(apic.tmr, vector, trigger_mode);
        apic_send_highest_priority_interrupt();
        break;
    }
}

// Send an inter processor interrupt to a CPU with APIC ID "destination"
static void apic_send_ipi2(uint32_t vector, int mode, int trigger, uint32_t destination)
{
    if (vector_invalid(vector)) {
        apic.error |= APIC_SEND_INVALID_VECTOR; // Is this right?
        apic_error();
    }

    // Only supports a single processor system at the moment
    if (destination == apic.id) {
        apic_receive_bus_message(vector, mode, trigger);
    }
}

static void apic_send_ipi(uint32_t vector, int mode, int trigger)
{
    if (vector_invalid(vector)) {
        apic.error |= APIC_SEND_INVALID_VECTOR; // Is this right?
        apic_error();
    }
    apic_receive_bus_message(vector, mode, trigger);
}

static uint32_t* get_lvt_ptr(int idx)
{
    switch (idx) {
    case 0x2F:
        return &apic.lvt[LVT_INDEX_CMCI];
    case 0x32:
        return &apic.lvt[LVT_INDEX_TIMER];
    case 0x33:
        return &apic.lvt[LVT_INDEX_THERMAL];
    case 0x34:
        return &apic.lvt[LVT_INDEX_PERFORMANCE_COUNTER];
    case 0x35:
        return &apic.lvt[LVT_LINT0];
    case 0x36:
        return &apic.lvt[LVT_LINT1];
    case 0x37:
        return &apic.lvt[LVT_ERROR];
    }
    // Should not reach here
    return NULL;
}

static int apic_get_clock_divide(void)
{
    return ((((apic.timer_divide >> 1 & 4) | (apic.timer_divide & 3)) + 1) & 7);
}

static uint32_t apic_get_count(void)
{
    return apic.timer_initial_count - ((uint32_t)(cpu_get_cycles() - apic.timer_reload_time) >> apic_get_clock_divide()) % apic.timer_initial_count;
}
// In terms of CPU ticks, independent of ticks_per_second because APIC timer isn't tied to realtime
static itick_t apic_get_period(void)
{
    return (itick_t)apic.timer_initial_count << apic_get_clock_divide();
}

static uint32_t apic_read(uint32_t addr)
{
    addr -= apic.base;
    addr >>= 4;
    switch (addr) {
    case 0x02:
        return apic.id;
    case 0x03:
        return 0x14 | (5 << 16) | (0 << 24); // Version 14h, 6 LVT entries supported, EOI something something unsupported
    case 0x08:
        return apic.task_priority;
    case 0x0B: // Note: no error when reading from EOI
        return 0;
    case 0x0D:
        return apic.logical_destination;
    case 0x0E:
        return apic.destination_format;
    case 0x0F: // Spurious interrupt vector register
        return apic.spurious_interrupt_vector;
    case 0x10 ... 0x17:
        return apic.isr[addr & 7];
    case 0x18 ... 0x1F:
        return apic.tmr[addr & 7];
    case 0x20 ... 0x27:
        return apic.irr[addr & 7];
    case 0x28: {
        // XXX -- we are supposed to clear it when we write
        return apic.cached_error;
    }
    case 0x2F:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
        return *get_lvt_ptr(addr);
    case 0x30 ... 0x31:
        return apic.icr[addr & 1];
    case 0x38:
        return apic.timer_initial_count;
    case 0x39:
        return apic_get_count();
    //return apic.timer_initial_count - ((uint32_t)(cpu_get_cycles() - apic.timer_reload_time) >> apic_get_clock_divide());
    case 0x3E:
        return apic.timer_divide;
    default:
        APIC_FATAL("TODO: APIC read %08x\n", addr);
    }
}
static void apic_write(uint32_t addr, uint32_t data)
{
    addr -= apic.base;
    addr >>= 4; // Must be 128-bit aligned
    switch (addr) {
#if 0
    default: // Reserved or read-only
        APIC_LOG("Invalid write to %08x\n", data);
        apic.error |= APIC_ILLEGAL_REGISTER_ACCESS;
        break;
#endif

    case 0x03:
        apic.error |= APIC_ILLEGAL_REGISTER_ACCESS;
        break;
    case 2:
        APIC_LOG("Setting APIC ID to %08x\n", data);
        apic.id = data;
        break;
    case 0x08: { // Task Priority Register
        apic.task_priority = data & 0xFF;

        // Update PPR as needed
        int highest_isr = highest_set_bit(apic.isr);
        if (highest_isr == -1)
            apic.processor_priority = apic.task_priority;
        else {
            int ndiff = (apic.task_priority & 0xF0) - (highest_isr & 0xF0);
            if (ndiff > 0) // TPR[7:4] > ISRV[7:4]
                apic.processor_priority = apic.task_priority;
            else
                apic.processor_priority = highest_isr & 0xF0;
        }

        apic_send_highest_priority_interrupt();
        break;
    }
    case 0x0B: { // EOI register
        int current_isr = highest_set_bit(apic.isr);
        if (current_isr != -1) {
            set_bit(apic.isr, current_isr, 0);
            if (get_bit(apic.tmr, current_isr)) {
                // Level-triggered interrupt, EOI-broadcast supression unsupported.
                ioapic_remote_eoi(current_isr);
            }
            APIC_LOG("EOI'ed: %02x Next highest: %02x\n", current_isr, highest_set_bit(apic.irr));
            apic_send_highest_priority_interrupt();
        }
        break;
    }
    case 0x0D: // Logical Destination Register
        apic.logical_destination = data & 0xFF000000;
        break;
    case 0x0E: // Destination Format
        apic.destination_format &= ~0xF0000000;
        apic.destination_format |= data & 0xF0000000;
        apic.dest_format_physical = apic.destination_format == 0xFFFFFFFF;
        if (!apic.dest_format_physical)
            APIC_LOG("Logical destination unsupported\n");
        break;
    case 0x0F: // Spurious interrupt vector register
        apic.spurious_interrupt_vector = data;
        if (data & 0x100) {
            // Software disabled
            for (int i = 0; i < 7; i++)
                apic.lvt[i] |= LVT_DISABLED;
        }
        break;
    case 0x10 ... 0x17:
        apic.isr[addr & 7] = data;
        break;
    case 0x18 ... 0x1F:
        apic.tmr[addr & 7] = data;
        break;
    case 0x20 ... 0x27:
        apic.irr[addr & 7] = data;
        break;
    case 0x28: // error register
        // From the manual:
        //  Before attempt to read from the ESR, software should first write to it. 
        //  (The value written does not affect the values read subsequently; only zero may be written in x2APIC mode.) 
        //  This write clears any previously logged errors and updates the ESR with any errors detected since the last write to the ESR. 
        //  This write also rearms the APIC error interrupt triggering mechanism.
        apic.cached_error = apic.error;
        apic.error = 0;
        break;
    case 0x2F:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
        *get_lvt_ptr(addr) = data;
        break;
    case 0x30: { // Write to lower 32 bits of ICR. This is how you send interrupts to other processors
        apic.icr[0] = data;

        int vector = data & 0xFF,
            delivery_mode = data >> 8 & 7,
            //destination_mode = data >> 11 & 1,
            level = data >> 14 & 1,
            trigger = data >> 15 & 1,
            destination_shorthand = data >> 18 & 3,
            apic_destination = apic.icr[1] >> (56 - 32);

        if (delivery_mode == 5 && level == 0 && trigger == 1) {
            // INIT level de-assert: not actually an INIT signal
            APIC_LOG("INIT level de-assert (not INIT)\n");
            return;
        }

        switch (destination_shorthand) {
        case 0: // Route interrupt to processor with specified APIC ID
            apic_send_ipi2(vector, delivery_mode, trigger, apic_destination);
            break;
        case 1: // Send interrupt to self, only
            apic_send_ipi(vector, LVT_DELIVERY_FIXED, trigger);
            break;
        case 2: // Send interrupt to all processors
            apic_send_ipi(vector, delivery_mode, trigger);
            break;
        case 3: // Send interrupt to all processors but self
            break;
        }
        break;
    }
    case 0x31: // ICR, upper 32 bits
        apic.icr[1] = data;
        break;
    case 0x38:
        apic.timer_initial_count = data;
        apic.timer_reload_time = get_now();
        apic.timer_next = apic.timer_reload_time + apic_get_period();
        cpu_cancel_execution_cycle(EXIT_STATUS_NORMAL);
        break;
    case 0x39:
        break;
    case 0x3E:
        apic.timer_divide = data;
        APIC_LOG("Timer divide=%d\n", 1 << apic_get_clock_divide());
        cpu_cancel_execution_cycle(EXIT_STATUS_NORMAL);
        break;
    default:
        APIC_FATAL("TODO: APIC write %08x data=%08x\n", addr, data);
    }
}

// Due to how access.c splits up mmio reads/writes, we need to allow 8-bit APIC accesses.
// However, since accesses that are less than 32-bits in size are undefined, we can do whatever we want here.
// We should not be doing this, but this is the simplest way to do things without adding a ton of logic in apic.c

static uint32_t apic_readb(uint32_t addr)
{
    //APIC_LOG("8-bit read from %08x\n", addr);
    return apic_read(addr & ~3) >> ((addr & 3) * 8) & 0xFF;
}
static void apic_writeb(uint32_t addr, uint32_t data)
{
    // Technically, we should not be doing this
    int offset = addr & 3, byte_offset = offset << 3;
    apic.temp_data &= ~(0xFF << byte_offset);
    apic.temp_data |= data << byte_offset;
    if (offset == 3) {
        apic_write(addr & ~3, apic.temp_data);
    }
    //APIC_FATAL("8-bit write to %08x with data %02x\n", addr, data);
}

static void apic_reset(void)
{
    apic.spurious_interrupt_vector = 0xFF;
    apic.base = 0xFEE00000;
    apic.id = 0;
    apic.error = 0;

    apic.destination_format = -1;
    apic.dest_format_physical = 1;

    for (int i = 0; i < LVT_END; i++)
        apic.lvt[i] = LVT_DISABLED; // Disabled

    // Map one page of MMIO at the specified address.
    io_register_mmio_read(apic.base, 4096, apic_readb, NULL, apic_read);
    io_register_mmio_write(apic.base, 4096, apic_writeb, NULL, apic_write);
}

// Find out how many ticks until next interrupt
int apic_next(itick_t now)
{
    // TODO: TSC Deadline mode
    if (!apic.enabled)
        return -1;
    // "A write of 0 to the initial-count register effectively stops the local APIC timer, in both one-shot and periodic mode."
    if (apic.timer_initial_count == 0)
        return -1;

    // We want to keep the APIC timer running in the background, but not sending any interrupts
    int apic_timer_enabled = 1;
    
    // Information regarding lvt
    int info = apic.lvt[LVT_INDEX_TIMER] >> 16;

    if (apic.timer_next <= now) {
        // Raise interrupt
        if(!(info & 1)) {// LVT_DISABLED set to 0
            APIC_LOG("  timer period %ld cur=%ld next=%ld\n", apic_get_period(), now, apic.timer_next);
            apic_receive_bus_message(apic.lvt[LVT_INDEX_TIMER] & 0xFF, LVT_DELIVERY_FIXED, 0);
        }
        else apic_timer_enabled = 0;
        
        switch (info >> 1 & 3) {
        case 2:
            APIC_FATAL("TODO: TSC Deadline\n");
            break;
        case 1: // Periodic
            apic.timer_next += apic_get_period();
            break;
        case 0: // One shot
            apic.timer_next = -1; // Disable timer
            return -1; // no more interrupts
        case 3:
            APIC_LOG("Invalid timer mode set, ignoring\n");
            return -1;
        }

        // If no interrupts, then return
        if(apic_timer_enabled) return -1;
    }

    itick_t next = apic.timer_next - now;
    if(next > 0xFFFFFFFF) return -1; // Don't allow wrap-around of large integers
    return (uint32_t)next;
}

void apic_init(struct pc_settings* pc)
{
    apic.enabled = pc->apic_enabled;
    if (!apic.enabled)
        return;
    io_register_reset(apic_reset);
    state_register(apic_state);
}

int apic_is_enabled(void)
{
    return apic.enabled;
}