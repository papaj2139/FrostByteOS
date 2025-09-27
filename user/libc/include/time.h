#ifndef LIBC_TIME_H
#define LIBC_TIME_H

#include <sys/types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct timespec {
    time_t tv_sec;
    long   tv_nsec;
} timespec;

typedef struct timeval {
    time_t tv_sec;
    long   tv_usec;
} timeval;

int clock_gettime(int clk_id, void* ts_out);
int gettimeofday(void* tv_out, void* tz_ignored);
int nanosleep(const void* req_ts, void* rem_ts);

time_t time(void);

#ifdef __cplusplus
}
#endif

#endif
