#ifndef LIBC_SYS_WAIT_H
#define LIBC_SYS_WAIT_H

#ifdef __cplusplus
extern "C" {
#endif

// Minimal waitpid options
#define WNOHANG 1

// FrostByteOS returns a raw status integer from the kernel:
// - Normal exit: status == exit_code (typically 0..127)
// - Signal death: status == 128 + signal
// These helpers match that convention.
#define WIFEXITED(s)     ((s) >= 0 && (s) < 128)
#define WEXITSTATUS(s)   ((s) & 0x7F)
#define WIFSIGNALED(s)   ((s) >= 128)
#define WTERMSIG(s)      ((s) - 128)

int waitpid(int pid, int* status, int options);

#ifdef __cplusplus
}
#endif

#endif
