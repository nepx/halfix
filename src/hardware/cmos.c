// CMOS RTC. Its main purpose is for pulling the system time/date, but a few operating systems go above and beyond.
//  OS/2: Uses the CMOS for timing: it calls an interrupt every 32 ms
//  Windows XP: Checks the REG_A UIP flag to delay a few seconds to calculate CPU speed
// https://www.nxp.com/files-static/microcontrollers/doc/data_sheet/MC146818.pdf

#include "devices.h"
#include "state.h"
#include <time.h>

#define FREQUENCY 32768
#define CMOS_LOG(x, ...) LOG("CMOS", x, ##__VA_ARGS__)
#define CMOS_FATAL(x, ...) FATAL("CMOS", x, ##__VA_ARGS__)

#define ALARM_SEC 1
#define ALARM_MIN 3
#define ALARM_HOUR 5

struct cmos {
    // <<< BEGIN STRUCT "struct" >>>
    uint8_t ram[128];
    uint8_t addr, nmi;

    time_t now;

    int periodic_ticks, periodic_ticks_max;

    uint32_t period;

    itick_t// How long we should be waiting per interrupt
        last_called, // The last time the timer handler was called.
        uip_period, // When the UIP flag should be cleared
        last_second_update; // The last time the seconds counter rolled over
    // <<< END STRUCT "struct" >>>
};

static struct cmos cmos;
static void cmos_state(void)
{
    // <<< BEGIN AUTOGENERATE "state" >>>
    struct bjson_object* obj = state_obj("cmos", 10);
    state_field(obj, 128, "cmos.ram", &cmos.ram);
    state_field(obj, 1, "cmos.addr", &cmos.addr);
    state_field(obj, 1, "cmos.nmi", &cmos.nmi);
    state_field(obj, sizeof(time_t), "cmos.now", &cmos.now);
    state_field(obj, 4, "cmos.periodic_ticks", &cmos.periodic_ticks);
    state_field(obj, 4, "cmos.periodic_ticks_max", &cmos.periodic_ticks_max);
    state_field(obj, 4, "cmos.period", &cmos.period);
    state_field(obj, 8, "cmos.last_called", &cmos.last_called);
    state_field(obj, 8, "cmos.uip_period", &cmos.uip_period);
    state_field(obj, 8, "cmos.last_second_update", &cmos.last_second_update);
// <<< END AUTOGENERATE "state" >>>
}

#define is24hour() (cmos.ram[0x0B] & 2)

static void cmos_lower_irq(void)
{
    pic_lower_irq(8);
}

static uint8_t bcd_read(uint8_t val)
{
    if (cmos.ram[0x0B] & 4)
        return val;
    else
        return ((val / 10) << 4) | (val % 10);
}

