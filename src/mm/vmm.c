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
extern void switch_cr3(uint32_t directory_phys);

static page_table_t map_pt_temp(uint32_t pt_phys, uint32_t* saved_entry_out);
static void unmap_pt_temp(uint32_t saved_entry);

//single temporary mapping slot for kernel helpers
#define TEMP_MAP_VA 0x007FD000

//map a physical page temporarily at a scratch VA (<8MB identity-mapped PDE/PT)
//and zero it hen unmap the scratch VA this avoids relying on PHYSICAL_TO_VIRTUAL
//for pages beyond the pre-mapped higher-half range
static void zero_phys_page_temp(uint32_t phys) {
    //map target page into TEMP_MAP_VA safely, zero it then unmap
    uint32_t eflags_save; 
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    uint32_t saved_entry = 0;
    void* va = vmm_map_temp_page(phys, &saved_entry);
    if (va) {
        memset(va, 0, PAGE_SIZE);
        vmm_unmap_temp_page(saved_entry);
    }
    if (eflags_save & 0x200) __asm__ volatile ("sti");
}

void* vmm_map_temp_page(uint32_t phys_addr, uint32_t* saved_entry_out) {
    page_directory_t dir = current_directory ? current_directory : kernel_directory;
    if (!dir) return NULL;

    uint32_t pd_i = PAGE_DIRECTORY_INDEX(TEMP_MAP_VA);
    uint32_t pt_i = PAGE_TABLE_INDEX(TEMP_MAP_VA);

    if (!(dir[pd_i] & PAGE_PRESENT)) {
        return NULL;
    }

    //avoid preemption while toggling TEMP_MAP_VA PTE
    uint32_t eflags_save; 
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");

    uint32_t pt_phys = dir[pd_i] & ~0xFFF;
    page_table_t pt = (page_table_t)PHYSICAL_TO_VIRTUAL(pt_phys);

    uint32_t old = pt[pt_i];
    pt[pt_i] = (phys_addr & ~0xFFF) | PAGE_PRESENT | PAGE_WRITABLE;
    flush_tlb();

    if (saved_entry_out) {
        *saved_entry_out = old;
    }

    if (eflags_save & 0x200) __asm__ volatile ("sti");
    return (void*)TEMP_MAP_VA;
}

void vmm_unmap_temp_page(uint32_t saved_entry) {
    page_directory_t dir = current_directory ? current_directory : kernel_directory;
    if (!dir) return;

    uint32_t pd_i = PAGE_DIRECTORY_INDEX(TEMP_MAP_VA);
    uint32_t pt_i = PAGE_TABLE_INDEX(TEMP_MAP_VA);

    if (!(dir[pd_i] & PAGE_PRESENT)) {
        return;
    }

    uint32_t eflags_save; 
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    uint32_t pt_phys = dir[pd_i] & ~0xFFF;
    page_table_t pt = (page_table_t)PHYSICAL_TO_VIRTUAL(pt_phys);

    pt[pt_i] = saved_entry;
    flush_tlb();
    if (eflags_save & 0x200) __asm__ volatile ("sti");
}

int vmm_unmap_page_in_directory(page_directory_t directory, uint32_t virtual_addr) {
    if (!directory) return -1;

    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virtual_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virtual_addr);

    if (!(directory[pd_index] & PAGE_PRESENT)) {
        return -1;
    }

    uint32_t pt_phys = directory[pd_index] & ~0xFFF;
    //protect PT scratch mapping against interrupts while active
    uint32_t eflags_save; 
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    uint32_t saved_entry;
    page_table_t page_table = map_pt_temp(pt_phys, &saved_entry);
    if (!page_table) { 
        if (eflags_save & 0x200) __asm__ volatile ("sti"); 
        return -1; 
    }

    if (!(page_table[pt_index] & PAGE_PRESENT)) {
        unmap_pt_temp(saved_entry);
        if (eflags_save & 0x200) __asm__ volatile ("sti");
        return -1;
    }

    page_table[pt_index] = 0;
    unmap_pt_temp(saved_entry);
    if (eflags_save & 0x200) __asm__ volatile ("sti");
    flush_tlb();

    return 0;
}

