#ifndef KERNEL_UACCESS_H
#define KERNEL_UACCESS_H

#include <stdint.h>
#include <stddef.h>

//returns 0 if the user range [ptr ptr+size) is within user VA space and each page is mapped
//if write is non-zero also requires the pages to be writable (rn not enforced future)
int user_range_ok(const void* ptr, size_t size, int write);

//safely copy from user to kernel returns 0 on success -1 on fault/invalid
int copy_from_user(void* dst, const void* user_src, size_t size);

//safely copy from kernel to user returns 0 on success -1 on fault/invalid
int copy_to_user(void* user_dst, const void* src, size_t size);

//copy a NUL-terminated string from user into dst buffer of size dstsz
//ensures NUL-termination returns 0 on success -1 on overflow/invalid
int copy_user_string(const char* user_src, char* dst, size_t dstsz);

#endif
