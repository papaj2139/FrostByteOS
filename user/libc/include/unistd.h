#ifndef LIBC_UNISTD_H
#define LIBC_UNISTD_H

#include <stddef.h>

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_ANON    0x1
#define MAP_FIXED   0x10

//clock IDs
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

//32-bit ABI time types
typedef struct { 
    int tv_sec; 
    int tv_nsec; 
} timespec_t;

typedef struct { 
    int tv_sec; 
    int tv_usec; 
} timeval_t;

#ifdef __cplusplus
extern "C" {
#endif

int write(int fd, const void* buf, size_t count);
int read(int fd, void* buf, size_t count);
int open(const char* path, int flags);
int close(int fd);
int getpid(void);
int sleep(unsigned int seconds);
int fork(void);
int execve(const char* path, char* const argv[], char* const envp[]);
int wait(int* status);
int yield(void);
int ioctl(int fd, unsigned int cmd, void* arg);
void* sbrk(intptr_t increment);
int brk(void* end);
void _exit(int status);
int mount(const char* device, const char* mount_point, const char* fs_type);
int umount(const char* mount_point);
int readdir_fd(int fd, unsigned index, char* name_buf, size_t buf_size, unsigned* out_type);
int unlink(const char* path);
int mkdir(const char* path, int mode);
int creat(const char* path, int mode);
int time(void);
void* mmap(void* addr, size_t length, int prot, int flags);
int munmap(void* addr, size_t length);
int chdir(const char* path);
char* getcwd(char* buf, size_t size);
int clock_gettime(int clk_id, void* ts_out);      
int gettimeofday(void* tv_out, void* tz_ignored); 
int nanosleep(const void* req_ts, void* rem_ts);  

#ifdef __cplusplus
}
#endif

#endif
