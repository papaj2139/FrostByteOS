#ifndef LIBC_FCNTL_H
#define LIBC_FCNTL_H

#ifdef __cplusplus
extern "C" {
#endif

//open(2) flags
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

//place-holders for common flags (not yet supported by kernel)
#define O_CREAT  0100
#define O_TRUNC  01000
#define O_APPEND 02000
#define O_NONBLOCK 04000

//fcntl commands
#define F_GETFL 3
#define F_SETFL 4

#ifdef __cplusplus
}
#endif

#endif