static uint8_t cmos_ram_read(uint8_t addr)
{
    struct tm* now;
    itick_t now_ticks, next_second;
    switch (addr) {
    case 0:
        now = localtime(&cmos.now);
        return bcd_read(now->tm_sec);
    case 2:
        now = localtime(&cmos.now);
        return bcd_read(now->tm_min);
    case 4:
        now = localtime(&cmos.now);
        if (is24hour())
            return bcd_read(now->tm_hour);
        else
            return bcd_read(now->tm_hour % 12) | (now->tm_hour > 12) << 7;
    case 6:
        now = localtime(&cmos.now);
        return bcd_read(now->tm_wday + 1);
    case 7:
        now = localtime(&cmos.now);
        return bcd_read(now->tm_mday);
    case 8:
        now = localtime(&cmos.now);
        return bcd_read(now->tm_mon + 1);
    case 9:
        now = localtime(&cmos.now);
        return bcd_read(now->tm_year % 100);
    case 1:
    case 3:
    case 5:
        return cmos.ram[cmos.addr];
    case 0x0A:
        // Special case for UIP bit
        //                                    A                           C     B
        //                                    v                           v     v
        //  |---------------------------======|---------------------------======|
        //  ^                           ^     ^                           ^     ^
        //  0                          UIP    1                          UIP    2
        // 
        // A: cmos.last_second_update
        // B: cmos.last_second_update + ticks_per_second
        // B <==> C: cmos.uip_period
        // C: cmos.last_second_update + ticks_per_second - cmos.uip_period
        // UIP will be set if it's within regions B and C.
        now_ticks = get_now();
        next_second = cmos.last_second_update+ticks_per_second;

        if(now_ticks >= (next_second-cmos.uip_period) && now_ticks < next_second){
            //printf("Now:              %ld\t\tLast second update: %ld\n", get_now(), cmos.last_second_update);
            //printf("Ticks per second: %d \t\tUIP period: %ld\n", ticks_per_second, cmos.uip_period);
            //printf("Combination:      %ld\n", cmos.last_second_update+ticks_per_second-cmos.uip_period);
            //printf("Next second: %ld\n", cmos.last_second_update+ticks_per_second);
            return cmos.ram[0x0A] | 0x80; // UIP bit is still set.
        }
        
        // INTENTIONAL FALLTHROUGH
    case 0x0B: // status register
        return cmos.ram[cmos.addr];
    case 0x0C: {
        cmos_lower_irq();
        int res = cmos.ram[0x0C];
        cmos.ram[0x0C] = 0;
        return res;
    }
    case 0x0D:
        return 0x80; // has power
    }
    CMOS_FATAL("should not be here\n");
}
static uint32_t cmos_readb(uint32_t port)
{
    switch (port & 1) {
    case 0:
        //CMOS_FATAL("Unknown readb 70h\n");

        // Windows XP reads from this register to determine which port it should write dummy values to. 
        return 0xFF; 
    case 1:
        //if(cmos.addr == 0xFF) cpu_add_now(100000);
        if (cmos.addr <= 0x0D)
            return cmos_ram_read(cmos.addr);
        else
            return cmos.ram[cmos.addr];
    }
    CMOS_FATAL("should not be here\n");
}

static inline void cmos_update_timer(void)
{
    if ((cmos.ram[0x0A] >> 4 & 7) != 2)
        CMOS_LOG("22-step divider set to strange value: %d\n", cmos.ram[0x0A] >> 4 & 7);
    int period = cmos.ram[0x0A] & 0x0F;
    if (!period)
        return;

    if (period < 3)
        period += 7;

    int freq = FREQUENCY >> (period - 1); // in hz

    if (cmos.ram[0x0B] & 0x40) {
        cmos.period = ticks_per_second / freq; // The amount of time, in ticks, that it takes for one interrupt to occur.
        cmos.periodic_ticks = 0;
        cmos.periodic_ticks_max = freq;
    } else {
        cmos.period = ticks_per_second; // We simply need to keep calling every second.
    }
    cmos.last_called = get_now();
}
static inline int bcd(int data)
{
    if (cmos.ram[0x0B] & 4)
        return data;
    return ((data & 0xf0) >> 1) + ((data & 0xf0) >> 3) + (data & 0x0f);
}

static inline void cmos_ram_write(uint8_t data)
{
    struct tm* now = localtime(&cmos.now);

    switch (cmos.addr) {
    case 1:
    case 3:
    case 5:
        cmos.ram[cmos.addr] = data;
        break;
    case 0:
        now->tm_sec = bcd(data);
        break;
    case 2:
        now->tm_min = bcd(data);
        break;
    case 4:
        now->tm_hour = bcd(data & 0x7F);
        if (!is24hour())
            if (data & 0x80)
                now->tm_hour += 12;
        break;
    case 6:
        now->tm_wday = bcd(data);
        break;
    case 7:
        now->tm_mday = bcd(data);
        break;
    case 8:
        now->tm_mon = bcd(data);
        break;
    case 9:
        now->tm_year = bcd(data) + (bcd(cmos.ram[0x32]) - 19) * 100; // Count years since 1900
		if(now->tm_year < 70) now->tm_year = 70;
        break;
    case 0x0A: // Status Register A
        cmos.ram[0x0A] = (data & 0x7F) | (cmos.ram[0x0A] & 0x80);
        cmos_update_timer();
        break;
    case 0x0B: // Status Register B
        cmos.ram[0x0B] = data;
        cmos_update_timer();
        break;
    case 0x0C ... 0x0D:
        break;
    default:
        CMOS_FATAL("?? writeb ??");
    }
    cmos.now = mktime(now);
}

