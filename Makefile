ISO_NAME = frostbyte.iso
KERNEL = kernel.elf
CC = i686-elf-gcc
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Isrc/libc -Isrc -fno-stack-protector
ASM = nasm
ASMFLAGS = -f elf32
USER_CC = i686-elf-gcc
USER_CFLAGS = -m32 -ffreestanding -Os -Wall -Wextra -fno-stack-protector -nostdlib -Iuser/libc/include
LDFLAGS = -m32 -nostdlib -T linker.ld
USER_LIBC_OBJS = user/libc/crt0.o user/libc/syscalls.o user/libc/string.o
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

user/init.o: user/init.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/forktest.o: user/forktest.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/fbsh.o: user/fbsh.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/init.elf: user/init.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/init.o -o $@

user/forktest.elf: user/forktest.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/forktest.o -o $@

user/fbsh.elf: user/fbsh.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/fbsh.o -o $@

user/mount.o: user/mount.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/mount.elf: user/mount.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/mount.o -o $@

user/ls.o: user/ls.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/ls.elf: user/ls.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/ls.o -o $@

user/echo.o: user/echo.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/echo.elf: user/echo.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/echo.o -o $@

user/cat.o: user/cat.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/cat.elf: user/cat.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/cat.o -o $@

user/touch.o: user/touch.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/touch.elf: user/touch.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/touch.o -o $@

user/mkdir.o: user/mkdir.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/mkdir.elf: user/mkdir.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/mkdir.o -o $@

user/write.o: user/write.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/write.elf: user/write.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/write.o -o $@

user/kill.o: user/kill.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/kill.elf: user/kill.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/kill.o -o $@

user/ln.o: user/ln.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/ln.elf: user/ln.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/ln.o -o $@

user/ps.o: user/ps.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/ps.elf: user/ps.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/ps.o -o $@

user/mkfat16.o: user/mkfat16.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/mkfat16.elf: user/mkfat16.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/mkfat16.o -o $@

user/lsblk.o: user/lsblk.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/lsblk.elf: user/lsblk.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/lsblk.o -o $@

user/partmk.o: user/partmk.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

user/partmk.elf: user/partmk.o $(USER_LIBC_OBJS) user.ld
	i686-elf-ld -m elf_i386 -nostdlib -T user.ld $(USER_LIBC_OBJS) user/partmk.o -o $@

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
           acpi.o cga.o panic.o klog.o kreboot.o kshutdown.o signal.o uaccess.o elf.o
	$(CC) $(LDFLAGS) -o $@ $^

initramfs.cpio: user/init.elf user/forktest.elf user/fbsh.elf user/mount.elf user/ls.elf user/echo.elf user/cat.elf user/touch.elf user/mkdir.elf user/write.elf user/kill.elf user/ln.elf user/ps.elf user/mkfat16.elf user/lsblk.elf user/partmk.elf
	rm -rf $(INITRAMFS_DIR) initramfs.cpio
	mkdir -p $(INITRAMFS_DIR)/bin $(INITRAMFS_DIR)/etc $(INITRAMFS_DIR)/dev $(INITRAMFS_DIR)/proc $(INITRAMFS_DIR)/mnt $(INITRAMFS_DIR)/usr/bin
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
	qemu-system-i386 -cdrom $(ISO_NAME) -hda disk.img

run-serial: iso disk.img
	qemu-system-i386 -cdrom $(ISO_NAME) -serial stdio -boot d -hda disk.img

run-sound: iso disk.img
	qemu-system-i386 -cdrom $(ISO_NAME) -audiodev alsa,id=snd0 -machine pcspk-audiodev=snd0 -hda disk.img

disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=64

.PHONY: disk-reset
disk-reset:
	rm -f disk.img
	dd if=/dev/zero of=disk.img bs=1M count=64

clean:
	rm -rf *.o $(KERNEL) $(ISO_NAME) isodir $(INITRAMFS_DIR) initramfs.cpio user/*.o user/*.elf
