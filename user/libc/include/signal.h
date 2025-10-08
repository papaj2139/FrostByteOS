#ifndef LIBC_SIGNAL_H
#define LIBC_SIGNAL_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

//signal numbers
#define SIGINT   2
#define SIGKILL  9
#define SIGSEGV  11
#define SIGTERM  15

int raise(int sig);

#ifdef __cplusplus
}
#endif

#endif
