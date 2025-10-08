USER_CC ?= i686-elf-gcc
USER_LD ?= i686-elf-ld
USER_CFLAGS ?= -m32 -ffreestanding -Os -Wall -Wextra -fno-stack-protector -fno-omit-frame-pointer -nostdlib -Ilibc/include -Ilibuser/include
USER_LDFLAGS ?= -m elf_i386 -nostdlib -dynamic-linker /lib/libc.so.1 -e _start -rpath=/lib --enable-new-dtags
USER_LIBUSER_LDFLAGS ?= -L libuser -l:libuser.so.1

LIBC_DIR := libc
LIBUSER_DIR := libuser
CRT0 := $(LIBC_DIR)/crt0.o
LIBC_SO := $(LIBC_DIR)/libc.so.1
LIBC_A := $(LIBC_DIR)/libc.a
LIBUSER_SO := $(LIBUSER_DIR)/libuser.so.1
LIBUSER_A := $(LIBUSER_DIR)/libuser.a

COREUTILS_PROGRAMS := ls cat touch mkdir cp mv rm true false sleep uname uptime free env yes head wc hd which clear chmod chown stat whoami id pwd
PROGRAMS := crash dd edit fbfill fbsh getent kill ldd ln login lsblk mkfat16 mkfat32 mount partmk passwd ps sbplay su useradd vplay write
LIBUSER_PROGRAMS := fbsh getent login passwd su useradd

PROGRAM_OBJS := $(PROGRAMS:%=%.o)
PROGRAM_BINS := $(PROGRAMS:%=%.elf)
COREUTILS_BINS := $(COREUTILS_PROGRAMS:%=%.elf)

INIT_OBJ := init.o
INIT_BIN := init.elf

.PHONY: all clean

all: $(PROGRAM_BINS) $(COREUTILS_BINS) $(INIT_BIN)

%.o: %.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

$(INIT_OBJ): FrostyInit/init.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

define link_template
$1.elf: $1.o $(CRT0) $(LIBC_SO) $(if $(filter $1,$(LIBUSER_PROGRAMS)),$(LIBUSER_SO))
	$(USER_LD) $(USER_LDFLAGS) $(CRT0) $1.o -L $(LIBC_DIR) -l:libc.so.1 $(if $(filter $1,$(LIBUSER_PROGRAMS)),$(USER_LIBUSER_LDFLAGS)) -o $$@
endef
$(foreach prog,$(PROGRAMS),$(eval $(call link_template,$(prog))))

$(INIT_BIN): $(INIT_OBJ) $(CRT0) $(LIBC_A) $(LIBUSER_A)
	$(USER_LD) -m elf_i386 -nostdlib -T user.ld $(CRT0) $(LIBC_A) $(LIBUSER_A) $(INIT_OBJ) -o $@

$(COREUTILS_BINS): coreutils/.stamp

.PHONY: coreutils/.stamp
coreutils/.stamp:
	$(MAKE) -C coreutils USER_CC=$(USER_CC) USER_LD=$(USER_LD) \
	  USER_CFLAGS="$(USER_CFLAGS) -I../libc/include -I../libuser/include" \
	  USER_LDFLAGS="$(USER_LDFLAGS)" all
	touch $@

clean:
	rm -f $(PROGRAM_OBJS) $(PROGRAM_BINS) $(COREUTILS_BINS) $(INIT_OBJ) $(INIT_BIN)
