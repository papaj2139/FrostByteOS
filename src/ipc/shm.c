#include "shm.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../process.h"
#include "../drivers/serial.h"
#include <string.h>

#define MAX_SHM_SEGMENTS 256
#define SHM_BASE_ADDR 0xB0000000  //base address for shared memory mappings (user space)

static uint32_t next_shm_virt = SHM_BASE_ADDR;

typedef struct {
    int valid;
    key_t key;
    size_t size;
    int shmid;
    void* kernel_addr;  //kernel virtual address for the shared memory
    uint32_t phys_addr; //physical address
    int nattch;         //number of processes attached
    pid_t cpid;         //creator PID
    pid_t lpid;         //last operation PID
    mode_t mode;
    uid_t uid;
    gid_t gid;
} shm_segment_t;

static shm_segment_t shm_segments[MAX_SHM_SEGMENTS];
static int next_shmid = 1;

void shm_init(void) {
    memset(shm_segments, 0, sizeof(shm_segments));
    serial_write_string("[IPC] Shared memory initialized\n");
}

int sys_shmget(int key, int size, int shmflg) {
    //check if segment with this key already exists
    if (key != IPC_PRIVATE) {
        for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
            if (shm_segments[i].valid && shm_segments[i].key == key) {
                if (shmflg & IPC_CREAT && shmflg & IPC_EXCL) {
                    return -EEXIST;
                }
                return shm_segments[i].shmid;
            }
        }
    }
    
    //create flag not set and segment doesn't exist
    if (!(shmflg & IPC_CREAT)) {
        return -ENOENT;
    }
    
    //find a free slot
    int slot = -1;
    for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (!shm_segments[i].valid) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        return -ENOSPC;
    }
    
    //round size up to page boundary
    size = (size + 0xFFF) & ~0xFFF;
    
    //allocate physical memory (one page at a time)
    uint32_t num_pages = size / 0x1000;
    uint32_t phys_addr = 0;

    //allocate contiguous physical pages and zero them
    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t page = pmm_alloc_page();
        if (!page) {
            for (uint32_t j = 0; j < i; j++) {
                pmm_free_page(phys_addr + (j * 0x1000));
            }
            return -ENOMEM;
        }

        if (i == 0) {
            phys_addr = page;
        } else if (page != phys_addr + (i * 0x1000)) {
            pmm_free_page(page);
            for (uint32_t j = 0; j < i; j++) {
                pmm_free_page(phys_addr + (j * 0x1000));
            }
            return -ENOMEM;
        }
    }

    //zero the allocated pages using temporary mappings
    void* kernel_addr = (void*)PHYSICAL_TO_VIRTUAL(phys_addr);
    memset(kernel_addr, 0, num_pages * 0x1000);

    //initialize segment
    shm_segment_t* seg = &shm_segments[slot];
    seg->valid = 1;
    seg->key = key;
    seg->size = size;
    seg->phys_addr = phys_addr;
    seg->kernel_addr = kernel_addr;
    seg->shmid = next_shmid++;
    seg->nattch = 0;
    seg->cpid = process_get_current()->pid;
    seg->lpid = seg->cpid;
    seg->mode = shmflg & 0777;
    seg->uid = 0;  //TODO: get from current process
    seg->gid = 0;
    
    return seg->shmid;
}

