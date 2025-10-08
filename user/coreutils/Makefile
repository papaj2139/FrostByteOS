USER_CC ?= i686-elf-gcc
USER_LD ?= i686-elf-ld
USER_CFLAGS ?= -m32 -ffreestanding -Os -Wall -Wextra -fno-stack-protector -fno-omit-frame-pointer -nostdlib -I../libc/include -I../libuser/include
USER_LDFLAGS ?= -m elf_i386 -nostdlib -dynamic-linker /lib/libc.so.1 -e _start -rpath=/lib --enable-new-dtags

OUT_DIR := ..
CRT0 := $(OUT_DIR)/libc/crt0.o
LIBC_SO := $(OUT_DIR)/libc/libc.so.1

PROGRAMS := cat chmod chown clear cp echo env false free hd head id ls mkdir mv pwd rm sleep stat touch true uname uptime wc which whoami yes

OUT_OBJS := $(PROGRAMS:%=$(OUT_DIR)/%.o)
OUT_BINS := $(PROGRAMS:%=$(OUT_DIR)/%.elf)

.PHONY: all clean

all: $(OUT_BINS)

$(OUT_DIR)/%.o: %.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(OUT_DIR)/%.elf: $(OUT_DIR)/%.o $(CRT0) $(LIBC_SO)
	$(USER_LD) $(USER_LDFLAGS) $(CRT0) $(OUT_DIR)/$*.o -L $(OUT_DIR)/libc -l:libc.so.1 -o $@

clean:
	rm -f $(OUT_OBJS) $(OUT_BINS)
