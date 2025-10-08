USER_CC ?= i686-elf-gcc
USER_LD ?= i686-elf-ld
USER_CFLAGS ?= -m32 -ffreestanding -Os -Wall -Wextra -fno-stack-protector -fno-omit-frame-pointer -nostdlib -I../libc/include -I../libuser/include -I.
USER_LDFLAGS ?= -m elf_i386 -nostdlib -dynamic-linker /lib/libc.so.1 -e _start -rpath=/lib --enable-new-dtags

CRT0 := ../libc/crt0.o
LIBC_SO := ../libc/libc.so.1

OBJS := libfwm.o frostywm.o
BINS := ../frostywm.elf

.PHONY: all clean

all: $(BINS)

libfwm.o: libfwm.c libfwm.h fwm_protocol.h
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

frostywm.o: frostywm.c fwm_protocol.h
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

../frostywm.elf: frostywm.o $(CRT0) $(LIBC_SO)
	$(USER_LD) $(USER_LDFLAGS) $(CRT0) frostywm.o -L ../libc -l:libc.so.1 -o $@

clean:
	rm -f $(OBJS) $(BINS)
