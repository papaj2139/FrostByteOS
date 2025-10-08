#ifndef BOOTLOG_H
#define BOOTLOG_H

#include <stddef.h>
#include <stdarg.h>
#include "cga.h"
#include "../libc/string.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_boot_console_enabled; //1 during early boot 0 after init handoff
extern int g_console_quiet;        //set to 1 when 'quiet' is in cmdline

static inline void bootlog_print(const char* s) {
    if (!s) return;
    if (!g_boot_console_enabled) return;
    if (g_console_quiet) return;
    print((char*)s, 0x0F);
}

#define bootlog_printf(fmt, ...) do { \
    if (g_boot_console_enabled && !g_console_quiet) { \
        char __bootlog_buf[1024]; \
        ksnprintf(__bootlog_buf, sizeof(__bootlog_buf), fmt, ##__VA_ARGS__); \
        print(__bootlog_buf, 0x0F); \
    } \
} while (0)

#ifdef __cplusplus
}
#endif

#endif
