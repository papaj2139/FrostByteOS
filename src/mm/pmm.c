#include "pmm.h"
#include "../drivers/serial.h"
#include "../kernel/multiboot.h"
#include <string.h>

#define MAX_MEMORY_REGIONS 32
#define BITMAP_SIZE (128 * 1024)  //support up to 512mb (128k pages * 4kb each)

static uint8_t page_bitmap[BITMAP_SIZE];
static uint32_t total_pages = 0;
static uint32_t used_pages = 0;

//bitmap operations
static inline void set_bit(uint32_t bit) {
    page_bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void clear_bit(uint32_t bit) {
    page_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline int test_bit(uint32_t bit) {
    return (page_bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

static int range_overlaps(uint32_t a_start, uint32_t a_end, uint32_t b_start, uint32_t b_end) {
    return !(a_end <= b_start || b_end <= a_start);
}

void pmm_init_multiboot(const struct multiboot_info* mbi,
                        uint32_t kernel_start_phys,
                        uint32_t kernel_end_phys) {
    DEBUG_PRINT("PMM: Initializing from Multiboot memory map");

    //determine total memory from highest address in mmap
    uint64_t max_end = 0;
    if (mbi && (mbi->flags & MBI_FLAG_MMAP)) {
        uint32_t mmap_cur = mbi->mmap_addr;
        uint32_t mmap_end = mbi->mmap_addr + mbi->mmap_length;
        while (mmap_cur < mmap_end) {
            multiboot_mmap_entry_t* e = (multiboot_mmap_entry_t*)mmap_cur;
            uint64_t end = e->addr + e->len;
            if (end > max_end) max_end = end;
            mmap_cur += e->size + sizeof(e->size);
        }
    } else if (mbi && (mbi->flags & MBI_FLAG_MEM)) {
        uint64_t total = ((uint64_t)mbi->mem_lower + (uint64_t)mbi->mem_upper) * 1024ull;
        if (total > max_end) max_end = total;
    } else {
        max_end = (uint64_t)BITMAP_SIZE * 8ull * PAGE_SIZE; //fallback to max PMM capacity
    }

    uint64_t max_supported_end = (uint64_t)BITMAP_SIZE * 8ull * (uint64_t)PAGE_SIZE;
    if (max_end > max_supported_end) max_end = max_supported_end;

    total_pages = (uint32_t)(max_end / PAGE_SIZE);
    if (total_pages > BITMAP_SIZE * 8) total_pages = BITMAP_SIZE * 8;

    //initialize bitmap to all used
    memset(page_bitmap, 0xFF, sizeof(page_bitmap));
    used_pages = total_pages;

    //build reserved ranges list: low 1MB kernel and modules
    typedef struct { uint32_t start, end; } range_t;
    range_t reserved[32];
    int rcount = 0;
    //low 1MB guard
    reserved[rcount++] = (range_t){0x00000000u, 0x00100000u};
    //kernel image
    reserved[rcount++] = (range_t){kernel_start_phys & ~0xFFFu, (kernel_end_phys + PAGE_SIZE - 1) & ~0xFFFu};

    if (mbi && (mbi->flags & MBI_FLAG_MODS) && mbi->mods_count && mbi->mods_addr) {
        uint32_t count = mbi->mods_count;
        uint32_t addr = mbi->mods_addr;
        for (uint32_t i = 0; i < count && rcount < (int)(sizeof(reserved)/sizeof(reserved[0])); i++) {
            //multiboot module structure
            typedef struct { uint32_t mod_start, mod_end, string, reserved; } mod_t;
            mod_t* m = (mod_t*)(addr + i * sizeof(mod_t));
            uint32_t ms = m->mod_start & ~0xFFFu;
            uint32_t me = (m->mod_end + PAGE_SIZE - 1) & ~0xFFFu;
            reserved[rcount++] = (range_t){ms, me};
        }
    }

    //free pages in available regions except reserved overlaps
    if (mbi && (mbi->flags & MBI_FLAG_MMAP)) {
        uint32_t mmap_cur = mbi->mmap_addr;
        uint32_t mmap_end = mbi->mmap_addr + mbi->mmap_length;
        while (mmap_cur < mmap_end) {
            multiboot_mmap_entry_t* e = (multiboot_mmap_entry_t*)mmap_cur;
            if (e->type == MULTIBOOT_MEMORY_AVAILABLE && e->len > 0) {
                uint64_t rstart64 = e->addr;
                uint64_t rend64 = e->addr + e->len;
                if (rend64 > max_end) rend64 = max_end;
                //align to page boundaries and clamp to 32-bit addressable range
                uint32_t rstart = (uint32_t)((rstart64 + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1));
                uint32_t rend   = (uint32_t)((rend64) & ~(uint64_t)(PAGE_SIZE - 1));
                for (uint32_t addr = rstart; addr < rend; addr += PAGE_SIZE) {
                    //check reserved overlap
                    int ov = 0;
                    for (int i = 0; i < rcount; i++) {
                        if (range_overlaps(addr, addr + PAGE_SIZE, reserved[i].start, reserved[i].end)) { ov = 1; break; }
                    }
                    if (ov) continue;
                    uint32_t page = addr / PAGE_SIZE;
                    if (page < total_pages && page < BITMAP_SIZE * 8) {
                        if (test_bit(page)) { clear_bit(page); used_pages--; }
                    }
                }
            }
            mmap_cur += e->size + sizeof(e->size);
        }
    }

    DEBUG_PRINTF("PMM: Total pages: %d, free: %d, used: %d", (int)total_pages, (int)(total_pages - used_pages), (int)used_pages);
}

void pmm_init(uint32_t mem_low, uint32_t mem_high) {
    DEBUG_PRINTF("PMM: Initializing physical memory manager");
    DEBUG_PRINTF("PMM: Low memory: %d KB, High memory: %d KB", (int)mem_low, (int)mem_high);

    //calculate total memory in bytes
    uint32_t total_memory = (mem_low + mem_high) * 1024;
    total_pages = total_memory / PAGE_SIZE;

    DEBUG_PRINTF("PMM: Total memory: %d MB (%d pages)",
                 (int)(total_memory / (1024 * 1024)), (int)total_pages);

    //clear bitmap (all pages marked as used initially)
    memset(page_bitmap, 0xFF, sizeof(page_bitmap));
    used_pages = total_pages;

    //available memory begins above low memory we currently free from end of kernel
    uint32_t available_end = total_memory;

    //reserve kernel space 
    uint32_t kernel_end = 0x500000;  //5mb

    //mark available pages as free (from end of kernel to end of memory)
    for (uint32_t addr = kernel_end; addr < available_end; addr += PAGE_SIZE) {
        uint32_t page = addr / PAGE_SIZE;
        if (page < total_pages && page < BITMAP_SIZE * 8) {
            clear_bit(page);
            used_pages--;
        }
    }

    DEBUG_PRINTF("PMM: Free pages: %d, Used pages: %d",
                 (int)(total_pages - used_pages), (int)used_pages);
}

uint32_t pmm_alloc_page(void) {
    //find first free page
    for (uint32_t page = 0; page < total_pages && page < BITMAP_SIZE * 8; page++) {
        if (!test_bit(page)) {
            set_bit(page);
            used_pages++;
            return page * PAGE_SIZE;
        }
    }
    return 0; //out of memory
}

void pmm_free_page(uint32_t page_addr) {
    uint32_t page = page_addr / PAGE_SIZE;
    if (page < total_pages && page < BITMAP_SIZE * 8) {
        if (test_bit(page)) {
            clear_bit(page);
            used_pages--;
        }
    }
}

uint32_t pmm_get_total_pages(void) {
    return total_pages;
}

uint32_t pmm_get_free_pages(void) {
    return total_pages - used_pages;
}

uint32_t pmm_get_used_pages(void) {
    return used_pages;
}
