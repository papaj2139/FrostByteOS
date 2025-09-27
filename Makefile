ISO_NAME = frostbyte.iso
KERNEL = kernel.elf
CC = i686-elf-gcc
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Isrc/libc -Isrc -fno-stack-protector
ASM = nasm
ASMFLAGS = -f elf32
USER_CC = i686-elf-gcc
USER_CFLAGS = -m32 -ffreestanding -Os -Wall -Wextra -fno-stack-protector -fno-omit-frame-pointer -nostdlib -Iuser/libc/include
USER_PIC_CFLAGS = $(USER_CFLAGS)
LDFLAGS = -m32 -nostdlib -T linker.ld
USER_LIBC_OBJS = user/libc/crt0.o user/libc/syscalls.o user/libc/string.o user/libc/stdio.o user/libc/errno.o user/libc/signal.o
INITRAMFS_DIR := initramfs_root
#VERY soon this will get horendously long and complex find a better solution maybe just a wildcard?
.PHONY: all clean run iso

all: iso

boot.o: src/boot.asm
	$(ASM) $(ASMFLAGS) $< -o $@

kernel.o: src/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

desktop.o: src/desktop.c
	$(CC) $(CFLAGS) -c $< -o $@

string.o: src/libc/string.c
	$(CC) $(CFLAGS) -c $< -o $@

stdlib.o: src/libc/stdlib.c
	$(CC) $(CFLAGS) -c $< -o $@

io.o: src/io.c
	$(CC) $(CFLAGS) -c $< -o $@

font.o: src/font.c
	$(CC) $(CFLAGS) -c $< -o $@

keyboard.o: src/drivers/keyboard.c
	$(CC) $(CFLAGS) -c $< -o $@

mouse.o: src/drivers/mouse.c
	$(CC) $(CFLAGS) -c $< -o $@

tty.o: src/drivers/tty.c
	$(CC) $(CFLAGS) -c $< -o $@

serial.o: src/drivers/serial.c
	$(CC) $(CFLAGS) -c $< -o $@

pc_speaker.o: src/drivers/pc_speaker.c
	$(CC) $(CFLAGS) -c $< -o $@

timer.o: src/drivers/timer.c
	$(CC) $(CFLAGS) -c $< -o $@

rtc.o: src/drivers/rtc.c
	$(CC) $(CFLAGS) -c $< -o $@

ata.o: src/drivers/ata.c
	$(CC) $(CFLAGS) -c $< -o $@

vga.o: src/gui/vga.c
	$(CC) $(CFLAGS) -c $< -o $@

vga_dev.o: src/drivers/vga_dev.c
	$(CC) $(CFLAGS) -c $< -o $@

idt.o: src/interrupts/idt.c
	$(CC) $(CFLAGS) -c $< -o $@

irq.o: src/interrupts/irq.c
	$(CC) $(CFLAGS) -c $< -o $@

pic.o: src/interrupts/pic.c
	$(CC) $(CFLAGS) -c $< -o $@

isr.o: src/interrupts/isr.asm
	$(ASM) $(ASMFLAGS) $< -o $@

isr_c.o: src/interrupts/isr.c
	$(CC) $(CFLAGS) -c $< -o $@

gdt.o: src/interrupts/gdt.c
	$(CC) $(CFLAGS) -c $< -o $@

gdt_asm.o: src/interrupts/gdt_asm.asm
	$(ASM) $(ASMFLAGS) $< -o $@

tss.o: src/interrupts/tss.c
	$(CC) $(CFLAGS) -c $< -o $@

syscall.o: src/syscall.c
	$(CC) $(CFLAGS) -Isrc -c src/syscall.c -o syscall.o

elf.o: src/kernel/elf.c
	$(CC) $(CFLAGS) -Isrc -c src/kernel/elf.c -o elf.o

dynlink.o: src/kernel/dynlink.c src/kernel/dynlink.h
	$(CC) $(CFLAGS) -Isrc -c src/kernel/dynlink.c -o dynlink.o

syscall_asm.o: src/syscall_asm.asm
	$(ASM) $(ASMFLAGS) $< -o $@

device_manager.o: src/device_manager.c
	$(CC) $(CFLAGS) -c $< -o $@