//temporarily map a page table physical page into a scratch VA and return a pointer to it
//if the physical address is within the pre-mapped 0 to 8MB range return the higher-half VA directly
//and set *saved_entry_out = 0xFFFFFFFF to indicate no unmap needed
static page_table_t map_pt_temp(uint32_t pt_phys, uint32_t* saved_entry_out) {
    if (pt_phys < 0x00800000) {
        if (saved_entry_out) *saved_entry_out = 0xFFFFFFFF;
        return (page_table_t)PHYSICAL_TO_VIRTUAL(pt_phys);
    }
    const uint32_t PT_SCRATCH = 0x007FE000;
    page_directory_t dir = current_directory ? current_directory : kernel_directory;
    if (!dir) return 0;
    uint32_t pd_i = PAGE_DIRECTORY_INDEX(PT_SCRATCH);
    uint32_t pt_i = PAGE_TABLE_INDEX(PT_SCRATCH);
    if (!(dir[pd_i] & PAGE_PRESENT)) return 0;
    uint32_t id_pt_phys = dir[pd_i] & ~0xFFF;
    //protect retargeting of PT_SCRATCH against preemption
    uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    page_table_t id_pt = (page_table_t)PHYSICAL_TO_VIRTUAL(id_pt_phys);
    uint32_t old = id_pt[pt_i];
    id_pt[pt_i] = (pt_phys & ~0xFFF) | PAGE_PRESENT | PAGE_WRITABLE;
    flush_tlb();
    if (saved_entry_out) *saved_entry_out = old;
    if (eflags_save & 0x200) __asm__ volatile ("sti");
    return (page_table_t)PT_SCRATCH;
}

static void unmap_pt_temp(uint32_t saved_entry) {
    if (saved_entry == 0xFFFFFFFF) return;
    const uint32_t PT_SCRATCH = 0x007FE000;
    page_directory_t dir = current_directory ? current_directory : kernel_directory;
    if (!dir) return;
    uint32_t pd_i = PAGE_DIRECTORY_INDEX(PT_SCRATCH);
    uint32_t pt_i = PAGE_TABLE_INDEX(PT_SCRATCH);
    uint32_t id_pt_phys = dir[pd_i] & ~0xFFF;
    uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    page_table_t id_pt = (page_table_t)PHYSICAL_TO_VIRTUAL(id_pt_phys);
    id_pt[pt_i] = saved_entry;
    flush_tlb();
    if (eflags_save & 0x200) __asm__ volatile ("sti");
}

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

    //map first 128MB to higher half (3GB virtual address)
    for (uint32_t addr = 0; addr < 0x08000000; addr += PAGE_SIZE) {
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

        //clear the new page table using PT scratch mapping with interrupts disabled
        uint32_t eflags_save_pt; 
        __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save_pt) :: "memory");
        uint32_t saved_entry_pt;
        page_table_t new_pt = map_pt_temp(pt_phys, &saved_entry_pt);
        if (!new_pt) {
            if (eflags_save_pt & 0x200) __asm__ volatile ("sti");
            pmm_free_page(pt_phys);
            return -1;
        }
        memset(new_pt, 0, PAGE_SIZE);
        unmap_pt_temp(saved_entry_pt);
        if (eflags_save_pt & 0x200) __asm__ volatile ("sti");
    }

    //get page table and update via safe mapping
    uint32_t pt_phys = directory[pd_index] & ~0xFFF;
    uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    uint32_t saved_entry;
    page_table_t page_table = map_pt_temp(pt_phys, &saved_entry);
    if (!page_table) { if (eflags_save & 0x200) __asm__ volatile ("sti"); return -1; }
    //map the page
    page_table[pt_index] = (physical_addr & ~0xFFF) | flags;
    unmap_pt_temp(saved_entry);
    if (eflags_save & 0x200) __asm__ volatile ("sti");

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
    uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    uint32_t saved_entry;
    page_table_t page_table = map_pt_temp(pt_phys, &saved_entry);
    if (!page_table) { if (eflags_save & 0x200) __asm__ volatile ("sti"); return -1; }

    if (!(page_table[pt_index] & PAGE_PRESENT)) {
        unmap_pt_temp(saved_entry);
        if (eflags_save & 0x200) __asm__ volatile ("sti");
        return -1; //page not mapped
    }

    //get physical address to free
    uint32_t phys_addr = page_table[pt_index] & ~0xFFF;

    //clear page table entry
    page_table[pt_index] = 0;
    unmap_pt_temp(saved_entry);
    if (eflags_save & 0x200) __asm__ volatile ("sti");

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
    uint32_t eflags_save; 
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    uint32_t saved_entry;
    page_table_t page_table = map_pt_temp(pt_phys, &saved_entry);
    if (!page_table) { if (eflags_save & 0x200) __asm__ volatile ("sti"); return -1; }

    if (!(page_table[pt_index] & PAGE_PRESENT)) {
        unmap_pt_temp(saved_entry);
        if (eflags_save & 0x200) __asm__ volatile ("sti");
        return -1; //page not mapped
    }

    //clear page table entry without freeing the physical frame
    page_table[pt_index] = 0;
    unmap_pt_temp(saved_entry);
    if (eflags_save & 0x200) __asm__ volatile ("sti");

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
    uint32_t eflags_save;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    uint32_t saved_entry;
    page_table_t page_table = map_pt_temp(pt_phys, &saved_entry);
    if (!page_table) { 
        if (eflags_save & 0x200) __asm__ volatile ("sti"); 
        return 0; 
    }

    if (!(page_table[pt_index] & PAGE_PRESENT)) {
        unmap_pt_temp(saved_entry);
        if (eflags_save & 0x200) __asm__ volatile ("sti");
        return 0; //not mapped
    }

    uint32_t phys_page = page_table[pt_index] & ~0xFFF;
    uint32_t offset = virtual_addr & 0xFFF;
    unmap_pt_temp(saved_entry);
    if (eflags_save & 0x200) __asm__ volatile ("sti");

    return phys_page + offset;
}

