ISO_NAME = frostbyte.iso
KERNEL = kernel.elf
CC = i686-elf-gcc
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Isrc/libc -Isrc -fno-stack-protector
ASM = nasm
ASMFLAGS = -f elf32
AR = i686-elf-ar
USER_CC = i686-elf-gcc
USER_LD = i686-elf-ld
USER_CFLAGS = -m32 -ffreestanding -Os -Wall -Wextra -fno-stack-protector -fno-omit-frame-pointer -nostdlib
USER_PIC_CFLAGS = $(USER_CFLAGS)
LDFLAGS = -m32 -nostdlib -T linker.ld
USER_LIBC_DIR := user/libc
USER_LIBUSER_DIR := user/libuser
INITRAMFS_DIR := initramfs_root
.PHONY: all clean run iso menuconfig tests

all: iso

boot.o: src/boot.asm
	$(ASM) $(ASMFLAGS) $< -o $@

kernel.o: src/kernel.c
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

sb16.o: src/drivers/sb16.c src/drivers/sb16.h
	$(CC) $(CFLAGS) -c src/drivers/sb16.c -o $@

pc_speaker.o: src/drivers/pc_speaker.c
	$(CC) $(CFLAGS) -c $< -o $@

timer.o: src/drivers/timer.c
	$(CC) $(CFLAGS) -c $< -o $@

rtc.o: src/drivers/rtc.c
	$(CC) $(CFLAGS) -c $< -o $@

ata.o: src/drivers/ata.c
	$(CC) $(CFLAGS) -c $< -o $@

pci.o: src/drivers/pci.c
	$(CC) $(CFLAGS) -c $< -o $@

ahci.o: src/drivers/ahci.c
	$(CC) $(CFLAGS) -c $< -o $@

apic.o: src/drivers/apic.c
	$(CC) $(CFLAGS) -c $< -o $@

vga.o: src/gui/vga.c
	$(CC) $(CFLAGS) -c $< -o $@

vga_dev.o: src/drivers/vga_dev.c
	$(CC) $(CFLAGS) -c $< -o $@

fb.o: src/drivers/fb.c
	$(CC) $(CFLAGS) -c $< -o $@

fbcon.o: src/drivers/fbcon.c
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

fat32.o: src/fs/fat32.c
	$(CC) $(CFLAGS) -c $< -o $@

fs.o: src/fs/fs.c
	$(CC) $(CFLAGS) -c $< -o $@

vfs.o: src/fs/vfs.c
	$(CC) $(CFLAGS) -c $< -o $@

fat16_vfs.o: src/fs/fat16_vfs.c
	$(CC) $(CFLAGS) -c $< -o $@

fat32_vfs.o: src/fs/fat32_vfs.c
	$(CC) $(CFLAGS) -c $< -o $@

devfs.o: src/fs/devfs.c
	$(CC) $(CFLAGS) -c $< -o $@

procfs.o: src/fs/procfs.c
	$(CC) $(CFLAGS) -c $< -o $@

tmpfs.o: src/fs/tmpfs.c
	$(CC) $(CFLAGS) -c $< -o $@

initramfs_cpio.o: src/fs/initramfs_cpio.c
	$(CC) $(CFLAGS) -c $< -o $@

initramfs.o: src/fs/initramfs.c
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

scheduler.o: src/scheduler.c
	$(CC) $(CFLAGS) -c $< -o $@

process_asm.o: src/process_asm.asm
	$(ASM) $(ASMFLAGS) $< -o $@

shm.o: src/ipc/shm.c
	$(CC) $(CFLAGS) -c $< -o $@

socket.o: src/ipc/socket.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): boot.o kernel.o string.o stdlib.o io.o font.o \
		   keyboard.o mouse.o tty.o serial.o sb16.o pc_speaker.o timer.o rtc.o ata.o pci.o ahci.o apic.o \
		   vga.o vga_dev.o fb.o fbcon.o idt.o irq.o pic.o isr.o isr_c.o gdt.o gdt_asm.o tss.o \
		   syscall.o syscall_asm.o device_manager.o fat16.o fat32.o fs.o vfs.o fat16_vfs.o fat32_vfs.o devfs.o procfs.o tmpfs.o fd.o initramfs.o initramfs_cpio.o \
		   pmm.o vmm.o heap.o paging_asm.o process.o process_asm.o scheduler.o \
		   acpi.o cga.o panic.o klog.o kreboot.o kshutdown.o signal.o uaccess.o elf.o dynlink.o shm.o socket.o
	$(CC) $(LDFLAGS) -o $@ $^

.PHONY: user_libc user_libuser user_libs user_coreutils user_frostywm user_desktop user_apps userspace

user_libc:
	$(MAKE) -C $(USER_LIBC_DIR) CC=$(USER_CC) AS=$(ASM) AR=$(AR) CFLAGS="$(USER_CFLAGS) -Iinclude" PIC_CFLAGS="$(USER_PIC_CFLAGS) -Iinclude" ASMFLAGS="$(ASMFLAGS)"