fat16.o: src/fs/fat16.c
	$(CC) $(CFLAGS) -c $< -o $@

fs.o: src/fs/fs.c
	$(CC) $(CFLAGS) -c $< -o $@

vfs.o: src/fs/vfs.c
	$(CC) $(CFLAGS) -c $< -o $@

devfs.o: src/fs/devfs.c
	$(CC) $(CFLAGS) -c $< -o $@

procfs.o: src/fs/procfs.c
	$(CC) $(CFLAGS) -c $< -o $@

initramfs_cpio.o: src/fs/initramfs_cpio.c
	$(CC) $(CFLAGS) -c $< -o $@

initramfs.o: src/fs/initramfs.c
	$(CC) $(CFLAGS) -c $< -o $@

user/libc/crt0.o: user/libc/crt0.asm
	$(ASM) $(ASMFLAGS) $< -o $@

user/libc/syscalls.o: user/libc/src/syscalls.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/libc/string.o: user/libc/src/string.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/libc/stdio.o: user/libc/src/stdio.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/libc/errno.o: user/libc/src/errno.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/libc/signal.o: user/libc/src/signal.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

# PIC objects for shared libc
user/libc/syscalls.pic.o: user/libc/src/syscalls.c
	$(USER_CC) $(USER_PIC_CFLAGS) -c $< -o $@

user/libc/string.pic.o: user/libc/src/string.c
	$(USER_CC) $(USER_PIC_CFLAGS) -c $< -o $@

user/libc/stdio.pic.o: user/libc/src/stdio.c
	$(USER_CC) $(USER_PIC_CFLAGS) -c $< -o $@

user/libc/errno.pic.o: user/libc/src/errno.c
	$(USER_CC) $(USER_PIC_CFLAGS) -c $< -o $@

user/libc/signal.pic.o: user/libc/src/signal.c
	$(USER_CC) $(USER_PIC_CFLAGS) -c $< -o $@

user/libc/libc.so.1: user/libc/syscalls.pic.o user/libc/string.pic.o user/libc/stdio.pic.o user/libc/errno.pic.o user/libc/signal.pic.o
	i686-elf-ld -shared -m elf_i386 -nostdlib -soname libc.so.1 $^ -o $@

user/init.o: user/init.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/forktest.o: user/forktest.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/fbsh.o: user/fbsh.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/init.elf: user/init.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/init.o -o $@

user/forktest.elf: user/forktest.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/forktest.o -L user/libc -l:libc.so.1 -o $@

user/fbsh.elf: user/fbsh.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/fbsh.o -L user/libc -l:libc.so.1 -o $@

user/mount.o: user/mount.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/mount.elf: user/mount.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/mount.o -L user/libc -l:libc.so.1 -o $@

user/ls.o: user/ls.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/ls.elf: user/ls.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/ls.o -L user/libc -l:libc.so.1 -o $@

user/echo.o: user/echo.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/echo.elf: user/echo.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/echo.o -L user/libc -l:libc.so.1 -o $@

user/cat.o: user/cat.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/cat.elf: user/cat.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/cat.o -L user/libc -l:libc.so.1 -o $@

user/touch.o: user/touch.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/touch.elf: user/touch.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/touch.o -L user/libc -l:libc.so.1 -o $@

user/mkdir.o: user/mkdir.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/mkdir.elf: user/mkdir.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/mkdir.o -L user/libc -l:libc.so.1 -o $@

user/write.o: user/write.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/write.elf: user/write.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/write.o -L user/libc -l:libc.so.1 -o $@

user/kill.o: user/kill.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/kill.elf: user/kill.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/kill.o -L user/libc -l:libc.so.1 -o $@

user/ln.o: user/ln.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/ln.elf: user/ln.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/ln.o -L user/libc -l:libc.so.1 -o $@

user/ps.o: user/ps.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/ps.elf: user/ps.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/ps.o -L user/libc -l:libc.so.1 -o $@

user/mkfat16.o: user/mkfat16.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/mkfat16.elf: user/mkfat16.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/mkfat16.o -L user/libc -l:libc.so.1 -o $@

user/lsblk.o: user/lsblk.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/lsblk.elf: user/lsblk.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/lsblk.o -L user/libc -l:libc.so.1 -o $@

