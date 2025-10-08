CC ?= i686-elf-gcc
AS ?= nasm
AR ?= i686-elf-ar
CFLAGS ?= -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-omit-frame-pointer -nostdlib -Iinclude
PIC_CFLAGS ?= $(CFLAGS)
ASMFLAGS ?= -f elf32

CRT0_OBJ := crt0.o
LIBC_OBJS := src/syscalls.o src/string.o src/stdio.o src/errno.o src/signal.o src/stdlib.o src/time.o
LIBC_PIC_OBJS := $(LIBC_OBJS:%.o=%.pic.o)
LIBC_STATIC := libc.a

.PHONY: all clean install static shared

all: static shared

static: $(CRT0_OBJ) $(LIBC_OBJS) $(LIBC_STATIC)

shared: libc.so.1

crt0.o: crt0.asm
	$(AS) $(ASMFLAGS) $< -o $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/%.pic.o: src/%.c
	$(CC) $(PIC_CFLAGS) -c $< -o $@

libc.so.1: $(LIBC_PIC_OBJS)
	i686-elf-ld -shared -m elf_i386 -nostdlib -soname libc.so.1 $^ -o $@

$(LIBC_STATIC): $(LIBC_OBJS)
	$(AR) rcs $@ $^

install:
	@:

clean:
	rm -f $(CRT0_OBJ) $(LIBC_OBJS) $(LIBC_PIC_OBJS) libc.so.1 $(LIBC_STATIC)
	
