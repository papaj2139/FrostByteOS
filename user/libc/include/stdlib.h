#ifndef LIBC_STDLIB_H
#define LIBC_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

//memory allocation
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);

//string conversion
int atoi(const char* str);
long atol(const char* str);
long strtol(const char* str, char** endptr, int base);
unsigned long strtoul(const char* str, char** endptr, int base);

//process control
void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));

//pseudo-random numbers
int rand(void);
void srand(unsigned int seed);

//absolute value
int abs(int n);
long labs(long n);

#ifdef __cplusplus
}
#endif

#endif
