#ifndef GDT_H
#define GDT_H

#include <stdint.h>

//GDT entry structure
typedef struct __attribute__((packed)) {
    uint16_t limit_low;   //lower 16 bits of limit
    uint16_t base_low;    //lower 16 bits of base
    uint8_t  base_middle; //next 8 bits of base
    uint8_t  access;      //access flags
    uint8_t  granularity; //granularity and upper 4 bits of limit
    uint8_t  base_high;   //upper 8 bits of base
} gdt_entry_t;

//GDT pointer structure
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} gdt_ptr_t;

void gdt_init(void);
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

#endif
