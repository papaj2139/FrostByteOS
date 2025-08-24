ISO_NAME = frostbyte.iso
KERNEL = kernel.elf
CC = i686-elf-gcc
CFLAGS = -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude -fno-stack-protector
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

string.o: src/string.c
	$(CC) $(CFLAGS) -c $< -o $@

io.o: src/io.c
	$(CC) $(CFLAGS) -c $< -o $@

font.o: src/font.c
	$(CC) $(CFLAGS) -c $< -o $@

keyboard.o: src/drivers/keyboard.c
	$(CC) $(CFLAGS) -c $< -o $@

serial.o: src/drivers/serial.c
	$(CC) $(CFLAGS) -c $< -o $@\

pc_speaker.o: src/drivers/pc_speaker.c
	$(CC) $(CFLAGS) -c $< -o $@

vga.o: src/gui/vga.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): boot.o kernel.o desktop.o string.o io.o font.o keyboard.o serial.o pc_speaker.o vga.o
	$(CC) $(LDFLAGS) -o $@ $^

iso: $(KERNEL)
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/
	cp boot/grub/grub.cfg isodir/boot/grub/
	grub-mkrescue -o $(ISO_NAME) isodir

run: iso
	qemu-system-i386 -cdrom $(ISO_NAME)

run-serial: iso
	qemu-system-i386 -cdrom $(ISO_NAME) -serial stdio

run-sound: iso
	qemu-system-i386 -cdrom $(ISO_NAME) -audiodev alsa,id=snd0 -machine pcspk-audiodev=snd0

clean:
	rm -rf *.o $(KERNEL) $(ISO_NAME) isodir