static void cmos_writeb(uint32_t port, uint32_t data)
{
    switch (port & 1) {
    case 0:
        cmos.nmi = data >> 7;
        cmos.addr = data & 0x7F;
        break;
    case 1:
        if (cmos.addr <= 0x0D)
            cmos_ram_write(data);
        else
            cmos.ram[cmos.addr] = data;
    }
}

#define PERIODIC 0x40
#define ALARM 0x20
#define UPDATE 0x10

// Raise an IRQ, select a reason from the enum above
static void cmos_raise_irq(int why)
{
    cmos.ram[0x0C] = 0x80 | why;
    pic_raise_irq(8);
}

// Called whenever the CPU exits from its main loop
int cmos_clock(itick_t now)
{
    // Some things to deal with:
    //  - Periodic interrupt
    //  - Updating seconds
    //  - Alarm Interrupt
    //  - UIP Interrupt (basically every second)
    // Note that one or more of these can happen per cmos_clock (required by OS/2 Warp 4.5)
    // Also sets UIP timer (needed for Windows XP timing calibration loop)

    // We have two options when it comes to CMOS timing: we can update registers per second or per interrupt.
    // If the periodic interrupt is not enabled, then we only have to update the clock every second.
    // If the periodic interrupt is enabled, then there's no reason to update the clock every second AND check
    // for the periodic interrupt -- every Nth periodic interrupt, there will be a clock update.

    itick_t next = cmos.last_called + cmos.period;

    if (now >= next) {
        int why = 0;

        if (cmos.ram[0x0B] & 0x40) {
            // Periodic interrupt is enabled.
            why |= PERIODIC;

            // Every Nth periodic interrupt, we will cause an alarm/UIP interrupt.
            cmos.periodic_ticks++;
            if (cmos.periodic_ticks != cmos.periodic_ticks_max)
                goto done; // No, we haven't reached the Nth tick yet
            
            cmos.periodic_ticks = 0; // Reset it back to zero since cmos.periodic_ticks == cmos.periodic_ticks_max
        }

        // Otherwise, we're here to update seconds.
        cmos.now++;
        if (cmos.ram[0x0B] & 0x20) {
            // XXX: there's got to be a more efficient way of doing this
            int ok = 1;
            ok &= cmos_ram_read(ALARM_SEC) == cmos_ram_read(0);
            ok &= cmos_ram_read(ALARM_MIN) == cmos_ram_read(2);
            ok &= cmos_ram_read(ALARM_HOUR) == cmos_ram_read(4); // Is this right?
            if (ok)
                why |= ALARM;
        }
        if (cmos.ram[0x0B] & 0x10) {
            // Clock has completed an update cycle
            why |= UPDATE;
        }

        // we just updated the seconds
        cmos.last_second_update = now;

    done:
        cmos.last_called = get_now();
        if (why){
            cmos_raise_irq(why);
            return 1;
        }
    }
    return 0;
}
int cmos_next(itick_t now)
{
    cmos_clock(now);
    return cmos.last_called + cmos.period - now;
}

void cmos_set(uint8_t where, uint8_t data)
{
    cmos.ram[where] = data;
}
uint8_t cmos_get(uint8_t where)
{
    return cmos.ram[where];
}

static void cmos_reset(void)
{
    cmos.ram[0x0A] = 0x26;
    cmos.ram[0x0B] = 0x02;
    cmos.ram[0x0C] = 0x00;
    cmos.ram[0x0D] = 0x80;
}

void cmos_init(uint64_t now)
{
    io_register_read(0x70, 2, cmos_readb, NULL, NULL);
    io_register_write(0x70, 2, cmos_writeb, NULL, NULL);
    state_register(cmos_state);
    io_register_reset(cmos_reset);
    if(now == 0) now = time(NULL);
    cmos.now = now;
    cmos.last_second_update = get_now(); // We are starting the clock right now.

    cmos.uip_period = 244; // 244 us before the turn of the second. 

    cmos.last_called = get_now();
    cmos.period = ticks_per_second;
}