user_libuser:
	$(MAKE) -C $(USER_LIBUSER_DIR) CC=$(USER_CC) LD=$(USER_LD) AR=$(AR) CFLAGS="$(USER_CFLAGS) -I../libc/include -Iinclude" PIC_CFLAGS="$(USER_PIC_CFLAGS) -I../libc/include -Iinclude"

user_libs: user_libc user_libuser

user_frostyinit: user_libs
	$(MAKE) -C user/FrostyInit USER_CC=$(USER_CC) USER_LD=$(USER_LD) USER_CFLAGS="$(USER_CFLAGS) -I../libc/include -I../libuser/include"

user_coreutils: user_libs
	$(MAKE) -C user/coreutils USER_CC=$(USER_CC) USER_LD=$(USER_LD) USER_CFLAGS="$(USER_CFLAGS) -I../libc/include -I../libuser/include" USER_LDFLAGS="-m elf_i386 -nostdlib -dynamic-linker /lib/libc.so.1 -e _start -rpath=/lib --enable-new-dtags"

user_frostywm: user_libs
	$(MAKE) -C user/frostywm USER_CC=$(USER_CC) USER_LD=$(USER_LD) USER_CFLAGS="$(USER_CFLAGS) -I../libc/include -I../libuser/include -I." USER_LDFLAGS="-m elf_i386 -nostdlib -dynamic-linker /lib/libc.so.1 -e _start -rpath=/lib --enable-new-dtags"

user_desktop: user_frostywm
	$(MAKE) -C user/desktop USER_CC=$(USER_CC) USER_LD=$(USER_LD) USER_CFLAGS="$(USER_CFLAGS) -I../libc/include -I../libuser/include -I../frostywm" USER_LDFLAGS="-m elf_i386 -nostdlib -dynamic-linker /lib/libc.so.1 -e _start -rpath=/lib --enable-new-dtags"

user_apps: user_libs
	$(MAKE) -C user USER_CC=$(USER_CC) USER_LD=$(USER_LD) USER_CFLAGS="$(USER_CFLAGS) -Ilibc/include -Ilibuser/include" USER_LDFLAGS="-m elf_i386 -nostdlib -dynamic-linker /lib/libc.so.1 -e _start -rpath=/lib --enable-new-dtags"

userspace: user_libs user_frostyinit user_coreutils user_frostywm user_desktop user_apps

tests: user_libs
	$(MAKE) -C tests USER_CC=$(USER_CC) USER_LD=$(USER_LD)

user/init.elf: user/libc/libc.a user/libuser/libuser.a
	$(MAKE) -C user/FrostyInit USER_CC=$(USER_CC) USER_LD=$(USER_LD) USER_CFLAGS="$(USER_CFLAGS) -I../libc/include -I../libuser/include"

user/libc/libc.a:
	$(MAKE) -C user/libc CC=$(USER_CC) AS=$(ASM) AR=$(AR) \
	CFLAGS="$(USER_CFLAGS) -Iinclude" PIC_CFLAGS="$(USER_PIC_CFLAGS) -Iinclude" ASMFLAGS="$(ASMFLAGS)" static

user/libc/libc.so.1:
	$(MAKE) -C user/libc CC=$(USER_CC) AS=$(ASM) AR=$(AR) \
	CFLAGS="$(USER_CFLAGS) -Iinclude" PIC_CFLAGS="$(USER_PIC_CFLAGS) -Iinclude" ASMFLAGS="$(ASMFLAGS)" shared

user/libuser/libuser.a:
	$(MAKE) -C user/libuser CC=$(USER_CC) LD=$(USER_LD) AR=$(AR) \
	CFLAGS="$(USER_CFLAGS) -I../libc/include -Iinclude" PIC_CFLAGS="$(USER_PIC_CFLAGS) -I../libc/include -Iinclude" static

user/libuser/libuser.so.1:
	$(MAKE) -C user/libuser CC=$(USER_CC) LD=$(USER_LD) AR=$(AR) \
	CFLAGS="$(USER_CFLAGS) -I../libc/include -Iinclude" PIC_CFLAGS="$(USER_PIC_CFLAGS) -I../libc/include -Iinclude" shared

user/%.elf: user/libc/libc.so.1 user/libuser/libuser.so.1
	$(MAKE) -C user USER_CC=$(USER_CC) USER_LD=$(USER_LD) \
	USER_CFLAGS="$(USER_CFLAGS) -Ilibc/include -Ilibuser/include" \
	USER_LDFLAGS="-m elf_i386 -nostdlib -dynamic-linker /lib/libc.so.1 -e _start -rpath=/lib --enable-new-dtags" $(@F)

