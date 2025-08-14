# FrostByte
FrostBte (sometimes refered to as "FrostByte OS" or "FBOS") is a hobbyist operating system made by [HarryDoesTech](https://yt.harrydoestech.com).
FrostByte is the sucessor of CakeOS.

# Building
## Dependencies
- gcc
- nasm
- qemu-system-i386 (optional, but if you want `make run` to work)


**recommended to build on Linux (I personally use Mint so i'm not too sure about other distros**
## Command
```bash
git clone https://github.com/FrostByte-OS/FrostByteOS.git
cd FrostByteOS
make
```
if you want to run it in QEMU
```bash
make run
```
*note, this'll also rebuild it*
