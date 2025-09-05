#include "pmm.h"
#include "../drivers/serial.h"
#include <string.h>

#define MAX_MEMORY_REGIONS 32
#define BITMAP_SIZE (128 * 1024)  //support up to 512mb (128k pages * 4kb each)

static uint8_t page_bitmap[BITMAP_SIZE];
static uint32_t total_pages = 0;
static uint32_t used_pages = 0;
static memory_region_t memory_regions[MAX_MEMORY_REGIONS];
static int region_count = 0;

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

    //add available memory region (above 1mb to avoid low memory issues)
    uint32_t available_start = 0x100000;  //1mb
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
