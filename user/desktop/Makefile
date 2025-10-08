USER_CC ?= i686-elf-gcc
USER_LD ?= i686-elf-ld
USER_CFLAGS ?= -m32 -ffreestanding -Os -Wall -Wextra -fno-stack-protector -fno-omit-frame-pointer -nostdlib -I../libc/include -I../libuser/include -I../frostywm
USER_LDFLAGS ?= -m elf_i386 -nostdlib -dynamic-linker /lib/libc.so.1 -e _start -rpath=/lib --enable-new-dtags

CRT0 := ../libc/crt0.o
LIBC_SO := ../libc/libc.so.1

PROGRAMS := frostyde frostyde_wm
OUT_OBJS := $(PROGRAMS:%=%.o)
OUT_BINS := $(PROGRAMS:%=../%.elf)

.PHONY: all clean

all: $(OUT_BINS)

%.o: %.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

../frostyde.elf: frostyde.o $(CRT0) $(LIBC_SO)
	$(USER_LD) $(USER_LDFLAGS) $(CRT0) frostyde.o -L ../libc -l:libc.so.1 -o $@

../frostyde_wm.elf: frostyde_wm.o ../frostywm/libfwm.o $(CRT0) $(LIBC_SO)
	$(USER_LD) $(USER_LDFLAGS) $(CRT0) frostyde_wm.o ../frostywm/libfwm.o -L ../libc -l:libc.so.1 -o $@

clean:
	rm -f $(OUT_OBJS) $(OUT_BINS)
