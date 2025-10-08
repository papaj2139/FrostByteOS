USER_CC ?= i686-elf-gcc
USER_LD ?= i686-elf-ld
USER_CFLAGS ?= -m32 -ffreestanding -Os -Wall -Wextra -fno-stack-protector -fno-omit-frame-pointer -nostdlib -I../user/libc/include -I../user/libuser/include
USER_LDFLAGS ?= -m elf_i386 -nostdlib -dynamic-linker /lib/libc.so.1 -e _start -rpath=/lib --enable-new-dtags

LIBC_DIR := ../user/libc
LIBUSER_DIR := ../user/libuser
CRT0 := $(LIBC_DIR)/crt0.o
LIBC_SO := $(LIBC_DIR)/libc.so.1
LIBUSER_SO := $(LIBUSER_DIR)/libuser.so.1

TESTS := test_memory test_process test_ipc test_vfs
RUNNER := test_runner

ALL_SOURCES := $(TESTS) $(RUNNER)
ALL_OBJS := $(ALL_SOURCES:%=%.o)
ALL_BINS := $(ALL_SOURCES:%=%.elf)

.PHONY: all clean

all: $(ALL_BINS)

%.o: %.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

%.elf: %.o $(CRT0) $(LIBC_SO) $(LIBUSER_SO)
	$(USER_LD) $(USER_LDFLAGS) $(CRT0) $< -L $(LIBC_DIR) -l:libc.so.1 -L $(LIBUSER_DIR) -l:libuser.so.1 -o $@

clean:
	rm -f $(ALL_OBJS) $(ALL_BINS)
