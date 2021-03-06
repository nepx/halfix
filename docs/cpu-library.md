# CPU Library

When I was developing Halfix, I found it very difficult to isolate the CPU components of other x86 emulators, which would have been useful for running tests. While it certainly is possible, it's not as simple as extracting the `cpu/` directory and running the Makefile. I would often have to make substantial rewrites of various routines, which had a chance of introducing bugs. 

After writing Halfix, I found many uses for the CPU core outside of full-system emulation. For instance, you can check the correctness of other CPU emulators cycle-by-cycle using the instrumentation API, and you can also use the emulator to run the code generated by x86 JIT backends on non-x86 systems. 

The CPU component can be easily extracted and used as a library. The file `cpu/libcpu.c` provides a number of callbacks and routines that provide control over a wide variety of CPU functions. 

If you would like the CPU library to do something that it can't currently perform, then feel free to create an issue. 

## Building

It's available as a separate build target, `libcpu`:

```bash
 $ node makefile.js libcpu
```

All other flags apply, i.e. `release` and `--enable-wasm`. It can be build under the Emscripten and native targets. 

The entire library should compile to a single file, which you can link into your own projects. You may find the definitions within `include/cpu/libcpu.h` (*NOT* to be confused with `include/cpuapi.h`, which provides support for multiple CPU implementations within Halfix) to be useful. 

## License

The CPU library is extracted from GPLed code, and thus is licensed under the GPL. However, it's covered by the GPL linking exception: 

```
Linking this library statically or dynamically with other modules is making a combined work based on this library. Thus, the terms and conditions of the GNU General Public License cover the whole combination.

As a special exception, the copyright holders of this library give you permission to link this library with independent modules to produce an executable, regardless of the license terms of these independent modules, and to copy and distribute the resulting executable under terms of your choice, provided that you also meet, for each linked independent module, the terms and conditions of the license of that module. An independent module is a module which is not derived from or based on this library. If you modify this library, you may extend this exception to your version of the library, but you are not obliged to do so. If you do not wish to do so, delete this exception statement from your version.
```

## Limitations

Most of the limitations here are respective of Halfix as a whole. It's not like separating the CPU component into a library suddenly fixes bugs. 

