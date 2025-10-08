#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef struct {
    unsigned year;   //full year
    unsigned month;  //1-12
    unsigned day;    //1-31
    unsigned hour;   //0-23
    unsigned minute; //0-59
    unsigned second; //0-59
} rtc_time_t;

//initialize RTC (currently no-op reserved for future use... if there will be any)
static inline void rtc_init(void) {}

//read current time from CMOS RTC returns 1 on success, 0 on failure
int rtc_read(rtc_time_t *out);

//register RTC as a device with device_manager
int rtc_register_device(void);

#endif
