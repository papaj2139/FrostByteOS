#include <stddef.h>
#include <unistd.h>

//syscall numbers
#define SYS_EXIT    1
#define SYS_WRITE   4
#define SYS_READ    3
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_GETPID  20
#define SYS_SLEEP   162
#define SYS_CREAT   8
#define SYS_FORK    2
#define SYS_EXECVE  11
#define SYS_WAIT    7
#define SYS_YIELD   158
#define SYS_IOCTL   54
#define SYS_BRK     45
#define SYS_SBRK    69
#define SYS_MOUNT   165
#define SYS_UMOUNT  166
#define SYS_UNLINK  10
#define SYS_MKDIR   39
#define SYS_RMDIR   40
#define SYS_READDIR_FD 167
#define SYS_MMAP    168
#define SYS_MUNMAP  169
#define SYS_TIME    170

static inline int syscall3(int n, int a, int b, int c) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return ret;
}
static inline int syscall5(int n, int a, int b, int c, int d, int e) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory");
    return ret;
}   
static inline int syscall4(int n, int a, int b, int c, int d) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d) : "memory");
    return ret;
}
static inline int syscall2(int n, int a, int b) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b) : "memory");
    return ret;
}
static inline int syscall1(int n, int a) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a) : "memory");
    return ret;
}
static inline int syscall0(int n) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}

int write(int fd, const void* buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (int)buf, (int)count);
}

int read(int fd, void* buf, size_t count) {
    return syscall3(SYS_READ, fd, (int)buf, (int)count);
}

int open(const char* path, int flags) {
    return syscall2(SYS_OPEN, (int)path, flags);
}

int close(int fd) {
    return syscall1(SYS_CLOSE, fd);
}

int getpid(void) {
    return syscall0(SYS_GETPID);
}

int sleep(unsigned int seconds) {
    return syscall1(SYS_SLEEP, (int)seconds);
}

int fork(void) {
    return syscall0(SYS_FORK);
}

int execve(const char* path, char* const argv[], char* const envp[]) {
    return syscall3(SYS_EXECVE, (int)path, (int)argv, (int)envp);
}

int wait(int* status) {
    return syscall1(SYS_WAIT, (int)status);
}

int yield(void) {
    return syscall0(SYS_YIELD);
}

int ioctl(int fd, unsigned int cmd, void* arg) {
    return syscall3(SYS_IOCTL, fd, (int)cmd, (int)arg);
}

void* sbrk(intptr_t increment) {
    //returns old break on success (void*)-1 on error
    int old = syscall1(SYS_SBRK, (int)increment);
    if (old < 0) return (void*)-1;
    return (void*)(uintptr_t)old;
}

int brk(void* end) {
    return syscall1(SYS_BRK, (int)end);
}

void _exit(int status) {
    syscall1(SYS_EXIT, status);
    for(;;) { 
        __asm__ volatile ("hlt"); 
    }
}

int mount(const char* device, const char* mount_point, const char* fs_type) {
    return syscall3(SYS_MOUNT, (int)device, (int)mount_point, (int)fs_type);
}

int umount(const char* mount_point) {
    return syscall1(SYS_UMOUNT, (int)mount_point);
}

int readdir_fd(int fd, unsigned index, char* name_buf, size_t buf_size, unsigned* out_type) {
    return syscall5(SYS_READDIR_FD, fd, (int)index, (int)name_buf, (int)buf_size, (int)out_type);
}

int unlink(const char* path) {
    return syscall1(SYS_UNLINK, (int)path);
}

int mkdir(const char* path, int mode) {
    (void)mode;
    return syscall2(SYS_MKDIR, (int)path, 0);
}

int rmdir(const char* path) {
    return syscall1(SYS_RMDIR, (int)path);
}

int creat(const char* path, int mode) {
    (void)mode;
    return syscall2(SYS_CREAT, (int)path, 0);
}

void* mmap(void* addr, size_t length, int prot, int flags) {
    int r = syscall4(SYS_MMAP, (int)addr, (int)length, prot, flags);
    if (r < 0) return (void*)-1;
    return (void*)(uintptr_t)r;
}

int munmap(void* addr, size_t length) {
    return syscall2(SYS_MUNMAP, (int)addr, (int)length);
}

int time(void) {
    return syscall0(SYS_TIME);
}
