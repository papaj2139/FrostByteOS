#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

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
#define SYS_BRK     45   //set program break
#define SYS_SBRK    69   //adjust program break by increment
#define SYS_MOUNT   165
#define SYS_UMOUNT  166
#define SYS_UNLINK  10
#define SYS_MKDIR   39
#define SYS_RMDIR   40
#define SYS_READDIR_FD 167
#define SYS_MMAP    168
#define SYS_MUNMAP  169
#define SYS_TIME    170

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

#endif
