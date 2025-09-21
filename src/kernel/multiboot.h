#ifndef KERNEL_MULTIBOOT_H
#define KERNEL_MULTIBOOT_H

#include <stdint.h>

//multiboot information structure (v0.6.96)
//only fields the kernel uses are defined here

typedef struct multiboot_mmap_entry {
    uint32_t size;        //size of the entry excluding this field
    uint64_t addr;        //base address
    uint64_t len;         //length in bytes
    uint32_t type;        //type of memory region
} __attribute__((packed)) multiboot_mmap_entry_t;

//memory map entry types
#define MULTIBOOT_MEMORY_AVAILABLE        1
#define MULTIBOOT_MEMORY_RESERVED         2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS              4
#define MULTIBOOT_MEMORY_BADRAM           5

typedef struct multiboot_info {
    uint32_t flags;          //0
    uint32_t mem_lower;      //4
    uint32_t mem_upper;      //8
    uint32_t boot_device;    //12
    uint32_t cmdline;        //16
    uint32_t mods_count;     //20
    uint32_t mods_addr;      //24
    uint32_t syms[4];        //28..43 (either a.out or ELF)
    uint32_t mmap_length;    //44
    uint32_t mmap_addr;      //48
    //ignore remaining firleds for now
} __attribute__((packed)) multiboot_info_t;

//multiboot info flags bits
#define MBI_FLAG_MEM     (1u << 0)   //mem_lower/mem_upper valid
#define MBI_FLAG_MMAP    (1u << 6)   //mmap_* valid
#define MBI_FLAG_MODS    (1u << 3)   //modules info valid

//multiboot module descriptor located at mods_addr when MBI_FLAG_MODS is set
typedef struct multiboot_module {
    uint32_t mod_start;   //start physical address
    uint32_t mod_end;     //end physical address (first byte after)
    uint32_t string;      //ASCII string pointer (e.x module name)
    uint32_t reserved;
} __attribute__((packed)) multiboot_module_t;

#endif