page_directory_t vmm_create_directory(void) {
    uint32_t dir_phys = pmm_alloc_page();
    if (!dir_phys) return 0;

    //calculate the virtual address for this page directory
    page_directory_t dir_virt = (page_directory_t)PHYSICAL_TO_VIRTUAL(dir_phys);

    //map the page directory into the kernel address space so we can access it
    //save current directory and temporarily switch to kernel directory
    page_directory_t saved_dir = current_directory;
    current_directory = kernel_directory;
    
    //this ensures the returned pointer is usable in kernel space
    int map_result = vmm_map_page((uint32_t)dir_virt, dir_phys, PAGE_PRESENT | PAGE_WRITABLE);
    
    //restore previous directory
    current_directory = saved_dir;
    
    if (map_result != 0) {
        pmm_free_page(dir_phys);
        return 0;
    }

    //now we can safely access it via the virtual address
    memset(dir_virt, 0, PAGE_SIZE);

    //copy kernel mappings (higher half) but only if they're present
    for (int i = 768; i < 1024; i++) { //768 = 3GB / 4MB
        if (kernel_directory[i] & PAGE_PRESENT) {
            dir_virt[i] = kernel_directory[i];
        }
    }

    //copy the identity-mapped PDEs for 0..8MB used by scratch mapping helpers
    //PDE size is 4MB so indices 0 and 1 cover 0..8MB
    if (kernel_directory[0] & PAGE_PRESENT) {
        dir_virt[0] = kernel_directory[0];
    }
    if (kernel_directory[1] & PAGE_PRESENT) {
        dir_virt[1] = kernel_directory[1];
    }

    return dir_virt;
}

void vmm_switch_directory(page_directory_t directory) {
    if (!directory) return;
    if (current_directory == directory) return; //avoid redundant TLB flush
    current_directory = directory;
    uint32_t dir_phys = VIRTUAL_TO_PHYSICAL((uint32_t)directory);
    switch_cr3(dir_phys);
}


//map a page in a specific page directory
int vmm_map_page_in_directory(page_directory_t directory, uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags) {
    uint32_t pd_index = PAGE_DIRECTORY_INDEX(virtual_addr);
    uint32_t pt_index = PAGE_TABLE_INDEX(virtual_addr);

    if (!directory) {
        return -1; //invalid directory
    }

    //ensure page table exists
    if (!(directory[pd_index] & PAGE_PRESENT)) {
        uint32_t pt_phys = pmm_alloc_page();
        if (!pt_phys) {
            return -1; //out of memory
        }
        directory[pd_index] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
        //clear the new page table using scratch mapping to avoid higher-half dependency
        uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
        uint32_t saved_entry;
        page_table_t page_table = map_pt_temp(pt_phys, &saved_entry);
        if (!page_table) { 
            if (eflags_save & 0x200) __asm__ volatile ("sti"); 
            return -1; 
        }
        memset(page_table, 0, PAGE_SIZE);
        unmap_pt_temp(saved_entry);
        if (eflags_save & 0x200) __asm__ volatile ("sti");
    }

    //get page table via scratch mapping (robust even if pt_phys is outside higher-half direct map)
    uint32_t pt_phys = directory[pd_index] & ~0xFFF;
    uint32_t eflags_save; 
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    uint32_t saved_entry;
    page_table_t page_table = map_pt_temp(pt_phys, &saved_entry);
    if (!page_table) { 
        if (eflags_save & 0x200) __asm__ volatile ("sti"); 
        return -1; 
    }
    //map the page
    page_table[pt_index] = (physical_addr & ~0xFFF) | flags;
    unmap_pt_temp(saved_entry);
    if (eflags_save & 0x200) __asm__ volatile ("sti");

    return 0;
}

//map kernel space into a user page directory
void vmm_map_kernel_space(page_directory_t directory) {
    if (!directory || !kernel_directory) {
        return;
    }

    //copy kernel mappings (higher half starting from 3GB) by mirroring PDEs
    for (int i = 768; i < 1024; i++) { //768 = 3GB / 4MB
        directory[i] = kernel_directory[i];
    }
}

//get the kernel page directory
page_directory_t vmm_get_kernel_directory(void) {
    return kernel_directory;
}

//get the current active page directory
page_directory_t vmm_get_current_directory(void) {
    return current_directory ? current_directory : kernel_directory;
}

//destroy a page directory and free its resources
void vmm_destroy_directory(page_directory_t directory) {
    if (!directory || directory == kernel_directory) {
        return; //don't destroy kernel directory or NULL pointer
    }

    //walk all user PDEs and free mapped pages and their page tables
    for (int i = 0; i < 768; i++) { //only user space (0-3GB)
        if (!(directory[i] & PAGE_PRESENT)) continue;
        //do NOT free shared identity-mapped PTs (0..8MB) copied from kernel dir
        //PDE size is 4MB so indices 0 and 1 cover 0..8MB
        if (i < 2 && kernel_directory && directory[i] == kernel_directory[i]) {
            continue;
        }
        uint32_t pt_phys = directory[i] & ~0xFFF;
        //map the page table temporarily to walk PTEs
        uint32_t eflags_save_pt; 
        __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save_pt) :: "memory");
        uint32_t saved_entry;
        page_table_t pt = map_pt_temp(pt_phys, &saved_entry);
        if (pt) {
            for (int j = 0; j < 1024; j++) {
                uint32_t pte = pt[j];
                if (pte & PAGE_PRESENT) {
                    uint32_t page_phys = pte & ~0xFFF;
                    //free the mapped physical page frame
                    pmm_free_page(page_phys);
                    pt[j] = 0;
                }
            }
            unmap_pt_temp(saved_entry);
            if (eflags_save_pt & 0x200) __asm__ volatile ("sti");
        }
        //free the page table frame itself
        pmm_free_page(pt_phys);
        directory[i] = 0;
    }

    //free the page directory itself
    uint32_t dir_phys = VIRTUAL_TO_PHYSICAL((uint32_t)directory);
    pmm_free_page(dir_phys);
}
