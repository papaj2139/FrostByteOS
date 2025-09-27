#ifndef LIBC_SYS_TYPES_H
#define LIBC_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int pid_t;
typedef int uid_t;
typedef int gid_t;
typedef unsigned int mode_t;
typedef int dev_t;
typedef unsigned int ino_t;
typedef unsigned int nlink_t;
typedef int off_t; //32-bit offset for now

typedef unsigned int time_t; //seconds since epoch

typedef unsigned int clock_t;

typedef int clockid_t;

#ifdef __cplusplus
}
#endif

#endif
