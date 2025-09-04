#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_PRESENT    0x001
#define PAGE_WRITABLE   0x002
#define PAGE_USER       0x004
#define PAGE_ACCESSED   0x020
#define PAGE_DIRTY      0x040

//page directory and table entries
typedef uint32_t page_entry_t;
typedef uint32_t* page_table_t;
typedef uint32_t* page_directory_t;

//assembly functions
extern void enable_paging(uint32_t directory_phys);
extern void flush_tlb(void);

//virtual memory manager
void vmm_init(void);
int vmm_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
int vmm_unmap_page(uint32_t virtual_addr);
int vmm_unmap_page_nofree(uint32_t virtual_addr);
uint32_t vmm_get_physical_addr(uint32_t virtual_addr);
void vmm_switch_directory(page_directory_t directory);
page_directory_t vmm_create_directory(void);
void vmm_destroy_directory(page_directory_t directory);
int vmm_map_page_in_directory(page_directory_t directory, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);
void vmm_map_kernel_space(page_directory_t directory);
page_directory_t vmm_get_kernel_directory(void);
void vmm_destroy_directory(page_directory_t directory);


//kernel memory layout
#define KERNEL_VIRTUAL_BASE 0xC0000000
#define KERNEL_HEAP_START   0xC0400000
#define KERNEL_HEAP_END     0xCFFFFFFF
#define USER_VIRTUAL_START  0x00400000
#define USER_VIRTUAL_END    0xBFFFFFFF

//macros
#define VIRTUAL_TO_PHYSICAL(addr) ((addr) - KERNEL_VIRTUAL_BASE)
#define PHYSICAL_TO_VIRTUAL(addr) ((addr) + KERNEL_VIRTUAL_BASE)

#endif
