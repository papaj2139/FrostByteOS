#include <time.h>

#define SECONDS_PER_MINUTE 60
#define MINUTES_PER_HOUR   60
#define HOURS_PER_DAY      24
#define DAYS_PER_WEEK      7

static const int days_in_month_norm[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static const int days_in_month_leap[12] = {
    31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static int is_leap_year(int year) {
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static void epoch_to_tm(time_t epoch, struct tm* out) {
    time_t seconds = epoch;

    out->tm_sec = (int)(seconds % SECONDS_PER_MINUTE);
    seconds /= SECONDS_PER_MINUTE;

    out->tm_min = (int)(seconds % MINUTES_PER_HOUR);
    seconds /= MINUTES_PER_HOUR;

    out->tm_hour = (int)(seconds % HOURS_PER_DAY);
    time_t days = seconds / HOURS_PER_DAY;

    out->tm_wday = (int)((days + 4) % DAYS_PER_WEEK);

    int year = 1970;
    while (true) {
        int days_in_year = is_leap_year(year) ? 366 : 365;
        if (days >= days_in_year) {
            days -= days_in_year;
            year++;
        } else {
            break;
        }
    }

    out->tm_year = year - 1900;
    out->tm_yday = (int)days;

    const int* month_lengths = is_leap_year(year) ? days_in_month_leap : days_in_month_norm;
    int month = 0;
    while (month < 12 && days >= month_lengths[month]) {
        days -= month_lengths[month];
        month++;
    }
    if (month >= 12) {
        month = 11;
        days = month_lengths[11] - 1;
    }

    out->tm_mon = month;
    out->tm_mday = (int)days + 1;
    out->tm_isdst = 0;
}

struct tm* localtime(const time_t* timer) {
    static struct tm local_tm;
    if (!timer) {
        return NULL;
    }

    epoch_to_tm(*timer, &local_tm);
    return &local_tm;
}