int sys_shmat(int shmid, const void* shmaddr, int shmflg) {
    serial_printf("[SHM] shmat called with shmid=%d\n", shmid);
    
    //find the segment
    shm_segment_t* seg = NULL;
    for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (shm_segments[i].valid && shm_segments[i].shmid == shmid) {
            seg = &shm_segments[i];
            serial_printf("[SHM] Found segment at slot %d, size=0x%x, phys_addr=0x%x\n", 
                         i, seg->size, seg->phys_addr);
            break;
        }
    }
    
    if (!seg) {
        serial_write_string("[SHM] ERROR: Segment not found\n");
        return -EINVAL;
    }
    
    //determine attach address
    uint32_t addr;
    if (shmaddr) {
        addr = (uint32_t)shmaddr;
        if (shmflg & SHM_RND) {
            addr &= ~0xFFF;  //round down to page boundary
        }
    } else {
        //allocate unique region in user space (bump allocator)
        addr = next_shm_virt;
        next_shm_virt += seg->size;
        next_shm_virt = (next_shm_virt + 0xFFF) & ~0xFFF; //maintain page alignment
    }
    
    serial_printf("[SHM] Attaching at address 0x%x\n", addr);
    
    //get current process
    process_t* proc = process_get_current();
    if (!proc) {
        serial_write_string("[SHM] ERROR: No current process\n");
        return -ESRCH;
    }
    
    if (!proc->page_directory) {
        serial_write_string("[SHM] ERROR: Process has no page directory\n");
        return -EINVAL;
    }
    
    //map the shared memory into the process address space
    uint32_t num_pages = seg->size / 0x1000;
    
    serial_printf("[SHM] Mapping %d pages\n", (int)num_pages);
    
    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t virt = addr + (i * 0x1000);
        uint32_t phys = seg->phys_addr + (i * 0x1000);

        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        if (!(shmflg & SHM_RDONLY)) {
            flags |= PAGE_WRITABLE;
        }

        int result = vmm_map_page_in_directory(proc->page_directory, virt, phys, flags);
        if (result != 0) {
            serial_printf("[SHM] ERROR: Failed to map page %u at virt=0x%x phys=0x%x\n",
                         i, virt, phys);
            //unmap already mapped pages
            for (uint32_t j = 0; j < i; j++) {
                uint32_t unmap_virt = addr + (j * 0x1000);
                vmm_unmap_page_in_directory(proc->page_directory, unmap_virt);
            }
            return -ENOMEM;
        }
    }
    
    seg->nattch++;
    seg->lpid = proc->pid;
    
    serial_printf("[SHM] Successfully attached at 0x%x\n", addr);
    
    return (int)addr;
}

int sys_shmdt(const void* shmaddr) {
    if (!shmaddr) {
        return -EINVAL;
    }
    
    uint32_t addr = (uint32_t)shmaddr & ~0xFFF;
    
    //find which segment this address belongs to
    shm_segment_t* seg = NULL;
    for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (shm_segments[i].valid) {
            //we'd need to track attachments per process
            //for now just decrement attach count
            seg = &shm_segments[i];
            break;
        }
    }
    
    if (!seg) {
        return -EINVAL;
    }
    
    //unmap from process address space
    process_t* proc = process_get_current();
    uint32_t num_pages = seg->size / 0x1000;
    
    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t virt = addr + (i * 0x1000);
        //switch to process directory temporarily
        page_directory_t old_dir = vmm_get_current_directory();
        vmm_switch_directory(proc->page_directory);
        vmm_unmap_page(virt);
        vmm_switch_directory(old_dir);
    }
    
    seg->nattch--;
    seg->lpid = proc->pid;
    
    return 0;
}

int sys_shmctl(int shmid, int cmd, void* buf) {
    (void)buf;
    //find the segment
    shm_segment_t* seg = NULL;
    for (int i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (shm_segments[i].valid && shm_segments[i].shmid == shmid) {
            seg = &shm_segments[i];
            break;
        }
    }
    
    if (!seg) {
        return -EINVAL;
    }
    
    switch (cmd) {
        case IPC_RMID:
            //mark for deletion (actually delete when nattch reaches 0)
            if (seg->nattch == 0) {
                //free physical memory
                uint32_t num_pages = seg->size / 0x1000;
                for (uint32_t i = 0; i < num_pages; i++) {
                    pmm_free_page(seg->phys_addr + (i * 0x1000));
                }
                seg->valid = 0;
            }
            return 0;
            
        case IPC_STAT:
            //copy segment info to user buffer
            //TODO: implement proper structure copy
            return 0;
            
        case IPC_SET:
            //set segment info from user buffer
            //TODO: implement proper structure copy
            return 0;
            
        default:
            return -EINVAL;
    }
}