user/partmk.o: user/partmk.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/partmk.elf: user/partmk.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/partmk.o -L user/libc -l:libc.so.1 -o $@

user/crash.o: user/crash.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/crash.elf: user/crash.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/crash.o -L user/libc -l:libc.so.1 -o $@

user/waitshow.o: user/waitshow.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/waitshow.elf: user/waitshow.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/waitshow.o -L user/libc -l:libc.so.1 -o $@

user/ldd.o: user/ldd.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/ldd.elf: user/ldd.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/ldd.o -L user/libc -l:libc.so.1 -o $@

user/true.o: user/true.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/true.elf: user/true.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/true.o -L user/libc -l:libc.so.1 -o $@

user/false.o: user/false.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/false.elf: user/false.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/false.o -L user/libc -l:libc.so.1 -o $@

user/sleep.o: user/sleep.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/sleep.elf: user/sleep.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/sleep.o -L user/libc -l:libc.so.1 -o $@

user/dltest.o: user/dltest.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/dltest.elf: user/dltest.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/dltest.o -L user/libc -l:libc.so.1 -o $@

user/hello_dyn.o: user/hello_dyn.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/hello_dyn.elf: user/hello_dyn.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/hello_dyn.o -L user/libc -l:libc.so.1 -o $@

# New small utilities
user/uname.o: user/uname.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/uname.elf: user/uname.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/uname.o -L user/libc -l:libc.so.1 -o $@

user/uptime.o: user/uptime.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/uptime.elf: user/uptime.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/uptime.o -L user/libc -l:libc.so.1 -o $@

user/free.o: user/free.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/free.elf: user/free.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/free.o -L user/libc -l:libc.so.1 -o $@

user/env.o: user/env.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/env.elf: user/env.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/env.o -L user/libc -l:libc.so.1 -o $@

user/yes.o: user/yes.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/yes.elf: user/yes.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/yes.o -L user/libc -l:libc.so.1 -o $@

user/head.o: user/head.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/head.elf: user/head.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/head.o -L user/libc -l:libc.so.1 -o $@

user/wc.o: user/wc.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/wc.elf: user/wc.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/wc.o -L user/libc -l:libc.so.1 -o $@

user/hd.o: user/hd.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/hd.elf: user/hd.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/hd.o -L user/libc -l:libc.so.1 -o $@

user/which.o: user/which.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/which.elf: user/which.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/which.o -L user/libc -l:libc.so.1 -o $@

user/vplay.o: user/vplay.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/vplay.elf: user/vplay.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/vplay.o -L user/libc -l:libc.so.1 -o $@

user/chmod.o: user/chmod.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/chmod.elf: user/chmod.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/chmod.o -L user/libc -l:libc.so.1 -o $@

user/chown.o: user/chown.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/chown.elf: user/chown.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/chown.o -L user/libc -l:libc.so.1 -o $@

user/stat.o: user/stat.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/stat.elf: user/stat.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/stat.o -L user/libc -l:libc.so.1 -o $@

user/whoami.o: user/whoami.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/whoami.elf: user/whoami.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/whoami.o -L user/libc -l:libc.so.1 -o $@

user/id.o: user/id.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/id.elf: user/id.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/id.o -L user/libc -l:libc.so.1 -o $@

user/pwd.o: user/pwd.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/pwd.elf: user/pwd.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/pwd.o -L user/libc -l:libc.so.1 -o $@

user/cp.o: user/cp.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/cp.elf: user/cp.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/cp.o -L user/libc -l:libc.so.1 -o $@

user/mv.o: user/mv.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/mv.elf: user/mv.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/mv.o -L user/libc -l:libc.so.1 -o $@

user/rm.o: user/rm.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/rm.elf: user/rm.o user/libc/crt0.o user/libc/libc.so.1
	i686-elf-ld -m elf_i386 -nostdlib -e _start -rpath=/lib --enable-new-dtags user/libc/crt0.o user/rm.o -L user/libc -l:libc.so.1 -o $@

fat16_vfs.o: src/fs/fat16_vfs.c
	$(CC) $(CFLAGS) -c $< -o $@

fd.o: src/fd.c
	$(CC) $(CFLAGS) -c $< -o $@

