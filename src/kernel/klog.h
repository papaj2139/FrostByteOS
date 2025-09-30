#ifndef KLOG_H
#define KLOG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void klog_init(void);
void klog_write(const char* s, size_t n);
//return current linearized size of the log (up to ring capacity)
uint32_t klog_size(void);
//copy from the beginning (oldest) + offset up to size bytes into dst
//returns bytes copied
uint32_t klog_copy(uint32_t offset, char* dst, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
