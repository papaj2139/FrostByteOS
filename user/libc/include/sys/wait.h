#ifndef LIBC_SYS_WAIT_H
#define LIBC_SYS_WAIT_H

#ifdef __cplusplus
extern "C" {
#endif

#define WNOHANG 1

#define WIFEXITED(s)     ((s) >= 0 && (s) < 128)
#define WEXITSTATUS(s)   ((s) & 0x7F)
#define WIFSIGNALED(s)   ((s) >= 128)
#define WTERMSIG(s)      ((s) - 128)

int waitpid(int pid, int* status, int options);

#ifdef __cplusplus
}
#endif

#endif
