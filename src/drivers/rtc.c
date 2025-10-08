#include "rtc.h"
#include "../io.h"
#include "../device_manager.h"
#include <stdint.h>
#include <string.h>

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

static inline uint8_t cmos_read(uint8_t reg) {
    //preserve NMI bit (bit 7) as enabled we write reg with bit7=0
    outb(CMOS_INDEX, reg & 0x7F);
    return inb(CMOS_DATA);
}

//device manager integration for /dev/rtc0
static int rtc_dev_init(struct device* d) {
    (void)d; 
    return 0; //no HW init needed
}
static int rtc_dev_read(struct device* d, uint32_t off, void* buf, uint32_t sz) {
    (void)d;
    (void)off;
    if (!buf || sz == 0) return 0;
    rtc_time_t t;
    if (!rtc_read(&t)) return -1;
    //format: YYYY-MM-DD HH:MM:SS\n
    char line[32];
    int p = 0;
    //naive itoa-like formatting
    //year
    unsigned y = t.year; line[p++] = (char)('0' + (y/1000)%10); line[p++] = (char)('0' + (y/100)%10); line[p++] = (char)('0' + (y/10)%10); line[p++] = (char)('0' + (y%10));
    line[p++] = '-';
    line[p++] = (char)('0' + (t.month/10)%10); line[p++] = (char)('0' + (t.month%10));
    line[p++] = '-';
    line[p++] = (char)('0' + (t.day/10)%10); line[p++] = (char)('0' + (t.day%10));
    line[p++] = ' ';
    line[p++] = (char)('0' + (t.hour/10)%10); line[p++] = (char)('0' + (t.hour%10));
    line[p++] = ':';
    line[p++] = (char)('0' + (t.minute/10)%10); line[p++] = (char)('0' + (t.minute%10));
    line[p++] = ':';
    line[p++] = (char)('0' + (t.second/10)%10); line[p++] = (char)('0' + (t.second%10));
    line[p++] = '\n';
    int len = p;
    if ((uint32_t)len > sz) len = (int)sz;
    memcpy(buf, line, (size_t)len);
    return len;
}
static int rtc_dev_write(struct device* d, uint32_t off, const void* buf, uint32_t sz) { 
    (void)d; 
    (void)off; 
    (void)buf; 
    (void)sz;
    return -1; 
}

static int rtc_dev_ioctl(struct device* d, uint32_t cmd, void* arg) { 
    (void)d; 
    (void)cmd; 
    (void)arg; 
    return -1; 
}    

static void rtc_dev_cleanup(struct device* d) { (void)d; }

static const device_ops_t rtc_ops = {
    .init = rtc_dev_init,
    .read = rtc_dev_read,
    .write = rtc_dev_write,
    .ioctl = rtc_dev_ioctl,
    .cleanup = rtc_dev_cleanup,
};

static device_t g_rtc_dev;

int rtc_register_device(void) {
    memset(&g_rtc_dev, 0, sizeof(g_rtc_dev));
    strcpy(g_rtc_dev.name, "rtc0");
    g_rtc_dev.type = DEVICE_TYPE_TIMER;
    g_rtc_dev.subtype = DEVICE_SUBTYPE_GENERIC;
    g_rtc_dev.status = DEVICE_STATUS_UNINITIALIZED;
    g_rtc_dev.ops = &rtc_ops;
    if (device_register(&g_rtc_dev) != 0) return -1;
    if (device_init(&g_rtc_dev) != 0) { 
        device_unregister(g_rtc_dev.device_id); 
        return -1; 
    }
    g_rtc_dev.status = DEVICE_STATUS_READY;
    return 0;
}

static inline int rtc_updating(void) {
    //bit 7 of Register A is UIP (update in progress)
    uint8_t ra;
    outb(CMOS_INDEX, 0x0A);
    ra = inb(CMOS_DATA);
    return (ra & 0x80) != 0;
}

static inline uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t)((v & 0x0F) + ((v >> 4) * 10));
}

int rtc_read(rtc_time_t *out) {
    if (!out) return 0;

    //read until we get two identical samples with UIP clear
    uint8_t sec, min, hour, day, mon, year, cent = 0;
    uint8_t sec2, min2, hour2, day2, mon2, year2, cent2 = 0;

    for (int tries = 0; tries < 10; ++tries) {
        //wait for UIP to clear
        while (rtc_updating()) { }

        sec  = cmos_read(0x00);
        min  = cmos_read(0x02);
        hour = cmos_read(0x04);
        day  = cmos_read(0x07);
        mon  = cmos_read(0x08);
        year = cmos_read(0x09);
        cent = cmos_read(0x32); //may not be implemented

        //recheck no update in progress and read again
        if (rtc_updating()) continue;

        sec2  = cmos_read(0x00);
        min2  = cmos_read(0x02);
        hour2 = cmos_read(0x04);
        day2  = cmos_read(0x07);
        mon2  = cmos_read(0x08);
        year2 = cmos_read(0x09);
        cent2 = cmos_read(0x32);

        if (sec == sec2 && min == min2 && hour == hour2 && day == day2 && mon == mon2 && year == year2 && cent == cent2) {
            //consistent sample
            //determine format from Register B
            outb(CMOS_INDEX, 0x0B);
            uint8_t rb = inb(CMOS_DATA);
            int is_24h = (rb & 0x02) != 0; //1 = 24-hour
            int is_bin = (rb & 0x04) != 0; //1 = binary 0 = BCD

            if (!is_bin) {
                sec  = bcd_to_bin(sec);
                min  = bcd_to_bin(min);
                hour = bcd_to_bin(hour & 0x7F);
                day  = bcd_to_bin(day);
                mon  = bcd_to_bin(mon);
                year = bcd_to_bin(year);
                if (cent != 0xFF && cent != 0x00) cent = bcd_to_bin(cent);
            } else {
                hour = hour & 0x7F;
            }

            if (!is_24h) {
                //12-hour format bit 7 of hour is PM (in BCD read was masked) but we masked already
                //common convention if original hour had bit7 set it is PM
                uint8_t raw_hour = cmos_read(0x04);
                int pm = (raw_hour & 0x80) != 0;
                if (pm && hour < 12) hour = (uint8_t)(hour + 12);
                if (!pm && hour == 12) hour = 0; //12 AM = 00
            }

            unsigned full_year;
            if (cent == 0xFF || cent == 0x00) {
                //no century register assume 2000-2099 range
                full_year = 2000u + (unsigned)year;
                if (year < 70) {
                
                }
            } else {
                full_year = (unsigned)cent * 100u + (unsigned)year;
            }

            out->year = full_year;
            out->month = mon;
            out->day = day;
            out->hour = hour;
            out->minute = min;
            out->second = sec;
            return 1;
        }
    }
    return 0;
}
