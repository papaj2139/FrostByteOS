#ifndef LIBC_TIME_H
#define LIBC_TIME_H

#include <sys/types.h>
#include <stddef.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct timespec {
    time_t tv_sec;
    long   tv_nsec;
} timespec;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

int clock_gettime(int clk_id, void* ts_out);
int gettimeofday(void* tv_out, void* tz_ignored);
int nanosleep(const void* req_ts, void* rem_ts);

time_t time(time_t* tloc);
struct tm* localtime(const time_t* timer);

#ifdef __cplusplus
}
#endif

#endif
