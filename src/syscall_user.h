#ifndef SYSCALL_USER_H
#define SYSCALL_USER_H

#include <stdint.h>


static inline int32_t syscall0(uint32_t syscall_num) {
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (syscall_num)
        : "memory"
    );
    return ret;
}

static inline int32_t syscall1(uint32_t syscall_num, uint32_t arg1) {
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (syscall_num), "b" (arg1)
        : "memory"
    );
    return ret;
}

static inline int32_t syscall2(uint32_t syscall_num, uint32_t arg1, uint32_t arg2) {
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (syscall_num), "b" (arg1), "c" (arg2)
        : "memory"
    );
    return ret;
}

static inline int32_t syscall3(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    int32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (syscall_num), "b" (arg1), "c" (arg2), "d" (arg3)
        : "memory"
    );
    return ret;
}

#define SYS_EXIT    1
#define SYS_WRITE   4
#define SYS_READ    3
#define SYS_GETPID  20
#define SYS_SLEEP   162

static inline void exit(int status) {
    syscall1(SYS_EXIT, (uint32_t)status);
}

static inline int32_t write(int fd, const char* buf, uint32_t count) {
    return syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, count);
}

static inline int32_t read(int fd, char* buf, uint32_t count) {
    return syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, count);
}

static inline int32_t getpid(void) {
    return syscall0(SYS_GETPID);
}

static inline int32_t sleep(uint32_t seconds) {
    return syscall1(SYS_SLEEP, seconds);
}

#endif
