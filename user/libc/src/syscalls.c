#include <stddef.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

//forward decls for ctor/dtor runners
void __libc_run_ctors(void);
void __libc_run_dtors(void);
static volatile int __dtors_ran = 0;

#define SYS_EXIT           1000
#define SYS_WRITE          1001
#define SYS_READ           1002
#define SYS_OPEN           1003
#define SYS_CLOSE          1004
#define SYS_GETPID         1005
#define SYS_SLEEP          1006
#define SYS_CREAT          1007
#define SYS_FORK           1008
#define SYS_EXECVE         1009
#define SYS_WAIT           1010
#define SYS_YIELD          1011
#define SYS_IOCTL          1012
#define SYS_BRK            1013
#define SYS_SBRK           1014
#define SYS_MOUNT          1015
#define SYS_UMOUNT         1016
#define SYS_UNLINK         1017
#define SYS_MKDIR          1018
#define SYS_RMDIR          1019
#define SYS_READDIR_FD     1020
#define SYS_MMAP           1021
#define SYS_MUNMAP         1022
#define SYS_TIME           1023
#define SYS_CHDIR          1024
#define SYS_GETCWD         1025
#define SYS_CLOCK_GETTIME  1026
#define SYS_GETTIMEOFDAY   1027
#define SYS_NANOSLEEP      1028
#define SYS_LINK           1029
#define SYS_KILL           1030
#define SYS_SYMLINK        1031
#define SYS_READLINK       1032
#define SYS_WAITPID        1033
#define SYS_DL_GET_INIT    1034
#define SYS_DL_GET_FINI    1035
#define SYS_DLOPEN         1036
#define SYS_DLSYM          1037
#define SYS_DLCLOSE        1038
#define SYS_GETUID         1039
#define SYS_GETEUID        1040
#define SYS_GETGID         1041
#define SYS_GETEGID        1042
#define SYS_UMASK          1043
#define SYS_STAT           1044
#define SYS_LSTAT          1045
#define SYS_FSTAT          1046
#define SYS_CHMOD          1047
#define SYS_CHOWN          1048
#define SYS_FCHMOD         1049
#define SYS_FCHOWN         1050
#define SYS_MMAP_EX        1051

typedef struct {
    int tv_sec;
    int tv_nsec;
} timespec32_t;

 typedef struct {
    int tv_sec;
    int tv_usec;
} timeval32_t;

static inline int syscall3(int n, int a, int b, int c) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory", "cc");
    return ret;
}
static inline int syscall5(int n, int a, int b, int c, int d, int e) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory", "cc");
    return ret;
}
static inline int syscall4(int n, int a, int b, int c, int d) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d) : "memory", "cc");
    return ret;
}
static inline int syscall2(int n, int a, int b) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b) : "memory", "cc");
    return ret;
}
static inline int syscall1(int n, int a) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a) : "memory", "cc");
    return ret;
}
static inline int syscall0(int n) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n) : "memory", "cc");
    return ret;
}

static inline int __fixret(int r)
{
    if (r < 0) {
        //kernel currently returns -1 for most errors use EINVAL as default
        int e = -r;
        if (e <= 0 || e > 4096) e = EINVAL;
        errno = e;
        return -1;
    }
    return r;
}

int write(int fd, const void* buf, size_t count) {
    return __fixret(syscall3(SYS_WRITE, fd, (int)buf, (int)count));
}

int read(int fd, void* buf, size_t count) {
    return __fixret(syscall3(SYS_READ, fd, (int)buf, (int)count));
}

int open(const char* path, int flags) {
    return __fixret(syscall2(SYS_OPEN, (int)path, flags));
}

int close(int fd) {
    return __fixret(syscall1(SYS_CLOSE, fd));
}

int getpid(void) {
    return syscall0(SYS_GETPID);
}

int sleep(unsigned int seconds) {
    return __fixret(syscall1(SYS_SLEEP, (int)seconds));
}

int fork(void) {
    return syscall0(SYS_FORK);
}

int execve(const char* path, char* const argv[], char* const envp[]) {
    return __fixret(syscall3(SYS_EXECVE, (int)path, (int)argv, (int)envp));
}

int wait(int* status) {
    return __fixret(syscall1(SYS_WAIT, (int)status));
}

int waitpid(int pid, int* status, int options) {
    return __fixret(syscall3(SYS_WAITPID, pid, (int)status, options));
}

int yield(void) {
    return __fixret(syscall0(SYS_YIELD));
}

int ioctl(int fd, unsigned int cmd, void* arg) {
    return __fixret(syscall3(SYS_IOCTL, fd, (int)cmd, (int)arg));
}

void* sbrk(intptr_t increment) {
    //returns old break on success (void*)-1 on error
    int old = syscall1(SYS_SBRK, (int)increment);
    if (old < 0) return (void*)-1;
    return (void*)(uintptr_t)old;
}

int brk(void* end) {
    return __fixret(syscall1(SYS_BRK, (int)end));
}

void _exit(int status) {
    if (!__dtors_ran) { __libc_run_dtors(); }
    syscall1(SYS_EXIT, status);
    for(;;) {
        __asm__ volatile ("hlt");
    }
}

int mount(const char* device, const char* mount_point, const char* fs_type) {
    return __fixret(syscall3(SYS_MOUNT, (int)device, (int)mount_point, (int)fs_type));
}

int umount(const char* mount_point) {
    return __fixret(syscall1(SYS_UMOUNT, (int)mount_point));
}

