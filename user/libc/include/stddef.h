#ifndef LIBC_STDDEF_H
#define LIBC_STDDEF_H

typedef unsigned int size_t;
typedef int ssize_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

#endif
