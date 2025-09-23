#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

//syscall numbers
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

//syscall interrupt vector
#define SYSCALL_INT 0x80

//register structure for syscall context
typedef struct {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags, useresp, ss;
} syscall_regs_t;

//function declarations
void syscall_init(void);
int32_t syscall_handler(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);
void syscall_capture_user_frame(uint32_t eip, uint32_t cs, uint32_t eflags, uint32_t useresp, uint32_t ss, uint32_t ebp);

//syscall implementations
int32_t sys_exit(int32_t status);
int32_t sys_write(int32_t fd, const char* buf, uint32_t count);
int32_t sys_read(int32_t fd, char* buf, uint32_t count);
int32_t sys_open(const char* pathname, int32_t flags);
int32_t sys_close(int32_t fd);
int32_t sys_getpid(void);
int32_t sys_sleep(uint32_t seconds);
int32_t sys_creat(const char* pathname, int32_t mode);
int32_t sys_fork(void);
int32_t sys_execve(const char* pathname, char* const argv[], char* const envp[]);
int32_t sys_wait(int32_t* status);
int32_t sys_yield(void);
int32_t sys_ioctl(int32_t fd, uint32_t cmd, void* arg);
int32_t sys_brk(uint32_t new_end);
int32_t sys_sbrk(int32_t increment);
int32_t sys_mount(const char* device, const char* mount_point, const char* fs_type);
int32_t sys_umount(const char* mount_point);
int32_t sys_unlink(const char* path);
int32_t sys_mkdir(const char* path, int32_t mode);
int32_t sys_rmdir(const char* path);
int32_t sys_readdir_fd(int32_t fd, uint32_t index, char* name_buf, uint32_t buf_size, uint32_t* out_type);
int32_t sys_mmap(uint32_t addr, uint32_t length, uint32_t prot, uint32_t flags);
int32_t sys_munmap(uint32_t addr, uint32_t length);
int32_t sys_time(void);
int32_t sys_chdir(const char* path);
int32_t sys_getcwd(char* buf, uint32_t bufsize);
int32_t sys_clock_gettime(uint32_t clock_id, void* ts_out);
int32_t sys_gettimeofday(void* tv_out, void* tz_ignored);
int32_t sys_nanosleep(const void* req_ts, void* rem_ts);
int32_t sys_link(const char* oldpath, const char* newpath);
int32_t sys_kill(uint32_t pid, uint32_t sig);
int32_t sys_symlink(const char* target, const char* linkpath);
int32_t sys_readlink(const char* path, char* buf, uint32_t bufsiz);
int32_t sys_waitpid(int32_t pid, int32_t* status, int32_t options);

#endif
