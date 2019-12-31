# Compatibility

Some notes about the operating systems currently supported by Halfix: 

## DOS
All versions should work, although they might not boot from floppy disks due to unimplemented commands. Installing must be done on DOSBox or Bochs because swapping floppy disks is not supported. 

## Windows

### Windows 1.01
Boots from floppy disk image.

### Windows 2.02
Boots from floppy disk image.

### Windows 3.1
Boots from a 10 MB hard disk image. Mouse is laggy because it doesn't use `HLT`. 

### Windows 95
Boots from a 250 MB hard disk image. Works very well

### Windows 98 SE
Boots from a 300 MB hard disk image.

### Windows ME
Boots and runs. 

### Windows NT 4.0
Installs, boots, and runs. Very stable. Can self-virtualize if SP6 is installed. 

### Windows 2000
Boots and runs. 

### Windows XP
Installs, boots, and runs. Very stable. 

### Windows Vista
Takes forever to boot with the Pentium 4 CPU configuration. Set IPS to around 50,000,000 to get the nice Aero theme. Run with 512 MB of RAM. 

### Windows 7
The disk image I created requires a Pentium 4 or better to boot (it hangs on a `#UD` exception on FXSAVE). Unstable, it crashed with a BSOD. Can't use CDs because not all commands are implemented. Run with 512 MB of RAM. 

### Windows 8, 10
Won't boot. PAE, NX aren't implemented. I suspect that they'll need some SSSE3 instructions too. 

## OS/2 

Don't jiggle the mouse around during boot. It confuses the keyboard controller, which then causes OS/2 to be confused. 

### Warp 3
Boots and runs. 

### Warp 4.5
Boots and runs. DeScribe 5 works well. 

## Linux

### ISO Linux
Boots from CD and runs. 

### Damn Small Linux
Boots from CD and runs. Slightly longer boot time. 

### Debian
No PAE. Kernel doesn't like that. 

### Red Star OS 2

Boots fine, but mouse doesn't work. I suspect it has something to do with the outport. 

## ReactOS

Works fine, from the LiveCD. VGA emulation is funky, though. Icons don't render. Hopefully VESA will fix that. 
