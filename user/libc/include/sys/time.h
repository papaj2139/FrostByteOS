#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include <sys/types.h>

typedef long suseconds_t;

struct timeval {
    time_t      tv_sec;     //seconds 
    suseconds_t tv_usec;    //microseconds 
};

struct timezone {
    int tz_minuteswest;     //minutes west of Greenwich
    int tz_dsttime;         //type of DST correction
};


int settimeofday(const struct timeval *tv, const struct timezone *tz);

#endif
