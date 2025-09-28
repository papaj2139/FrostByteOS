#include "uaccess.h"
#include "../mm/vmm.h"
#include "../libc/string.h"
#include "../process.h"

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
    //walk pages under the current process page directory to validate mappings
    page_directory_t saved = vmm_get_current_directory();
    process_t* cur = process_get_current();
    if (cur && cur->page_directory) {
        vmm_switch_directory(cur->page_directory);
    }
    int ok = 1;
    uint32_t a = s & ~0xFFFu;
    uint32_t end_page = e & ~0xFFFu;
    for (;;) {
        if (vmm_get_physical_addr(a) == 0) { ok = 0; break; }
        if (a == end_page) break;
        a += 0x1000;
    }
    if (ok) {
        if (vmm_get_physical_addr(s) == 0) ok = 0;
        if (vmm_get_physical_addr(e) == 0) ok = 0;
    }
    if (saved) vmm_switch_directory(saved);
    return ok;
}

int copy_from_user(void* dst, const void* user_src, size_t size) {
    if (size == 0) return 0;
    //validate under user directory and copy while it's active
    page_directory_t saved = vmm_get_current_directory();
    process_t* cur = process_get_current();
    if (cur && cur->page_directory) vmm_switch_directory(cur->page_directory);
    if (!user_range_ok(user_src, size, 0)) {
        if (saved) vmm_switch_directory(saved);
        return -1;
    }
    memcpy(dst, user_src, size);
    if (saved) vmm_switch_directory(saved);
    return 0;
}

int copy_to_user(void* user_dst, const void* src, size_t size) {
    if (size == 0) return 0;
    page_directory_t saved = vmm_get_current_directory();
    process_t* cur = process_get_current();
    if (cur && cur->page_directory) vmm_switch_directory(cur->page_directory);
    if (!user_range_ok(user_dst, size, 1)) {
        if (saved) vmm_switch_directory(saved);
        return -1;
    }
    memcpy(user_dst, src, size);
    if (saved) vmm_switch_directory(saved);
    return 0;
}

int copy_user_string(const char* user_src, char* dst, size_t dstsz) {
    if (!user_src || !dst || dstsz == 0) return -1;
    page_directory_t saved = vmm_get_current_directory();
    process_t* cur = process_get_current();
    if (cur && cur->page_directory) vmm_switch_directory(cur->page_directory);
    size_t i = 0;
    uint32_t base = (uint32_t)user_src;
    for (;;) {
        if (i + 1 >= dstsz) {
            dst[i] = '\0';
            if (saved) vmm_switch_directory(saved);
            return -1;
        }
        uint32_t addr = base + (uint32_t)i;
        if (vmm_get_physical_addr(addr) == 0) {
            dst[i] = '\0';
            if (saved) vmm_switch_directory(saved);
            return -1;
        }
        char c = *(const char*)addr;
        dst[i++] = c;
        if (c == '\0') break;
    }
    if (saved) vmm_switch_directory(saved);
    return 0;
}
