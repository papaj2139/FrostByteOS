#ifndef _KERNEL_STRING_H
#define _KERNEL_STRING_H

#include <stddef.h>
#include <stdarg.h>

int memcmp(const void *s1, const void *s2, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int strcmp(const char *a, const char *b);
unsigned int strlen(const char *str);
void strncpy(char* dest, const char* src, unsigned int n);
void itoa_unsigned(unsigned int value, char *buf, int base);
int ksnprintf(char *out, size_t size, const char *fmt, ...);

#endif
