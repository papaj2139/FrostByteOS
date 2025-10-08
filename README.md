# FrostByte
FrostByte (sometimes refered to as "FrostByte OS" or "FBOS") is a hobbyist from-scratch operating system made by [HarryDoesTech](https://yt.harrydoestech.com).
FrostByte is the sucessor of CakeOS.
## Issues
If you encunter any issues please open an issue ticket or email **frostbyte.operatingsystem@gmail.com**

# Building
This repository currently contains the entire opearting system, including the userland. 
But that is a subject to change very soon,
THis repo will only contain the kernel and init system,standard C library,coreutils etc will be in seperate repositories
## Dependencies
- GNU make (BSD make won't work)
- i686-elf-gcc or i386-elf-gcc
- nasm
- qemu-system-i386 (optional, but if you want `make run` to work)


**recommended to build on Linux (I personally use Mint so i'm not too sure about other distros) but any other unix-like, e.x freeBSD, mac osx will work**
## Command
```bash
git clone https://github.com/FrostByteOS-project/FrostByteOS.git
cd FrostByteOS
make
```
If you're on a BSD:
```bash
gmake
```

if you want to run it in QEMU
```bash
make run
```
*note, this'll also rebuild it*

if you'd like to use i386-elf-gcc
```bash
make CC=i386-elf-gcc
```

# Requirements to run
- a VM or physical machine with BIOS or alternatively UEFI with CSM
- for real hardware like any PC made since ~1998 should work:
- 6MB of ram minimum
- An IA-32 or amd64 CPU
- PS/2 keyboard and (optional) mouse
- VESA-capable GPU
- (optional) an IDE hard drive


# Notes
- Currently only supports BIOS and x86, amd64 and UEFI support will be added in the future.
- Uses GRUB, will probably create a custom bootloader in the future.
- Uses ELF32 for binaries
- Forces VESA 1024x768x32 for the TTY console; will add fallback to standard VGA text mode later via kernel parameters

# TODO
- Add UEFI support
- Add USB support
- Improve scheduler
- ATAPI support
- PCIE detection

# Credits
Special thanks to:

@papaj2139 for helping out refactor the code
