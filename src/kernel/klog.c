#include "klog.h"
#include "../mm/heap.h"
#include <string.h>

#ifndef KLOG_CAP
#define KLOG_CAP 8192
#endif

static char g_buf[KLOG_CAP];
static uint32_t g_head = 0;       //next write index
static uint32_t g_total = 0;      //total bytes written (monotonic)
static int g_inited = 0;

void klog_init(void) {
    g_head = 0;
    g_total = 0;
    g_inited = 1;
}

void klog_write(const char* s, size_t n) {
    if (!g_inited) klog_init();
    if (!s || n == 0) return;
    for (size_t i = 0; i < n; i++) {
        g_buf[g_head] = s[i];
        g_head = (g_head + 1) % KLOG_CAP;
        if (g_total < KLOG_CAP) {
            g_total++;
        } else {
            //once full we just overwrite oldest and keep total at cap
            g_total = KLOG_CAP;
        }
    }
}

uint32_t klog_size(void) {
    return g_total;
}

//copy log content in chronological order (oldest->newest) then apply offset
uint32_t klog_copy(uint32_t offset, char* dst, uint32_t size) {
    if (!dst || size == 0) return 0;
    if (!g_inited) klog_init();
    uint32_t available = g_total;
    if (offset >= available) return 0;
    uint32_t to_copy = size;
    if (offset + to_copy > available) to_copy = available - offset;

    //oldest data starts at (g_head - g_total + KLOG_CAP) % KLOG_CAP
    uint32_t start = (g_head + KLOG_CAP - g_total) % KLOG_CAP;
    //start position with offset applied
    uint32_t pos = (start + offset) % KLOG_CAP;
    uint32_t copied = 0;
    while (copied < to_copy) {
        uint32_t chunk = to_copy - copied;
        uint32_t till_end = (pos <= (uint32_t)(KLOG_CAP - 1)) ? (KLOG_CAP - pos) : 0;
        if (chunk > till_end) chunk = till_end;
        if (chunk == 0) break;
        memcpy(dst + copied, g_buf + pos, chunk);
        copied += chunk;
        pos = (pos + chunk) % KLOG_CAP;
    }
    return copied;
}
