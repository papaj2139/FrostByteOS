# FrostByte
FrostByte (sometimes refered to as "FrostByte OS" or "FBOS") is a hobbyist operating system made by [HarryDoesTech](https://yt.harrydoestech.com).
FrostByte is the sucessor of CakeOS.
## Issues
If you encunter any issues please open an issue ticket or email **frostbyte.operatingsystem@gmail.com**

# Building
## Dependencies
- GNU make (BSD make won't work)
- i686-elf-gcc or i386-elf-gcc
- nasm
- qemu-system-i386 (optional, but if you want `make run` to work)


**recommended to build on Linux (I personally use Mint so i'm not too sure about other distros) but any other unix-like, e.x freeBSD, mac osx will work**
## Command
```bash
git clone https://github.com/FrostByte-OS/FrostByteOS.git
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

# Notes
- The OS uses a primitive initramfs; during compilation the Makefile assembles 'init.asm' and 'userapp.asm' (the shell) and puts the binary code into header files which the initramfs puts into files at runtime so the kernel can execute them
- The execution path by default is /bin/init -> /bin/forktest` (to change it to just execute /bin/sh directly change the filename in init.asm, altho forktest does exec /bin/sh anyway)


# Credits
Special thanks to:

@papaj2139 for helping out refactor the code
