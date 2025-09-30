#ifndef KERNEL_SIGNAL_H
#define KERNEL_SIGNAL_H

#include <stdint.h>
#include "../process.h"

//signal numbers
#define SIGINT   2
#define SIGILL   4
#define SIGBUS   7
#define SIGFPE   8
#define SIGSEGV  11
#define SIGKILL  9
#define SIGTERM  15
#define SIGCHLD  17

//raise a signal for a specific process
void signal_raise(process_t* p, int sig);

//check and act on pending signals for the given process (default actions only)
void signal_check(process_t* p);

//check current process
void signal_check_current(void);

#endif
