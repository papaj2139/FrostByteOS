#include "uaccess.h"
#include "../mm/vmm.h"
#include "../libc/string.h"

static inline int in_user_range(uint32_t start, uint32_t end_inclusive) {
    if (start < USER_VIRTUAL_START) return 0;
    if (end_inclusive < start) return 0; //overflow or empty
    if (end_inclusive > USER_VIRTUAL_END) return 0;
    return 1;
}

int user_range_ok(const void* ptr, size_t size, int write) {
    (void)write; //write-check not enforced yet
    if (size == 0) return 1;
    uint32_t s = (uint32_t)ptr;
    uint32_t e = s + (uint32_t)(size - 1);
    if (!in_user_range(s, e)) return 0;
    //walk pages
    uint32_t a = s & ~0xFFFu;
    uint32_t end_page = e & ~0xFFFu;
    for (;;) {
        if (vmm_get_physical_addr(a) == 0) return 0;
        if (a == end_page) break;
        a += 0x1000;
    }
    //check first and last addresses specifically if unaligned
    if (vmm_get_physical_addr(s) == 0) return 0;
    if (vmm_get_physical_addr(e) == 0) return 0;
    return 1;
}

int copy_from_user(void* dst, const void* user_src, size_t size) {
    if (size == 0) return 0;
    if (!user_range_ok(user_src, size, 0)) return -1;
    memcpy(dst, user_src, size);
    return 0;
}

int copy_to_user(void* user_dst, const void* src, size_t size) {
    if (size == 0) return 0;
    if (!user_range_ok(user_dst, size, 1)) return -1;
    memcpy(user_dst, src, size);
    return 0;
}

int copy_user_string(const char* user_src, char* dst, size_t dstsz) {
    if (!user_src || !dst || dstsz == 0) return -1;
    size_t i = 0;
    uint32_t base = (uint32_t)user_src;
    for (;;) {
        if (i + 1 >= dstsz) { dst[i] = '\0'; return -1; }
        uint32_t addr = base + (uint32_t)i;
        if (vmm_get_physical_addr(addr) == 0) { dst[i] = '\0'; return -1; }
        char c = *(const char*)addr;
        dst[i++] = c;
        if (c == '\0') break;
    }
    return 0;
}