pmm.o: src/mm/pmm.c
	$(CC) $(CFLAGS) -c $< -o $@

vmm.o: src/mm/vmm.c
	$(CC) $(CFLAGS) -c $< -o $@

cga.o: src/kernel/cga.c
	$(CC) $(CFLAGS) -c $< -o $@

panic.o: src/kernel/panic.c
	$(CC) $(CFLAGS) -c $< -o $@

klog.o: src/kernel/klog.c
	$(CC) $(CFLAGS) -c $< -o $@

kreboot.o: src/kernel/kreboot.c
	$(CC) $(CFLAGS) -c $< -o $@

kshutdown.o: src/kernel/kshutdown.c
	$(CC) $(CFLAGS) -c $< -o $@

signal.o: src/kernel/signal.c
	$(CC) $(CFLAGS) -c $< -o $@

uaccess.o: src/kernel/uaccess.c
	$(CC) $(CFLAGS) -c $< -o $@

acpi.o: src/arch/x86/acpi.c
	$(CC) $(CFLAGS) -c $< -o $@

heap.o: src/mm/heap.c
	$(CC) $(CFLAGS) -c $< -o $@

paging_asm.o: src/mm/paging_asm.asm
	$(ASM) $(ASMFLAGS) $< -o $@

process.o: src/process.c
	$(CC) $(CFLAGS) -c $< -o $@

process_asm.o: src/process_asm.asm
	$(ASM) $(ASMFLAGS) $< -o $@

$(KERNEL): boot.o kernel.o desktop.o string.o stdlib.o io.o font.o \
           keyboard.o mouse.o tty.o serial.o pc_speaker.o timer.o rtc.o ata.o \
           vga.o vga_dev.o idt.o irq.o pic.o isr.o isr_c.o gdt.o gdt_asm.o tss.o \
           syscall.o syscall_asm.o device_manager.o fat16.o fs.o vfs.o fat16_vfs.o devfs.o procfs.o fd.o initramfs.o initramfs_cpio.o \
           pmm.o vmm.o heap.o paging_asm.o process.o process_asm.o \
           acpi.o cga.o panic.o klog.o kreboot.o kshutdown.o signal.o uaccess.o elf.o dynlink.o
	$(CC) $(LDFLAGS) -o $@ $^