int readdir_fd(int fd, unsigned index, char* name_buf, size_t buf_size, unsigned* out_type) {
    return __fixret(syscall5(SYS_READDIR_FD, fd, (int)index, (int)name_buf, (int)buf_size, (int)out_type));
}

int unlink(const char* path) {
    return __fixret(syscall1(SYS_UNLINK, (int)path));
}

int mkdir(const char* path, int mode) {
    (void)mode;
    return __fixret(syscall2(SYS_MKDIR, (int)path, 0));
}

int rmdir(const char* path) {
    return __fixret(syscall1(SYS_RMDIR, (int)path));
}

int creat(const char* path, int mode) {
    (void)mode;
    return __fixret(syscall2(SYS_CREAT, (int)path, 0));
}

void* mmap(void* addr, size_t length, int prot, int flags) {
    int r = syscall4(SYS_MMAP, (int)addr, (int)length, prot, flags);
    if (r < 0) return (void*)-1;
    return (void*)(uintptr_t)r;
}

void* mmap_ex(void* addr, size_t length, int prot, int flags, int fd, size_t offset) {
    struct mmap_args { int addr, length, prot, flags, fd, offset; } args;
    args.addr = (int)addr;
    args.length = (int)length;
    args.prot = prot;
    args.flags = flags;
    args.fd = fd;
    args.offset = (int)offset;
    int r = syscall1(SYS_MMAP_EX, (int)&args);
    if (r < 0) return (void*)-1;
    return (void*)(uintptr_t)r;
}

int munmap(void* addr, size_t length) {
    return __fixret(syscall2(SYS_MUNMAP, (int)addr, (int)length));
}

int time(void) {
    return __fixret(syscall0(SYS_TIME));
}

int chdir(const char* path) {
    return __fixret(syscall1(SYS_CHDIR, (int)path));
}

char* getcwd(char* buf, size_t size) {
    int r = syscall2(SYS_GETCWD, (int)buf, (int)size);
    if (r < 0) return (char*)0;
    return buf;
}

int dlopen(const char* path, int flags) {
    return syscall2(SYS_DLOPEN, (int)path, (int)flags);
}

void* dlsym(int handle, const char* name) {
    int r = syscall2(SYS_DLSYM, (int)handle, (int)name);
    if (r == 0) return (void*)0;
    return (void*)(unsigned)r;
}

int dlclose(int handle) {
    return syscall1(SYS_DLCLOSE, (int)handle);
}

//run all constructors reported by the kernel (RUNPATH/RPATH-resolved libraries)
void __libc_run_ctors(void) {
    for (unsigned i = 0;; i++) {
        int fn = syscall1(SYS_DL_GET_INIT, (int)i);
        if (fn == 0) break;
        void (*f)(void) = (void(*)(void))(unsigned)fn;
        f();
    }
}

//un all destructors in reverse order
void __libc_run_dtors(void) {
    if (__dtors_ran) return;
    for (unsigned i = 0;; i++) {
        int fn = syscall1(SYS_DL_GET_FINI, (int)i);
        if (fn == 0) break;
        void (*f)(void) = (void(*)(void))(unsigned)fn;
        f();
    }
    __dtors_ran = 1;
}

int clock_gettime(int clk_id, void* ts_out) {
    return __fixret(syscall2(SYS_CLOCK_GETTIME, clk_id, (int)ts_out));
}

int gettimeofday(void* tv_out, void* tz_ignored) {
    return __fixret(syscall2(SYS_GETTIMEOFDAY, (int)tv_out, (int)tz_ignored));
}

int nanosleep(const void* req_ts, void* rem_ts) {
    return __fixret(syscall2(SYS_NANOSLEEP, (int)req_ts, (int)rem_ts));
}

int link(const char* oldpath, const char* newpath) {
    return __fixret(syscall2(SYS_LINK, (int)oldpath, (int)newpath));
}

int kill(int pid, int sig) {
    return __fixret(syscall2(SYS_KILL, pid, sig));
}

int symlink(const char* target, const char* linkpath) {
    return __fixret(syscall2(SYS_SYMLINK, (int)target, (int)linkpath));
}

int readlink(const char* path, char* buf, size_t size) {
    return __fixret(syscall3(SYS_READLINK, (int)path, (int)buf, (int)size));
}

int getuid(void) {
    return syscall0(SYS_GETUID);
}

int geteuid(void) {
    return syscall0(SYS_GETEUID);
}

int getgid(void) {
    return syscall0(SYS_GETGID);
}

int getegid(void) {
    return syscall0(SYS_GETEGID);
}

int umask(int newmask) {
    return __fixret(syscall1(SYS_UMASK, newmask));
}

int stat(const char* path, struct stat* st) {
    return __fixret(syscall2(SYS_STAT, (int)path, (int)st));
}

int lstat(const char* path, struct stat* st) {
    return __fixret(syscall2(SYS_LSTAT, (int)path, (int)st));
}

int fstat(int fd, struct stat* st) {
    return __fixret(syscall2(SYS_FSTAT, fd, (int)st));
}

int chmod(const char* path, int mode) {
    return __fixret(syscall2(SYS_CHMOD, (int)path, mode));
}

int chown(const char* path, int uid, int gid) {
    return __fixret(syscall3(SYS_CHOWN, (int)path, uid, gid));
}

int fchmod(int fd, int mode) {
    return __fixret(syscall2(SYS_FCHMOD, fd, mode));
}

int fchown(int fd, int uid, int gid) {
    return __fixret(syscall3(SYS_FCHOWN, fd, uid, gid));
}
