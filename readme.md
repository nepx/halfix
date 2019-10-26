# Halfix x86 emulator

Halfix is a portable x86 emulator written in C99. It allows you to run legacy operating systems on more modern platforms. 

## Why?

I made this mostly for fun, and because it was a great way to learn about the x86 PC architecture. On a more practical level, it can be used for:
 - Testing out or developing operating systems
 - Running old programs or operating systems that no longer work on modern computers or you wouldn't want to risk running on your personal computer. 
 - Simulating other x86-based systems (the CPU component can be isolated relatively easily and used in other projects)
 - Testing web browser performance

## Building and Running

You will need `node.js`, a C99-compatible compiler, `libsdl`, `zlib`, and Emscripten (only if you're targeting the browser). Make sure that the required libraries are in a place where the compiler can get them. No prior configuration is required. 

For debug builds, run `node makefile.js` without any arguments. For a debug Emscripten build, pass `emscripten` to the file. For release builds, run `node makefile.js release`.  

Halfix requires disk images to be chunked and gzip'ed. Run `node tools/imgsplit.js [path-to-your-disk-image]` and modify the configuration file as required. 

**Summary**:

```
 $ node makefile.js release
 $ node tools/imgsplit.js os.img
 $ ./halfix
```

## Emulated Hardware

 - CPU: Pentium Pro-compatible (with FPU and optional Advanced Programmable Interrupt Controller)
 - Intel 8259 Programmable Interrupt Controller
 - Intel 8254 Programmable Interval Timer
 - Intel 8237 Direct Memory Access Controller
 - Intel 8042 "PS/2" Controller with attached keyboard and mouse
 - Generic VGA graphics card
 - Generic IDE controller (hard drive only)
 - i440FX chipset (this one doesn't work quite so well yet)
 - Intel 82093AA I/O Advanced Programmable Interrupt Controller
 - Dummy PC speaker (no sound)

## License

GNU General Public License version 3

## Similar Projects and Credits

 - [v86](https://www.github.com/copy/v86)
 - [JSLinux](http://bellard.org/jslinux/)
 - [jemul8](http://www.github.com/asmblah/jemul8)

The FPU emulator uses an modified version of [Berkeley SoftFloat](jhauser.us/arithmetic/SoftFloat.html) from the [Bochs](bochs.sourceforge.net) emulator. 