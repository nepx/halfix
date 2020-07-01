# Halfix Internals

This document aims to explain the internal workings of Halfix and hopefully make the code a little bit easier to understand. Note that `host` refers to the system that Halfix is running on and `guest` refers to the system that is being run. In some cases, the guest and host operating system *types* may be the same (i.e. Ubuntu on Ubuntu), but they certainly aren't the same *systems*. 

## Entry Point

On the native build, Halfix has one entry point in `src/main.c`: `int main(int argc, char** argv)`. It parses command line options and then reads the `ini` configuration file parser, sendning the text off to `src/ini.c`. In the past, the `ini` file parser was integrated within `main.c`, but it was refactored out to allow more code to be shared between the Emscripten and native versions. 

The `ini` file parser opens all necessary image files, initializes them, and fills `struct pc_settings` with appropriate values. It is, of course, possible to fill `struct pc_settings` with your own values and bypass the `ini` file parser entirely. 

Then it runs some sanity checks on the settings, calls `pc_init`, and then calls the main loop. 

On the Emscripten build, creating a `Halfix` object and calling its `.init(cb)` method loads the emulator executable. Once loaded, `Module.onRuntimeInitialized` is called, and it parses arguments, creates a nice little `ini` file for the configuration parser to consume, and waits for the configuration parser to finish. 

The Emscripten build does not contain the filesystem module; in lieu of using the `read`/`write` calls, it hands off image loading to the JavaScript wrapper code. `window.load_file_xhr`, contrary to its name, loads files from both `File` images (`file!%d`, where `%d` is an index into an array) and `ArrayBuffer`s (`ab!%d`, of the same format), in addition to the regular `XMLHttpRequest`. Its name is a holdover from a time when the browser version only supported XHR requests. `drive_init` also initializes all drives on the JavaScript side of things. 

After all the requests are completed and all drives (if they exist) have been initialized, then `run_wrapper2` is called, which simply calls `pc_init`, though it could probably do a bit more if needed. After that, the callback passed to `init` is called, which will likely start up the main loop. 

## `pc_init`

`pc_init` begins by initializing CPU, I/O, and device state. Note that at this point, only initialization is being done -- i.e. setting pointers, mapping I/O ports, etc. The actual device states aren't reset until `io_reset` is called and triggers a full system reset. Note that ALL device initialization functions are called, but some will simply disable the component and return. In this case, this component will act like it doesn't exist -- if you disable APIC emulation, for instance, the guest operating system will have no way of detecting or interacting with the APIC. 

Then it initializes a number of I/O ports meant for the BIOS to use. Some of these date back to the time when every unimplemented I/O port access ended up with a fatal abort. Then it allocates memory (`cpu_init_mem`) and hands the PCI controller a pointer to main memory because it needs the ability to remap BIOS shadow memory. 

`cpu_add_rom` does nothing at the moment, but older versions used KVM, and it required areas of memory to be specifically registered as ROM. In the emulator, the BIOS/VGABIOS images are protected from writes in the MMU and MMIO interfaces, so no additional work needs to be done here. 

## Main Loop

Whether on Emscripten or native, `pc_execute` is where the action takes place. It begins by calling `drive_check_complete`, to see if asynchronous disk reads have completed. After that, it checks how many cycles have passed since the last time `pc_execute` was called -- if more than `INSNS_PER_FRAME` cycles have elapsed, then it automatically produces a savestate image, if savestates are enabled, of course. This was very useful when debugging operating systems with long boot cycles. 

`devices_get_next` polls a number of devices and sees if they need to raise an IRQ. Each device returns an integer containing the number of cycles until the next IRQ. The routine finds the minimum of these values and clamps it down to 200,000 cycles if needed (this prevents the loop from spinning too long). Then, the actal CPU core executes (`cpu_run`) and returns the number of cycles executed. The CPU will provide a reasion why it exited (`cpu_get_exit_reason`), and if it turns out to be a `hlt`, it finds out how many cycles were supposed to be executed but didn't, converts that into milliseconds, and returns. 

`pc_execute` will execute at most 2,000,000 instructions per frame. In the most likely scenario, less instructions will be executed due to IRQ exits and `hlt` being called. 

## CPU loop

The CPU loop, heavily simplified, looks like this: 

```c
int cycles_left = cycles_to_run; 
do {
    execute_instruction();
} while (--cycles_left);
```

Notably, we can compute the number of instructions that have been executed by computing `cycles_to_run - cycles_left`. This is the core logic of `cpu_get_cycles`. The value in `cpu.cycles` is not to be trusted. Occasionally, the values of `cycles_to_run` and `cycles_left` will be fudged slightly to account for IRQs and interrupt guards, but calling `cpu_get_cycles` will *always* return the correct value at any given time. 

The status of the IRQ line is the first thing checked. In order for an interrupt to be acknowledged (and an `IAC` to be sent to the APIC/PIC), two conditions must be met: `IF=1` and the interrupt guard must not be set. The first condition is well-known to most x86 developers, but the second condition is a bit obscure. After an instruction modifies `ss` segment register, interrupts are blocked for one instruction to allow `esp`/`sp` to be changed too (`mov ss, ax` and `pop ss` do this, I believe). The way this is handled will be explained shortly. 

If these two conditions are met, then the CPU will get the interrupt number from the PIC by calling `pic_get_interrupt`. This will also send an `IAC` message to the PIC signalling that the CPU has received the interrupt and is servicing it. A hardware interrupt is generated, and the call looks complicated because protected mode interrupts are not fun. 

At this point, the CPU is ready to begin executing instructions -- it has switched out of its `hlt` state. In the case where `cpu_loop` was called on a halted CPU with no IRQ (it's possible during early boot, when only the PIT is firing and its period is over 200,000 cycles), then the loop would simply return without executing anything. 

After that comes the interrupt guard handler. If the last instruction caused one to be set up, precisely one instruction is executed (`cycles_left` is set to one). Don't worry about `refill_counter` for now. 

Then comes the fun part, `cpu_execute`, followed by a complete timing realignment. `cpu.cycles` is pushed forward (we can move forward in time, but not backwards), and `cycles_to_run` and `cycles_left` are modified according to the number of instructions that have been executed. This is because certain instructions cause us to bail out of the loop -- I/O accesses that produce IRQs, interrupt guards, or if 200,000 instructions have been executed. 

If `cycles_left` is zero, it bails out of the loop and the routine returns the number of cycles that have been executed since we entered the loop. 

TBC