ISO_NAME = frostbyte.iso
KERNEL = kernel.elf
CC = i386-elf-gcc
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

dekstop.o: src/desktop.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): boot.o kernel.o dekstop.o
	$(CC) $(LDFLAGS) -o $@ $^

iso: $(KERNEL)
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/
	cp boot/grub/grub.cfg isodir/boot/grub/
	grub-mkrescue -o $(ISO_NAME) isodir

run: iso
	qemu-system-i386 -cdrom $(ISO_NAME)

clean:
	rm -rf *.o $(KERNEL) $(ISO_NAME) isodir