initramfs.cpio: userspace user/init.elf user/fbsh.elf user/login.elf user/useradd.elf user/passwd.elf user/su.elf user/getent.elf user/mount.elf user/ls.elf user/echo.elf user/cat.elf user/touch.elf user/mkdir.elf user/write.elf user/kill.elf user/ln.elf user/ps.elf user/mkfat16.elf user/mkfat32.elf user/lsblk.elf user/partmk.elf user/crash.elf user/ldd.elf user/chmod.elf user/chown.elf user/stat.elf user/whoami.elf user/id.elf user/pwd.elf user/cp.elf user/mv.elf user/rm.elf user/true.elf user/false.elf user/sleep.elf user/uname.elf user/uptime.elf user/free.elf user/env.elf user/yes.elf user/head.elf user/wc.elf user/hd.elf user/which.elf user/clear.elf user/vplay.elf user/sbplay.elf user/fbfill.elf user/edit.elf user/dd.elf user/frostyde.elf user/frostyde_wm.elf user/frostywm.elf user/libc/libc.so.1 user/libuser/libuser.so.1
	rm -rf $(INITRAMFS_DIR) initramfs.cpio
	mkdir -p $(INITRAMFS_DIR)/bin $(INITRAMFS_DIR)/etc $(INITRAMFS_DIR)/dev $(INITRAMFS_DIR)/proc $(INITRAMFS_DIR)/mnt $(INITRAMFS_DIR)/tmp $(INITRAMFS_DIR)/usr/bin $(INITRAMFS_DIR)/lib
	cp user/init.elf $(INITRAMFS_DIR)/bin/init
	cp user/fbsh.elf $(INITRAMFS_DIR)/bin/sh
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
	cp user/mkfat32.elf $(INITRAMFS_DIR)/bin/mkfat32
	cp user/lsblk.elf $(INITRAMFS_DIR)/bin/lsblk
	cp user/partmk.elf $(INITRAMFS_DIR)/bin/partmk
	cp user/crash.elf $(INITRAMFS_DIR)/bin/crash
	cp user/ldd.elf $(INITRAMFS_DIR)/bin/ldd
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
	cp user/clear.elf $(INITRAMFS_DIR)/bin/clear
	cp user/vplay.elf $(INITRAMFS_DIR)/bin/vplay
	cp user/sbplay.elf $(INITRAMFS_DIR)/bin/sbplay
	cp user/fbfill.elf $(INITRAMFS_DIR)/bin/fbfill
	cp user/edit.elf $(INITRAMFS_DIR)/bin/edit
	cp user/dd.elf $(INITRAMFS_DIR)/bin/dd
	cp user/login.elf $(INITRAMFS_DIR)/bin/login
	cp user/useradd.elf $(INITRAMFS_DIR)/bin/useradd
	cp user/passwd.elf $(INITRAMFS_DIR)/bin/passwd
	cp user/su.elf $(INITRAMFS_DIR)/bin/su
	cp user/getent.elf $(INITRAMFS_DIR)/bin/getent
	cp user/libc/libc.so.1 $(INITRAMFS_DIR)/lib/libc.so.1
	cp user/libuser/libuser.so.1 $(INITRAMFS_DIR)/lib/libuser.so.1
	cp user/frostyde_wm.elf $(INITRAMFS_DIR)/bin/frostyde_wm
	cp user/frostywm.elf $(INITRAMFS_DIR)/bin/frostywm
	mkdir -p $(INITRAMFS_DIR)/tmp
	echo "Welcome to FrostByte (cpio initramfs)" > $(INITRAMFS_DIR)/etc/motd
	echo "root::0:0:root:/root:/bin/sh" > $(INITRAMFS_DIR)/etc/passwd
	echo "root::0:" > $(INITRAMFS_DIR)/etc/group
	mkdir -p $(INITRAMFS_DIR)/root $(INITRAMFS_DIR)/home
	ln -sf /etc/motd $(INITRAMFS_DIR)/motd_link
	ln -sf /bin/sh $(INITRAMFS_DIR)/usr/bin/sh
	cd $(INITRAMFS_DIR) && find . | cpio -o -H newc > ../initramfs.cpio

iso: $(KERNEL) initramfs.cpio
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/
	cp boot/grub/grub.cfg isodir/boot/grub/
	cp initramfs.cpio isodir/boot/initramfs.cpio
	grub2-mkrescue -o $(ISO_NAME) isodir

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

HOST_CC ?= gcc
HOST_CFLAGS ?= -O2 -Wall -Wextra
MENUCONFIG_BIN := tools/menuconfig/menuconfig

$(MENUCONFIG_BIN): tools/menuconfig/menuconfig.c
	@echo "  HOSTCC  $@"
	@$(HOST_CC) $(HOST_CFLAGS) -o $@ $< -lncurses

menuconfig: $(MENUCONFIG_BIN)
	@$(MENUCONFIG_BIN)

clean:
	rm -rf *.o $(KERNEL) $(ISO_NAME) isodir $(INITRAMFS_DIR) initramfs.cpio user/*.o user/*.elf $(MENUCONFIG_BIN) *.so.*
	$(MAKE) -C user/libc clean || true
	$(MAKE) -C user/libuser clean || true
	$(MAKE) -C user/coreutils clean || true
	$(MAKE) -C user/frostywm clean || true
	$(MAKE) -C user/desktop clean || true
	$(MAKE) -C user clean || true
