#ifndef LIBC_UNISTD_H
#define LIBC_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

//standard file descriptors
#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_ANON    0x1
#define MAP_FIXED   0x10

//clock IDs
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

//lseek whence values
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

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
int usleep(unsigned int usec);
int fork(void);
int execve(const char* path, char* const argv[], char* const envp[]);
int wait(int* status);
int waitpid(int pid, int* status, int options);
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
int rmdir(const char* path);
int creat(const char* path, int mode);
void* mmap(void* addr, size_t length, int prot, int flags);
void* mmap_ex(void* addr, size_t length, int prot, int flags, int fd, size_t offset);
int munmap(void* addr, size_t length);
int chdir(const char* path);
char* getcwd(char* buf, size_t size);
int clock_gettime(int clk_id, void* ts_out);
int gettimeofday(void* tv_out, void* tz_ignored);
int nanosleep(const void* req_ts, void* rem_ts);
int link(const char* oldpath, const char* newpath);
int kill(int pid, int sig);
int symlink(const char* target, const char* linkpath);
int readlink(const char* path, char* buf, size_t size);
int getuid(void);
int geteuid(void);
int getgid(void);
int getegid(void);
int setuid(int uid);
int setgid(int gid);
int seteuid(int euid);
int setegid(int egid);
int umask(int newmask);
int chown(const char* path, int uid, int gid);
int dlopen(const char* path, int flags);
void* dlsym(int handle, const char* name);
int dlclose(int handle);
int rename(const char* oldpath, const char* newpath);
int dup(int fd);
int dup2(int oldfd, int newfd);
int pipe(int pipefd[2]);
int lseek(int fd, int offset, int whence);
int fcntl(int fd, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif
