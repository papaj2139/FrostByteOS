#include "vmm.h"
#include "pmm.h"
#include "../drivers/serial.h"
#include <string.h>

static page_directory_t kernel_directory = 0;
static page_directory_t current_directory = 0;

//get page directory index from virtual address
#define PAGE_DIRECTORY_INDEX(addr) ((addr) >> 22)
//get page table index from virtual address
#define PAGE_TABLE_INDEX(addr) (((addr) >> 12) & 0x3FF)

extern void enable_paging(uint32_t directory_phys);
extern void flush_tlb(void);

//this function is used before paging is enabled to map pages directly using physical addresses
static int vmm_map_page_direct(uint32_t* directory, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virtual_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virtual_addr);

    //check if page table exists
    if (!(directory[pd_index] & PAGE_PRESENT)) {
        //allocate new page table
        uint32_t pt_phys = pmm_alloc_page();
        if (!pt_phys) {
            return -1; //out of memory
        }

        directory[pd_index] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);

        //clear the new page table use physical address directly
        memset((void*)pt_phys, 0, PAGE_SIZE);
    }

    //get page table physical address
    uint32_t pt_phys = directory[pd_index] & ~0xFFF;
    uint32_t* page_table = (uint32_t*)pt_phys;

    //map the page
    page_table[pt_index] = (physical_addr & ~0xFFF) | flags;

    return 0;
}

void vmm_init(void) {
    DEBUG_PRINT("VMM: Initializing virtual memory manager");

    //allocate kernel page directory
    uint32_t kernel_dir_phys = pmm_alloc_page();
    if (!kernel_dir_phys) {
        DEBUG_PRINT("VMM: Failed to allocate kernel page directory!");
        return;
    }

    //for now work with physical addresses until paging is enabled
    uint32_t* dir_phys_ptr = (uint32_t*)kernel_dir_phys;
    memset(dir_phys_ptr, 0, PAGE_SIZE);

    DEBUG_PRINTF("VMM: Kernel directory at physical 0x%x", kernel_dir_phys);

    //identity map first 8MB (kernel space + extra room)
    for (uint32_t addr = 0; addr < 0x800000; addr += PAGE_SIZE) {
        if (vmm_map_page_direct(dir_phys_ptr, addr, addr, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            DEBUG_PRINT("VMM: Failed to identity map kernel space");
            return;
        }
    }

    //map first 8MB to higher half (3GB virtual address)
    for (uint32_t addr = 0; addr < 0x800000; addr += PAGE_SIZE) {
        if (vmm_map_page_direct(dir_phys_ptr, KERNEL_VIRTUAL_BASE + addr, addr, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            DEBUG_PRINT("VMM: Failed to map kernel to higher half");
            return;
        }
    }

    DEBUG_PRINT("VMM: Kernel memory mapped");

    //switch to new page directory passing physical address
    enable_paging(kernel_dir_phys);

    //now we can use virtual addresses
    kernel_directory = (page_directory_t)PHYSICAL_TO_VIRTUAL(kernel_dir_phys);
    current_directory = kernel_directory;

    DEBUG_PRINT("VMM: Paging enabled successfully");
}

int vmm_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virtual_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virtual_addr);

    page_directory_t directory = current_directory;
    if (!directory) directory = kernel_directory;

    //check if page table exists
    if (!(directory[pd_index] & PAGE_PRESENT)) {
        //allocate new page table
        uint32_t pt_phys = pmm_alloc_page();
        if (!pt_phys) {
            return -1; //out of memory
        }

        directory[pd_index] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);

        //clear the new page table
        page_table_t pt = (page_table_t)PHYSICAL_TO_VIRTUAL(pt_phys);
        memset(pt, 0, PAGE_SIZE);
    }

    //get page table
    uint32_t pt_phys = directory[pd_index] & ~0xFFF;
    page_table_t page_table = (page_table_t)PHYSICAL_TO_VIRTUAL(pt_phys);

    //map the page
    page_table[pt_index] = (physical_addr & ~0xFFF) | flags;

    //flush TLB entry
    flush_tlb();

    return 0;
}

int vmm_unmap_page(uint32_t virtual_addr) {
    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virtual_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virtual_addr);

    page_directory_t directory = current_directory;
    if (!directory) directory = kernel_directory;

    if (!(directory[pd_index] & PAGE_PRESENT)) {
        return -1; //page table doesn't exist
    }

    uint32_t pt_phys = directory[pd_index] & ~0xFFF;
    page_table_t page_table = (page_table_t)PHYSICAL_TO_VIRTUAL(pt_phys);

    if (!(page_table[pt_index] & PAGE_PRESENT)) {
        return -1; //page not mapped
    }

    //get physical address to free
    uint32_t phys_addr = page_table[pt_index] & ~0xFFF;

    //clear page table entry
    page_table[pt_index] = 0;

    //free physical page
    pmm_free_page(phys_addr);

    //flush TLB
    flush_tlb();

    return 0;
}

int vmm_unmap_page_nofree(uint32_t virtual_addr) {
    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virtual_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virtual_addr);

    page_directory_t directory = current_directory;
    if (!directory) directory = kernel_directory;

    if (!(directory[pd_index] & PAGE_PRESENT)) {
        return -1; //page table doesn't exist
    }

    uint32_t pt_phys = directory[pd_index] & ~0xFFF;
    page_table_t page_table = (page_table_t)PHYSICAL_TO_VIRTUAL(pt_phys);

    if (!(page_table[pt_index] & PAGE_PRESENT)) {
        return -1; //page not mapped
    }

    //clear page table entry without freeing the physical frame
    page_table[pt_index] = 0;

    //flush TLB
    flush_tlb();

    return 0;
}

uint32_t vmm_get_physical_addr(uint32_t virtual_addr) {
    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virtual_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virtual_addr);

    page_directory_t directory = current_directory;
    if (!directory) directory = kernel_directory;

    if (!(directory[pd_index] & PAGE_PRESENT)) {
        return 0; //not mapped
    }

    uint32_t pt_phys = directory[pd_index] & ~0xFFF;
    page_table_t page_table = (page_table_t)PHYSICAL_TO_VIRTUAL(pt_phys);

    if (!(page_table[pt_index] & PAGE_PRESENT)) {
        return 0; //not mapped
    }

    uint32_t phys_page = page_table[pt_index] & ~0xFFF;
    uint32_t offset = virtual_addr & 0xFFF;

    return phys_page + offset;
}

page_directory_t vmm_create_directory(void) {
    uint32_t dir_phys = pmm_alloc_page();
    if (!dir_phys) return 0;

    page_directory_t directory = (page_directory_t)PHYSICAL_TO_VIRTUAL(dir_phys);
    memset(directory, 0, PAGE_SIZE);

    //copy kernel mappings (higher half)
    for (int i = 768; i < 1024; i++) { //768 = 3GB / 4MB
        directory[i] = kernel_directory[i];
    }

    return directory;
}

void vmm_switch_directory(page_directory_t directory) {
    current_directory = directory;
    uint32_t dir_phys = VIRTUAL_TO_PHYSICAL((uint32_t)directory);
    enable_paging(dir_phys);
}


//map a page in a specific page directory
int vmm_map_page_in_directory(page_directory_t directory, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virtual_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virtual_addr);

    if (!directory) {
        return -1; //invalid directory
    }

    //check if page table exists
    if (!(directory[pd_index] & PAGE_PRESENT)) {
        //allocate new page table
        uint32_t pt_phys = pmm_alloc_page();
        if (!pt_phys) {
            return -1; //out of memory
        }

        directory[pd_index] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);

        //clear the new page table
        page_table_t pt = (page_table_t)PHYSICAL_TO_VIRTUAL(pt_phys);
        memset(pt, 0, PAGE_SIZE);
    }

    //get page table
    uint32_t pt_phys = directory[pd_index] & ~0xFFF;
    page_table_t page_table = (page_table_t)PHYSICAL_TO_VIRTUAL(pt_phys);

    //map the page
    page_table[pt_index] = (physical_addr & ~0xFFF) | flags;

    return 0;
}

//map kernel space into a user page directory
void vmm_map_kernel_space(page_directory_t directory) {
    if (!directory || !kernel_directory) {
        return;
    }

    //copy kernel mappings (higher half  starting from 3GB)
    for (int i = 768; i < 1024; i++) { //768 = 3GB / 4MB
        directory[i] = kernel_directory[i];
    }
}

//get the kernel page directory
page_directory_t vmm_get_kernel_directory(void) {
    return kernel_directory;
}

//destroy a page directory and free its resources
void vmm_destroy_directory(page_directory_t directory) {
    if (!directory || directory == kernel_directory) {
        return; //don't destroy kernel directory or NULL pointer
    }

    //free all user page tables (not kernel ones)
    for (int i = 0; i < 768; i++) { //only user space (0-3GB)
        if (directory[i] & PAGE_PRESENT) {
            uint32_t pt_phys = directory[i] & ~0xFFF;
            pmm_free_page(pt_phys);
        }
    }

    //free the page directory itself
    uint32_t dir_phys = VIRTUAL_TO_PHYSICAL((uint32_t)directory);
    pmm_free_page(dir_phys);
}
