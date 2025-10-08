CC ?= i686-elf-gcc
AR ?= i686-elf-ar
LD ?= i686-elf-ld
CFLAGS ?= -m32 -ffreestanding -Os -Wall -Wextra -fno-stack-protector -fno-omit-frame-pointer -nostdlib -Iinclude
PIC_CFLAGS ?= $(CFLAGS)

LIBUSER_OBJS := src/passwd.o
LIBUSER_PIC_OBJS := $(LIBUSER_OBJS:%.o=%.pic.o)

.PHONY: all clean install static shared

all: static shared

static: libuser.a

shared: libuser.so.1

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/%.pic.o: src/%.c
	$(CC) $(PIC_CFLAGS) -c $< -o $@

libuser.a: $(LIBUSER_OBJS)
	$(AR) rcs $@ $^

libuser.so.1: $(LIBUSER_PIC_OBJS)
	$(LD) -shared -m elf_i386 -nostdlib -soname libuser.so.1 $^ -o $@

install:
	@:

clean:
	rm -f $(LIBUSER_OBJS) $(LIBUSER_PIC_OBJS) libuser.a libuser.so.1
