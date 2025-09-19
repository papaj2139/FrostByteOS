ISO_NAME = frostbyte.iso
KERNEL = kernel.elf
CC = i686-elf-gcc
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Isrc/libc -Isrc -fno-stack-protector
ASM = nasm
ASMFLAGS = -f elf32
LDFLAGS = -m32 -nostdlib -T linker.ld

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
	$(CC) $(CFLAGS) -c $< -o $@

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

initramfs.o: src/fs/initramfs.c src/fs/usershell_blob.h src/fs/init_blob.h src/fs/forktest_blob.h
	$(CC) $(CFLAGS) -c $< -o $@

usershell.bin: userapp.asm
	$(ASM) -f bin $< -o $@

#generate a header embedding the user shell binary for initramfs
src/fs/usershell_blob.h: usershell.bin
	xxd -i $< > $@
	sed -i 's/unsigned char usershell_bin/const unsigned char usershell_bin/g' $@
	sed -i 's/unsigned int usershell_bin_len/const unsigned int usershell_bin_len/g' $@

init.bin: init.asm
	$(ASM) -f bin $< -o $@

#generate a header embedding the init binary for initramfs
src/fs/init_blob.h: init.bin
	xxd -i $< > $@
	sed -i 's/unsigned char init_bin/const unsigned char init_bin/g' $@
	sed -i 's/unsigned int init_bin_len/const unsigned int init_bin_len/g' $@

forktest.bin: forktest.asm
	$(ASM) -f bin $< -o $@

#generate header embedding for initramfs
src/fs/forktest_blob.h: forktest.bin
	xxd -i $< > $@
	sed -i 's/unsigned char forktest_bin/const unsigned char forktest_bin/g' $@
	sed -i 's/unsigned int forktest_bin_len/const unsigned int forktest_bin_len/g' $@

fat16_vfs.o: src/fs/fat16_vfs.c
	$(CC) $(CFLAGS) -c $< -o $@

fd.o: src/fd.c
	$(CC) $(CFLAGS) -c $< -o $@

pmm.o: src/mm/pmm.c
	$(CC) $(CFLAGS) -c $< -o $@

vmm.o: src/mm/vmm.c
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
           vga.o idt.o irq.o pic.o isr.o isr_c.o gdt.o gdt_asm.o tss.o \
           syscall.o syscall_asm.o device_manager.o fat16.o fs.o vfs.o fat16_vfs.o fd.o initramfs.o \
           pmm.o vmm.o heap.o paging_asm.o process.o process_asm.o
	$(CC) $(LDFLAGS) -o $@ $^

iso: $(KERNEL)
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/
	cp boot/grub/grub.cfg isodir/boot/grub/
	grub-mkrescue -o $(ISO_NAME) isodir

run: iso disk.img
	qemu-system-i386 -cdrom $(ISO_NAME)

run-serial: iso disk.img
	qemu-system-i386 -cdrom $(ISO_NAME) -serial stdio -boot d

run-sound: iso disk.img
	qemu-system-i386 -cdrom $(ISO_NAME) -audiodev alsa,id=snd0 -machine pcspk-audiodev=snd0

disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=1

clean:
	rm -rf *.o $(KERNEL) $(ISO_NAME) isodir
