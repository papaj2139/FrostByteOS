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

io.o: src/io.c
	$(CC) $(CFLAGS) -c $< -o $@

font.o: src/font.c
	$(CC) $(CFLAGS) -c $< -o $@

keyboard.o: src/drivers/keyboard.c
	$(CC) $(CFLAGS) -c $< -o $@

mouse.o: src/drivers/mouse.c
	$(CC) $(CFLAGS) -c $< -o $@

serial.o: src/drivers/serial.c
	$(CC) $(CFLAGS) -c $< -o $@

pc_speaker.o: src/drivers/pc_speaker.c
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

timer.o: src/drivers/timer.c
	$(CC) $(CFLAGS) -c $< -o $@

rtc.o: src/drivers/rtc.c
	$(CC) $(CFLAGS) -c $< -o $@

ata.o: src/drivers/ata.c
	$(CC) $(CFLAGS) -c $< -o $@

syscall.o: src/syscall.c
	$(CC) $(CFLAGS) -c $< -o $@

syscall_asm.o: src/syscall_asm.asm
	$(ASM) $(ASMFLAGS) $< -o $@

gdt.o: src/interrupts/gdt.c
	$(CC) $(CFLAGS) -c $< -o $@

gdt_asm.o: src/interrupts/gdt_asm.asm
	$(ASM) $(ASMFLAGS) $< -o $@

tss.o: src/interrupts/tss.c
	$(CC) $(CFLAGS) -c $< -o $@

device_manager.o: src/device_manager.c
	$(CC) $(CFLAGS) -c $< -o $@

stdlib.o: src/libc/stdlib.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): boot.o kernel.o desktop.o string.o io.o font.o keyboard.o mouse.o serial.o pc_speaker.o vga.o idt.o irq.o pic.o isr.o isr_c.o timer.o rtc.o syscall.o syscall_asm.o gdt.o gdt_asm.o tss.o device_manager.o ata.o stdlib.o 
	$(CC) $(LDFLAGS) -o $@ $^

iso: $(KERNEL)
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/
	cp boot/grub/grub.cfg isodir/boot/grub/
	grub-mkrescue -o $(ISO_NAME) isodir

run: iso disk.img
	qemu-system-i386 -cdrom $(ISO_NAME) -hda disk.img

run-serial: iso disk.img
	qemu-system-i386 -cdrom $(ISO_NAME) -hda disk.img -serial stdio

run-sound: iso disk.img
	qemu-system-i386 -cdrom $(ISO_NAME) -hda disk.img -audiodev alsa,id=snd0 -machine pcspk-audiodev=snd0

disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=1

clean:
	rm -rf *.o $(KERNEL) $(ISO_NAME) isodir
