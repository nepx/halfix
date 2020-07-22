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

## Execution

The main loop is located in `opcodes.c`. It's really simple: 

```c
void cpu_execute(void)
{
    struct decoded_instruction* i = cpu_get_trace();
    do {
        i = i->handler(i);
        if (!--cpu.cycles_to_run)
            break;
    } while (1);
}
```

The loop ensures that at least one instruction will be run every time `cpu_execute` is called (which is important since it advances the emulated system clock). `cpu_get_trace` pulls a series of compiled instructions from the trace cache or compiles one if not found. 

`i->handler(i)` also has the unique property that it doesn't care what the handler points to. It could just as easily point to `op_pop_s16` as it could a block of precompiled or dynamically translated code (this would make a dynarec easier to implement, I assume). 

### `cpu_get_trace`

*Functional description: retreives a `struct decoded_instruction` corresponding to the current value of `cpu.eip_phys`*

Traces are indexed by physical address, which seems strange at first since most protected mode code uses paging. The `I_NEXT(i)` macro, amongst other things, increments only the physical EIP value. The reason behind this is because the same physical page can be mapped in multiple locations in memory using paging, but there will be at most one physical page containing the code (and thus, every duplicate can share the same code). 

Previous versions of Halfix had issues with the same physical page being mapped in different virtual memory locations (as sometimes happens with shared libraries). It was caused by treating relative jumps as absolute ones with an immediate operand (i.e. instead of `jmp eax` it was `jmp abs 0x10000000`), so code compiled at virtual address, say, `0x12345000` wouldn't work properly when the virtual address was `0xFFFFF000`. This version treats relative jumps as relative jumps so code is, again, position independent. 

The EIP that `call` and a few other instructions generate is generated by a simple addition operation: `phys_eip + eip_bias + CS.base`. You're probably acquainted with `phys_eip` and `CS.base`, but `eip_bias` is slightly stranger: it's the difference between the physical EIP and linear EIP. 

Whenever we do paging checks for EIP (described below), we get a linear address as a by-product. `eip_bias` is simply `lin_eip - phys_eip`, and it's possible to recover `lin_eip` by adding `phys_eip` to `eip_bias` (`lin_eip - phys_eip + phys_eip` = `lin_eip - 0` = `lin_eip`). While that may seem strange at first, hopefully the following example clears things up and shows why keeping a separate `eip_bias` is such an important optimization. 

During early operating system boot, the kernel is usually loaded at the 1 MB mark (`0x00100000`), physically, and mapped to some location in virtual memory, usually `0xC0000000`. If we assume the kernel's entry point is `0x00100000`/`0xC0000000`, `eip_bias` becomes `bff00000`. 

Now let's say we execute one instruction -- say, `cli`. The physical EIP gets incremented by one (but `eip_bias` does not, for reasons that will become clear soon). Now the value of `LIN_EIP()` is: 

`LIN_EIP() = eip_bias + phys_eip = 0xBFF00000 + 0x00100001 = 0xC0000001`

Subsequent changes in `phys_eip`, as long as they're all relative (either additions or subtractions), will produce a valid linear EIP. 

`eip_bias` saves us from having to update both `lin_eip` and `phys_eip` after an instruction. While the code sequence may be non-obvious at first, it improved performance by over 30%. 

What if we cross a page boundary because of a jump or a fault? The important thing to note about the `eip_bias` scheme is that it produces the correct linear EIP even when the physical address is incorrect. Say that in our previous example, we crossed over from `0xC0000FFF` to `0xC0001000` (this, of course, would mean that physical EIP went from `0x00100FFF` to `0x00101000`). The thing is that we can't assume that `0xC0001000` maps to the very next physical page; it could map to `0x00102000`, `0x00104000`, or even `0xFEE00000` for all we know! But note that the linear address is correct (`0xC0001000`), even if the physical address (`0x00101000`) may not be (in x86 emulation, "may not be" is essentially the same as "is not" -- if it's possible for you to do something weird, than an operating system has done it before. No exceptions). 

