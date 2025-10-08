USER_CC ?= i686-elf-gcc
USER_LD ?= i686-elf-ld
USER_CFLAGS ?= -m32 -ffreestanding -Os -Wall -Wextra -fno-stack-protector -fno-omit-frame-pointer -nostdlib -I../libc/include -I../libuser/include

CRT0 := ../libc/crt0.o
LIBC_A := ../libc/libc.a
LIBUSER_A := ../libuser/libuser.a

.PHONY: all clean

all: ../init.elf

../init.o: init.c
	$(USER_CC) $(USER_CFLAGS) -c $< -o $@

../init.elf: ../init.o $(CRT0) $(LIBC_A) $(LIBUSER_A)
	$(USER_LD) -m elf_i386 -nostdlib -T ../../user.ld $(CRT0) ../init.o $(LIBC_A) $(LIBUSER_A) -o $@

clean:
	rm -f ../init.o ../init.elf
