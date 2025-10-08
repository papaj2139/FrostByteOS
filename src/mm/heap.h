#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

//heap allocator
void heap_init(void);
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, uint32_t alignment);
void* kmalloc_physical(size_t size, uint32_t* physical_addr);
void kfree(void* ptr);

//heap stats
typedef struct {
    size_t total_size;
    size_t used_size;
    size_t free_size;
    size_t num_blocks;
} heap_stats_t;

void heap_get_stats(heap_stats_t* stats);

#endif