Note that with paging, if you verify one byte of a page for reading/writing, then all the other 4,095 bytes become free game. If I verify address `0xC0001000` for reading/execution, then I don't have to check page access/permission bits for as long as execution remains on that frame (meaning that any jumps that have a target between `0xC0001000` and `0xC0001FFF` don't need to be verified!). Most x86 jumps remain on the same page, after all. 

`cpu_get_trace` begins by checking whether the top 20 bits of the current physical EIP (`phys_eip`) match the top 20 bits of the last physical EIP (`last_phys_eip`) we've translated (during startup, `phys_eip` is `0xFFFF0` and `last_phys_eip` is `0`, so it still works then). If they differ, then some paging checks are run, first verifying that the page is readable and then, in the case that PAE is supported, checking the TLB attribute bits\*. If either are invalid, we call `cpu_mmu_translate` using our still-valid linear EIP, which does the paging translation for us (the `| 8` forces `cpu_mmu_translate` to watch for the NX bit). Then some of our values are recomputed: `phys_eip` is converted from a pointer to a memory offset (using `PTR_TO_PHYS` and the TLB linear-to-physical resolution algorithm, which is similar to how the `eip_bias` thing works), `eip_bias` and `last_phys_eip` are computed given this new value. 

\* (Note that if PAE support is off, `cpu.tlb_attrs[lin_eip >> 12] & TLB_ATTR_NX` will always evaluate to zero). 

If a page fault occurs while we compute `phys_eip`, then all we're left with is the linear address, pulled from the IDT. But without a second call to `cpu_mmu_translate`, we can't tell what `phys_eip` is. 

One solution is to simply wrap `cpu_get_trace` in a giant loop: 

```c
struct decoded_instruction* cpu_get_trace(void)
{
    while (1) {
        if ((cpu.phys_eip ^ cpu.last_phys_eip) > 4095) {
            if (cpu_mmu_translate(lin_eip, cpu.tlb_shift_read | 8))
                continue; // Start from the top again
        }

        struct decoded_instruction* i = get_trace_somehow();
        return i; // Exit out of loop
    }
}
```

Emscripten generates suboptimal code in this case (using a multitude of `label` variables instead of one main loop), even with `-O3`. A slightly stranger, but no less correct, way is to return a dummy instruction that immediately calls `cpu_get_trace` once executed. This eliminates the need for the `while` loop, and simple tests have shown a 5-10% increase in performance, making this optimization "worth it." 

The dummy instruction in question is `temporary_placeholder`, and it simply invokes `op_trace_end`, whose sole purpose is to call `cpu_get_trace`. 

All translated instructions in the `struct decoded_instruction` format are kept in a huge table called `cpu.trace_cache`. During runtime, it's filled with instruction traces, seemingly plucked at random from main memory, invalidated traces (from `cpu/smc.c`), and garbage data from previous translations. To make sense of the trace cache, `cpu_get_trace` consults the an array of `struct trace_info`, `cpu.trace_info`. 

The "ideal" `cpu.trace_info` array would encompass every single byte in system memory so that there are absolutely no collisions. However, in practice we have a great deal less memory available to us (especially in the browser, with Emscripten), so we use a smaller table and accept the fact that we'll see collisions from time to time. It's possible to hash `cpu.phys_eip` so that collisions become less frequent, but there's not much of a performance boost if we do enable hashing, I've found. 

In the case that we either experience a `cpu.trace_info` collision *or* the corresponding trace entry isn't present, we overwrite the current values within that particular entry. 

To prevent buffer overflows, `cpu_decode_instruction` caps the maximum trace length to `MAX_TRACE_SIZE` instructions, which is hard-coded to 31 at the time of writing. While 31 instructions may seem stifling, the vast majority of traces are shorter than this; in fact, the optimal number of instructions per trace is eight, since `TRACE_INFO_ENTRIES / TRACE_CACHE_SIZE = 8`. I've found that this is a reasonable ratio for most real-world software. 



TBC