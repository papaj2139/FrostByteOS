#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "../drivers/serial.h"
#include <string.h>

typedef struct heap_block {
    size_t size;
    int free;
    struct heap_block* next;
    struct heap_block* prev;
} heap_block_t;

static heap_block_t* heap_start = 0;
static uint32_t heap_end = KERNEL_HEAP_START;
static size_t total_allocated = 0;

void heap_init(void) {
    DEBUG_PRINT("HEAP: Initializing kernel heap");

    //allocate initial heap page
    uint32_t phys_page = pmm_alloc_page();
    if (!phys_page) {
        DEBUG_PRINT("HEAP: Failed to allocate initial heap page!");
        return;
    }

    if (vmm_map_page(KERNEL_HEAP_START, phys_page, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        DEBUG_PRINT("HEAP: Failed to map initial heap page!");
        pmm_free_page(phys_page);
        return;
    }

    heap_start = (heap_block_t*)KERNEL_HEAP_START;
    heap_start->size = PAGE_SIZE - sizeof(heap_block_t);
    heap_start->free = 1;
    heap_start->next = 0;
    heap_start->prev = 0;

    heap_end = KERNEL_HEAP_START + PAGE_SIZE;

    DEBUG_PRINTF("HEAP: Initialized with %d bytes at 0x%x",
                 (int)heap_start->size, KERNEL_HEAP_START);
}

static int expand_heap(size_t needed_size) {
    size_t pages_needed = (needed_size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (size_t i = 0; i < pages_needed; i++) {
        uint32_t phys_page = pmm_alloc_page();
        if (!phys_page) {
            return -1;
        }

        if (vmm_map_page(heap_end, phys_page, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            pmm_free_page(phys_page);
            return -1;
        }

        heap_end += PAGE_SIZE;
    }

    return 0;
}

void* kmalloc(size_t size) {
    if (size == 0) return 0;

    //align size
    size = (size + 7) & ~7;

    heap_block_t* current = heap_start;

    //find free block
    while (current) {
        if (current->free && current->size >= size) {
            //split block if necessary
            if (current->size > size + sizeof(heap_block_t) + 8) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)current + sizeof(heap_block_t) + size);
                new_block->size = current->size - size - sizeof(heap_block_t);
                new_block->free = 1;
                new_block->next = current->next;
                new_block->prev = current;

                if (current->next) {
                    current->next->prev = new_block;
                }
                current->next = new_block;
                current->size = size;
            }

            current->free = 0;
            total_allocated += current->size;

            void* ret_ptr = (void*)((uint8_t*)current + sizeof(heap_block_t));
            //verify the entire block is within mapped heap range
            uint32_t block_end = (uint32_t)ret_ptr + size;
            if (block_end > heap_end) {
                //block extends beyond heap this shouldn't happen
                current->free = 1; //mark as free again
                total_allocated -= current->size;
                return 0;
            }
            return ret_ptr;
        }
        current = current->next;
    }

    //need to expand heap
    size_t needed = size + sizeof(heap_block_t);
    //remember where the new region will begin before expansion
    uint32_t old_end = heap_end;
    if (expand_heap(needed) != 0) {
        return 0; //out of memory
    }

    //create new block at the start of the newly expanded region
    //old_end marks the first byte of the newly mapped space
    heap_block_t* new_block = (heap_block_t*)(old_end);
    new_block->size = size;
    new_block->free = 0;
    new_block->next = 0;

    //find last block and link
    current = heap_start;
    while (current->next) {
        current = current->next;
    }
    current->next = new_block;
    new_block->prev = current;

    total_allocated += size;

    void* ret_ptr = (void*)((uint8_t*)new_block + sizeof(heap_block_t));
    //verify the entire block is within mapped heap range
    uint32_t block_end = (uint32_t)ret_ptr + size;
    if (block_end > heap_end) {
        //this shouldn't happen after expansion but safety check
        new_block->free = 1;
        total_allocated -= size;
        return 0;
    }
    return ret_ptr;
}

void kfree(void* ptr) {
    if (!ptr) return;

    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->free = 1;
    total_allocated -= block->size;

    //merge with next block if free
    if (block->next && block->next->free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        if (block->next->next) {
            block->next->next->prev = block;
        }
        block->next = block->next->next;
    }

    //merge with previous block if free
    if (block->prev && block->prev->free) {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next) {
            block->next->prev = block->prev;
        }
    }
}

void heap_get_stats(heap_stats_t* stats) {
    if (!stats) return;

    stats->total_size = heap_end - KERNEL_HEAP_START;
    stats->used_size = total_allocated;
    stats->free_size = stats->total_size - stats->used_size;
    stats->num_blocks = 0;

    //count blocks
    heap_block_t* current = heap_start;
    while (current) {
        stats->num_blocks++;
        current = current->next;
    }
}

//aligned allocation  (fallback kmalloc for now)
void* kmalloc_aligned(size_t size, uint32_t alignment) {
    (void)alignment;
    return kmalloc(size);
}

//allocate memory and also return the physical address of the first byte
void* kmalloc_physical(size_t size, uint32_t* physical_addr) {
    void* p = kmalloc(size);
    if (!p) return 0;
    if (physical_addr) {
        //translate the first byte to physical address
        *physical_addr = vmm_get_physical_addr((uint32_t)p);
    }
    return p;
}

