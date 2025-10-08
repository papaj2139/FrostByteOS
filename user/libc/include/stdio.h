#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

int putchar(int c);
int fputc(int fd, int c);

int puts(const char* s);
int fputs(int fd, const char* s);

char* fgets(int fd, char* buf, int size);

int printf(const char* fmt, ...);
int fprintf(int fd, const char* fmt, ...);
int dprintf(int fd, const char* fmt, ...);
int snprintf(char* buf, unsigned long size, const char* fmt, ...);
int vsnprintf(char* buf, unsigned long size, const char* fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#endif
