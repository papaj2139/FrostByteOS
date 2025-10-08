#ifndef _KERNEL_IO_H
#define _KERNEL_IO_H

#include <stdint.h>

//x86 I/O ports
void outb(uint16_t port, uint8_t val);
void outw(uint16_t port, uint16_t val);
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
void outl(uint16_t port, uint32_t val);
uint32_t inl(uint16_t port);

#endif
