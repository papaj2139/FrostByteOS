#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H

#include <sys/types.h>
#include <sys/time.h>

#define FD_SETSIZE 1024

typedef struct {
    unsigned long fds_bits[FD_SETSIZE / (8 * sizeof(unsigned long))];
} fd_set;

#define FD_ZERO(set) \
    do { \
        unsigned int __i; \
        for (__i = 0; __i < sizeof(fd_set) / sizeof(unsigned long); __i++) \
            ((fd_set *)(set))->fds_bits[__i] = 0; \
    } while (0)

#define FD_SET(fd, set) \
    ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] |= \
     (1UL << ((fd) % (8 * sizeof(unsigned long)))))

#define FD_CLR(fd, set) \
    ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] &= \
     ~(1UL << ((fd) % (8 * sizeof(unsigned long)))))

#define FD_ISSET(fd, set) \
    (((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] & \
      (1UL << ((fd) % (8 * sizeof(unsigned long))))) != 0)

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout);

#endif