initramfs.cpio: user/init.elf user/forktest.elf user/fbsh.elf user/mount.elf user/ls.elf user/echo.elf user/cat.elf user/touch.elf user/mkdir.elf user/write.elf user/kill.elf user/ln.elf user/ps.elf user/mkfat16.elf user/lsblk.elf user/partmk.elf user/crash.elf user/waitshow.elf user/ldd.elf user/dltest.elf user/hello_dyn.elf user/chmod.elf user/chown.elf user/stat.elf user/whoami.elf user/id.elf user/pwd.elf user/cp.elf user/mv.elf user/rm.elf user/true.elf user/false.elf user/sleep.elf user/uname.elf user/uptime.elf user/free.elf user/env.elf user/yes.elf user/head.elf user/wc.elf user/hd.elf user/which.elf user/vplay.elf user/libc/libc.so.1
	rm -rf $(INITRAMFS_DIR) initramfs.cpio
	mkdir -p $(INITRAMFS_DIR)/bin $(INITRAMFS_DIR)/etc $(INITRAMFS_DIR)/dev $(INITRAMFS_DIR)/proc $(INITRAMFS_DIR)/mnt $(INITRAMFS_DIR)/usr/bin $(INITRAMFS_DIR)/lib
	cp user/init.elf $(INITRAMFS_DIR)/bin/init
	cp user/fbsh.elf $(INITRAMFS_DIR)/bin/sh
	cp user/forktest.elf $(INITRAMFS_DIR)/bin/forktest
	cp user/mount.elf $(INITRAMFS_DIR)/bin/mount
	cp user/ls.elf $(INITRAMFS_DIR)/bin/ls
	cp user/echo.elf $(INITRAMFS_DIR)/bin/echo
	cp user/cat.elf $(INITRAMFS_DIR)/bin/cat
	cp user/touch.elf $(INITRAMFS_DIR)/bin/touch
	cp user/mkdir.elf $(INITRAMFS_DIR)/bin/mkdir
	cp user/write.elf $(INITRAMFS_DIR)/bin/write
	cp user/kill.elf $(INITRAMFS_DIR)/bin/kill
	cp user/ln.elf $(INITRAMFS_DIR)/bin/ln
	cp user/ps.elf $(INITRAMFS_DIR)/bin/ps
	cp user/mkfat16.elf $(INITRAMFS_DIR)/bin/mkfat16
	cp user/lsblk.elf $(INITRAMFS_DIR)/bin/lsblk
	cp user/partmk.elf $(INITRAMFS_DIR)/bin/partmk
	cp user/crash.elf $(INITRAMFS_DIR)/bin/crash
	cp user/waitshow.elf $(INITRAMFS_DIR)/bin/waitshow
	cp user/ldd.elf $(INITRAMFS_DIR)/bin/ldd
	cp user/dltest.elf $(INITRAMFS_DIR)/bin/dltest
	cp user/hello_dyn.elf $(INITRAMFS_DIR)/bin/hello-dyn
	cp user/chmod.elf $(INITRAMFS_DIR)/bin/chmod
	cp user/chown.elf $(INITRAMFS_DIR)/bin/chown
	cp user/stat.elf $(INITRAMFS_DIR)/bin/stat
	cp user/whoami.elf $(INITRAMFS_DIR)/bin/whoami
	cp user/id.elf $(INITRAMFS_DIR)/bin/id
	cp user/pwd.elf $(INITRAMFS_DIR)/bin/pwd
	cp user/cp.elf $(INITRAMFS_DIR)/bin/cp
	cp user/mv.elf $(INITRAMFS_DIR)/bin/mv
	cp user/rm.elf $(INITRAMFS_DIR)/bin/rm
	cp user/true.elf $(INITRAMFS_DIR)/bin/true
	cp user/false.elf $(INITRAMFS_DIR)/bin/false
	cp user/sleep.elf $(INITRAMFS_DIR)/bin/sleep
	cp user/uname.elf $(INITRAMFS_DIR)/bin/uname
	cp user/uptime.elf $(INITRAMFS_DIR)/bin/uptime
	cp user/free.elf $(INITRAMFS_DIR)/bin/free
	cp user/env.elf $(INITRAMFS_DIR)/bin/env
	cp user/yes.elf $(INITRAMFS_DIR)/bin/yes
	cp user/head.elf $(INITRAMFS_DIR)/bin/head
	cp user/wc.elf $(INITRAMFS_DIR)/bin/wc
	cp user/hd.elf $(INITRAMFS_DIR)/bin/hd
	cp user/which.elf $(INITRAMFS_DIR)/bin/which
	cp user/vplay.elf $(INITRAMFS_DIR)/bin/vplay
	cp user/libc/libc.so.1 $(INITRAMFS_DIR)/lib/libc.so.1
	echo "Welcome to FrostByte (cpio initramfs)" > $(INITRAMFS_DIR)/etc/motd
	ln -sf /etc/motd $(INITRAMFS_DIR)/motd_link
	ln -sf /bin/sh $(INITRAMFS_DIR)/usr/bin/sh
	cd $(INITRAMFS_DIR) && find . | cpio -o -H newc > ../initramfs.cpio

iso: $(KERNEL) initramfs.cpio
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/
	cp boot/grub/grub.cfg isodir/boot/grub/
	cp initramfs.cpio isodir/boot/initramfs.cpio
	grub-mkrescue -o $(ISO_NAME) isodir

run: iso disk.img
	qemu-system-i386 -cdrom $(ISO_NAME)

run-serial: iso disk.img
	qemu-system-i386 -cdrom $(ISO_NAME) -serial stdio -boot d

run-sound: iso disk.img
	qemu-system-i386 -cdrom $(ISO_NAME) -audiodev alsa,id=snd0 -machine pcspk-audiodev=snd0

disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=64

.PHONY: disk-reset
disk-reset:
	rm -f disk.img
	dd if=/dev/zero of=disk.img bs=1M count=64

clean:
	rm -rf *.o $(KERNEL) $(ISO_NAME) isodir $(INITRAMFS_DIR) initramfs.cpio user/*.o user/*.elf
