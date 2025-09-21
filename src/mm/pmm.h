#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096
#define PAGE_ALIGN(addr) ((addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(addr) (addr & ~(PAGE_SIZE - 1))

//forward declare multiboot info so PMM can expose an initializer without depending on kernel headers
struct multiboot_info;

//physical memory manager
void pmm_init(uint32_t mem_low, uint32_t mem_high);
void pmm_init_multiboot(const struct multiboot_info* mbi,
                        uint32_t kernel_start_phys,
                        uint32_t kernel_end_phys);
uint32_t pmm_alloc_page(void);
void pmm_free_page(uint32_t page);
uint32_t pmm_get_total_pages(void);
uint32_t pmm_get_free_pages(void);
uint32_t pmm_get_used_pages(void);

//memory regions
typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t type;
} memory_region_t;

#define MEMORY_AVAILABLE 1
#define MEMORY_RESERVED  2
#define MEMORY_KERNEL    3

#endif