The CPU library, like Halfix, is inherently single-threaded. The emulator is hard-wired to use a single `cpu` structure (so that's why we use `cpu.regs32` but not `cpu->regs32`). You might be able to get around this by using hacks to load the same library in multiple places, but a better way might be to simply use software task switching, for systems that have run in parallel (i.e. multiprocessor simulations). 

## API

### `libcpu_init`

This routine doesn't need to be called if your platform runs the `main` routine, but you can call it as many times as you like. It doesn't do anything currently, but if it did, it would set up internal things like pointers to various structs. It doesn't reset the CPU. 

### `cpu_get_state_ptr`

This is an important routine. It provides you with pointers to a number of tables and arrays within the CPU, like general purpose registers, segments, and the APIC base. All pointers are read-write and can be read or written at any point, even in the middle of an instruction (though be prepared for some less-than-savory side effects). Pointers are provided because they present less overhead than calling a routine every time you want to get or set EIP. 

Basically, you can modify any of these fields at will, and no internal CPU state will have to be modified -- with two exception. In the case that you modify `CPUPTR_SEG_ACCESS`, you'll also have to set `cpu.esp_mask` and `cpu.state_hash` accordingly, depending on the segment register being modified. Modifying the `CS` selector will also require to update the `cpu.cpl` field manually in protected mode. This behavior is intended -- there are certain instances when the lower three bits of CS don't line up with CPL (Virtual 8086 mode comes to mind). 

There's quite a bit of state pointers that you can query, though note that the library offers no protection against stupidity and careless pointer arithmetic. If you write past the end of the array, that's on you (`CPUPTR_GPR` returns an array of 16 `uint32_t`s, but note that the last eight MUST be set to zero for correct effective address decoding). 

Note that writing to these values won't cause an emulated CPU exception by themselves. 

There are "extra" segment registers beyond the usual `ES`/`CS`/`SS`/`DS`/`FS`/`GS` -- the IDTR, GDTR, etc. are all included in the array. You can find their indices within `cpu/cpu.h`. 

Example:

```c
uint32_t* regs = cpu_get_state_ptr(CPUPTR_GPR);
printf("Current value of ESP: %08x\n", regs[4]);
printf("Setting ESI to -1\n");
regs[6] = -1;
```

### `cpu_get_state`

Gets CPU state. Some values require computation (`cpu.eflags` doesn't contain the correct value of EFLAGS), others don't. 

Example:

```c
printf("Is the carry flag set? %s\n", cpu_get_state(CPU_EFLAGS) & 1 ? "Yes" : "No");
```

### `cpu_set_state`

Sets CPU state. It goes through all the proper channels -- setting the value of a segment register will force a lookup from the GDT/LDT. If you don't want to go through the hassle of setting up a GDT, you can set segment values directly by getting the `CPUPTR_SEG_DESC` pointer from `cpu_get_state_ptr`, but that also requires you to play with the descriptor caches directly. 

Example:

```c
// Enable real mode
cpu_set_state(MAKE_CR_ID(0), 0x60000010);
// Disable A20
cpu_set_state(CPU_A20, 0);
// Load 0xF000 into CS. This will automatically fill the descriptor caches as well. 
cpu_set_state(MAKE_SEG_ID(1), 0xF000);
// Since we want to boot at 0xFFFFFFF0, set the descriptor caches manually
uint32_t* segment_bases = cpu_get_state_ptr(CPUPTR_SEG_BASE);
segment_bases[0] = 0xFFFFF000;
cpu_set_state(CPU_EIP, 0xFFF0);
```

The function will return a non-zero value if something went wrong or an exception occurred. 

### `cpu_init_32bit`

Automatically sets up CPU in 32-bit flat protected mode, without paging. To be specific, the environment is set up like this:

```
 CS.base = DS.base = SS.base = 0
 CS.limit = DS.limit = SS.limit = -1
 ESP is 32-bit
 Instructions and data are 32-bit
 There are a total of 4 GB addressable. 
 IF is set, OSZAPC flags unmodified. 
 A20 is enabled
 CR0 = 1
```

If you want paging, you'll have to fiddle with `cpu_set_state` -- but in some cases, you can still provide a flat, user mode address space without paging, see below. 

### `cpu_init_fpu`

Enable the FPU. This was the topic of [#5](https://github.com/nepx/halfix/issues/5). Initializes FPU to a default state:

```
 Clears CR0.EM and CR0.TS
 Sets CR4.CR0_NE
 "Runs" the fninit instruction
```

You can learn more about what `fninit` does [here](https://www.felixcloutier.com/x86/finit:fninit). 

### `cpu_raise_irq_line`

Raise IRQ line. Sends a signal to the CPU that there's an interrupt ready. Pass the interrupt vector as the only argument. 

Useful for PIC emulations. Not for anything else. This routine is the only way to deliver an IRQ to the CPU. 

### `cpu_lower_irq_line`

Lowers the IRQ line. 

### `cpu_register_pic_ack`

The function pointer that's passed to this routine is executed when the CPU has accepted an interrupt. This could be used, for instance, to tell the PIC to to set the ISR bits. Otherwise, it'll be ignored. 

### `cpu_register_fpu_irq`

The function pointer passed to this routine will be called if the FPU raises the legacy IRQ13 line (OS/2 does this). It's *not* called when a `#MF` is triggered, only when the IRQ13 line is set high. 

### `cpu_enable_apic`

There's no onboard APIC packaged with the CPU emulation. All this routine does is let the CPU know whether an APIC is attached or not, and the values returned by `CPUID` will reflect this. 

Note that nothing's stopping you from calling `cpu_enable_apic(1)` and not providing an APIC implementation (like, for instance, for user mode emulation, where programs typically don't have permission to play with the APIC registers). It's up to you whether or not you want to have an APIC hooked up to memory address `0xFEE00000` or not. 

If you want an APIC with the CPU emulation, then you'll have to write your own or bundle `hardware/apic.c`. 

## Memory Management

The CPU library, unlike the regular system emulator, lets you create a "sparse" physical memory map, meaning that not all the pages need to be allocated at once and can be provided on demand. However, once a page is mapped into memory and is accessed, it can't be unmapped unless you want a segmentation fault; pointers to the page will be strewn all across the CPU's internal data structures, most notably the TLB. 

If you do want to get rid of a page, flush the TLB, including all kernel and global entries. 

If you want to "unregister" a handler, simply call the function again with `NULL` as the only argument. 

### `cpu_register_mem_refill_handler`

The function pointer provided here will be called every time the emulator needs a page of physical memory. It's called after paging translations, and the arguments will always be page-aligned, though the memory address you provide doesn't have to be (8-byte alignment should be fine). 

Note with this rotine, you don't need to care about how linear addresses are mapped; the emulator handles all of that for you. You just need to focus on providing pages of physical memory. 

Example:

```c
static void* provide_pages(uint32_t addr, int write) {
    void* res = do_page_lookup(addr, write);
    if (res) return res;
    else {
        res = calloc(1, 4096); // The default alignment of calloc should be fine. 
        add_page(addr, res, write);
        return res;
    }
}
static void init_mmap(void) {
    cpu_register_mem_refill_handler(provide_pages);
}
```

Note that physical memory also shares the same address space as MMIO accesses and ROM, and each page should be mapped read-write. The `write` flag is simply provided for your convienience -- just because `write` is zero when the function is called doesn't mean that the page will never, ever be written to. 

The page provider should always return a memory location. The MMU should never provide an address beyond the memory size you specify. If you return some kind of invalid pointer (`NULL`, for instance), the emulator will use that pointer, and bad things will happen. 

### `cpu_register_lin_refill_handler`

Provide pages of linear memory. This is useful for creating userspace emulators. If a handler is provided, then the page translation code is entirely bypassed -- the function pointer you provide is effectively the replacement for `cpu_mmu_translate`. If not page translation goes on as normal. This translation will occur even if `CR0.PG` is disabled. 

You can say that pages aren't mapped by returning `NULL`, which will cause an emulated page fault. 

Example:

```c
static void* provide_pages(uint32_t addr, int write) {
    void* res = do_page_lookup(addr, write);
    if (res) return res;
    else return NULL; // If needed, add something here to handle a page fault. 
}
static void init_mmap(void) {
    cpu_register_lin_refill_handler(provide_pages);
}
```

Note that pages will be mapped one-to-one, so be careful around MMIO locations and ROM areas. 

This is for linear memory, the address space seen after segmentation but before paging. Segments must be handled via an emulated GDT or perhaps playing around with the descriptor caches using `cpu_get_state_ptr`. 

`addr` will always be page aligned, but the resultant pointer doesn't have to be. You can also dynamically unmap and remap pages, but flush the TLB afterwards. 

### `cpu_register_ptr_to_phys`

This is a Halfix-specific function. Regardless of whether you're using physical or linear memory mapping, you will need to provide a function pointer for this! It should be able to convert a pointer to a chunk of memory back into a physical address (or linear address, if you happen to be using `cpu_register_lin_refill_handler`). The TLB is stored as a table of "adjusted" pointers, and certain routines, like `cpu_decode`, have to pull a pointer from the TLB and convert it back into a physical address. 

This function will be usually be called for values of `addr` that have already been mapped. In the case that it isn't (i.e. a bad far call transferred control to an unmapped page), then just return an invalid memory address. 

This is, by far, the most important function when it comes to memory management. 

Example: 

```c
static uint32_t ptr_to_phys(void* addr) {
    uint32_t res = do_reverse_lookup(addr);
    if (res) return res;
    else return 0xDEADB000; // Known-bad address that should never be mapped
}
static void init_mmap(void) {
    cpu_register_ptr_to_phys(provide_pages);
}
```

## Event Callbacks

Port I/O and MMIO handlers can be registered through the `cpu_register_{io|mmio}_{read|write}_cb` function. The defines look like this:

```c
// Port I/O reads are only called with one argument -- the port number -- but should return the value that was read
typedef uint32_t (*io_read_handler)(uint32_t address);
// Port I/O writes are called with the port number and the data
typedef void (*io_write_handler)(uint32_t address, uint32_t data);
// MMIO reads are called with two arguments -- the address and the size of the access. Should return the memory that was read. 
typedef uint32_t (*mmio_read_handler)(uint32_t address, int size);
// MMIO writes are called with three arguments -- the address, the data, and the size of the access. 
typedef void (*mmio_write_handler)(uint32_t address, uint32_t data, int size);
```

Note that for port I/O handlers, the size is already pre-determined by the second parameter to `cpu_register_io_{read|write}_cb`. 

Port I/O handlers will be called every time an I/O port is accessed. The CPU doesn't register any itself. 

MMIO handlers are called for a specific range of memory: `0xA0000`-`0xFFFFF` and any address over `cpu.memory_size` (which includes various MMIO devices, like the VBE LFB and the APIC -- this is where the 3 GB limit on the Readme comes from). You can play games with the MMIO handlers and turn ROM into RAM, as shown in this example:

```c
static void mmio_w_handler(uint32_t address, uint32_t data, int size) {
    if (address >= 0xA0000 && address < 0x100000) {
        switch (size) { 
            case 0: 
                *(uint8_t*)do_page_lookup(address) = data;
                break;
            case 1: 
                *(uint16_t*)do_page_lookup(address) = data;
                break;
            case 2: 
                *(uint32_t*)do_page_lookup(address) = data;
                break;
        }
        return;
    }
    // do something else here...
}
void mman_init(void) {
    cpu_register_mmio_write_cb(mmio_w_handler);
}
```