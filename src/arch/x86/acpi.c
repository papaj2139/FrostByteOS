#include "acpi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../../mm/vmm.h"
#include "../../mm/pmm.h"
#include "../../io.h"
#include "../../drivers/serial.h"

static int acpi_checksum(void *ptr, size_t len) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t*)ptr;
    for (size_t i = 0; i < len; i++) sum += p[i];
    return sum == 0;
}
 
static rsdp_descriptor_t* find_rsdp(void) {
    //EBDA segment pointer at 0x40E
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Warray-bounds"
    uint16_t ebda_seg = *(uint16_t*)0x40E;
    #pragma GCC diagnostic pop
     
    uint32_t ebda = ((uint32_t)ebda_seg) << 4;
    if (ebda >= 0x80000 && ebda < 0xA0000) {
        for (uint32_t addr = ebda; addr < ebda + 1024; addr += 16) {
            if (memcmp((void*)addr, RSDP_SIG, 8) == 0) {
                rsdp_descriptor_t *rsdp = (rsdp_descriptor_t*)addr;
                if (acpi_checksum(rsdp, 20)) return rsdp;
            }
        }
    }
    //BIOS area scan
    for (uint32_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        if (memcmp((void*)addr, RSDP_SIG, 8) == 0) {
            rsdp_descriptor_t *rsdp = (rsdp_descriptor_t*)addr;
            if (acpi_checksum(rsdp, 20)) return rsdp;
        }
    }
    return NULL;
}

static acpi_table_header_t* find_acpi_table(acpi_table_header_t *rsdt, const char *signature) {
    if (!rsdt) return NULL;
    bool is_xsdt = (memcmp(rsdt->signature, ACPI_SIG_XSDT, 4) == 0);
    uint32_t entry_size = is_xsdt ? 8 : 4;
    uint32_t entries = (rsdt->length - sizeof(acpi_table_header_t)) / entry_size;
    uint8_t *table_data = (uint8_t*)rsdt + sizeof(acpi_table_header_t);
 
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t table_addr;
        if (is_xsdt) {
            uint64_t addr64 = *(uint64_t*)(table_data + i * 8);
            if (addr64 > 0xFFFFFFFFu) continue; //ignore 64-bit above 4GB
            table_addr = (uint32_t)addr64;
        } else {
            table_addr = *(uint32_t*)(table_data + i * 4);
        }
        uint32_t page_addr = table_addr & ~(PAGE_SIZE - 1);
        uint32_t offset = table_addr & (PAGE_SIZE - 1);
        uint32_t temp_virt = 0x400000; //temp mapping
        if (vmm_map_page(temp_virt, page_addr, PAGE_PRESENT | PAGE_WRITABLE) != 0) continue;
        acpi_table_header_t *table = (acpi_table_header_t*)(temp_virt + offset);
        bool match = (memcmp(table->signature, signature, 4) == 0);
        vmm_unmap_page_nofree(temp_virt);
        if (match) return (acpi_table_header_t*)table_addr; //return physical
    }
    return NULL;
}

static uint16_t find_s5_sleep_type(acpi_table_header_t *dsdt) {
    if (!dsdt) return 5; //default S5
    uint8_t *data = (uint8_t*)dsdt;
    uint32_t length = dsdt->length;
    for (uint32_t i = 0; i < length - 10; i++) {
        if (data[i] == '_' && data[i+1] == 'S' && data[i+2] == '5' && data[i+3] == '_') {
            for (uint32_t j = i + 4; j < i + 20 && j < length - 5; j++) {
                if (data[j] == 0x12) { //package opcode
                    uint32_t k = j + 1;
                    if (data[k] & 0xC0) k += (data[k] >> 6) & 3; //pkg length
                    k++; //element count
                    if (k < length) {
                        if (data[k] == 0x0A && k + 1 < length) {
                            uint8_t val = data[k + 1];
                            if (val > 0 && val < 8) return val;
                        } else if (data[k] == 0x01) {
                            return 1;
                        }
                    }
                    break;
                }
            }
        }
    }
    return 5;
}

void acpi_shutdown(void) {
    DEBUG_PRINT("Initiating ACPI shutdown...");

    rsdp_descriptor_t *rsdp = find_rsdp();
    if (!rsdp) goto fallback_shutdown;
    DEBUG_PRINT("RSDP found");

    //determine RSDT/XSDT physical address
    uint32_t rsdt_phys = 0;
    if (rsdp->revision >= 2 && rsdp->xsdt_address && (rsdp->xsdt_address >> 32) == 0) {
        rsdt_phys = (uint32_t)rsdp->xsdt_address;
    } else if (rsdp->rsdt_address) {
        rsdt_phys = rsdp->rsdt_address;
    }
    if (!rsdt_phys) goto fallback_shutdown;
 
    //map rsdt/xsdt
    uint32_t rsdt_page = rsdt_phys & ~(PAGE_SIZE - 1);
    uint32_t rsdt_offset = rsdt_phys & (PAGE_SIZE - 1);
    uint32_t rsdt_virt = 0x500000;
    if (vmm_map_page(rsdt_virt, rsdt_page, PAGE_PRESENT | PAGE_WRITABLE) != 0) goto fallback_shutdown;
    acpi_table_header_t *rsdt = (acpi_table_header_t*)(rsdt_virt + rsdt_offset);
 
    //locate FADT
    uint32_t fadt_phys = (uint32_t)find_acpi_table(rsdt, ACPI_SIG_FADT);
    if (!fadt_phys) { vmm_unmap_page_nofree(rsdt_virt); goto fallback_shutdown; }
 
    //map FADT
    uint32_t fadt_page = fadt_phys & ~(PAGE_SIZE - 1);
    uint32_t fadt_offset = fadt_phys & (PAGE_SIZE - 1);
    uint32_t fadt_virt = 0x600000;
    if (vmm_map_page(fadt_virt, fadt_page, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        vmm_unmap_page_nofree(rsdt_virt);
        goto fallback_shutdown;
    }
    fadt_t *fadt = (fadt_t*)(fadt_virt + fadt_offset);
 
    uint32_t pm1a_cnt = fadt->pm1a_cnt_blk;
    if (!pm1a_cnt) {
        vmm_unmap_page_nofree(fadt_virt);
        vmm_unmap_page_nofree(rsdt_virt);
        goto fallback_shutdown;
    }
    serial_printf("PM1a control register: 0x%x\n", pm1a_cnt);
 
    //determine S5 type from DSDT
    uint16_t slp_typ = 5;
    if (fadt->dsdt) {
        uint32_t dsdt_phys = fadt->dsdt;
        uint32_t dsdt_page = dsdt_phys & ~(PAGE_SIZE - 1);
        uint32_t dsdt_offset = dsdt_phys & (PAGE_SIZE - 1);
        uint32_t dsdt_virt = 0x700000;
        if (vmm_map_page(dsdt_virt, dsdt_page, PAGE_PRESENT | PAGE_WRITABLE) == 0) {
            acpi_table_header_t *dsdt = (acpi_table_header_t*)(dsdt_virt + dsdt_offset);
            slp_typ = find_s5_sleep_type(dsdt);
            vmm_unmap_page_nofree(dsdt_virt);
        }
    }
    serial_printf("S5 sleep type: %d\n", slp_typ);
 
    //enable ACPI if necessary
    if (fadt->smi_cmd && fadt->acpi_enable) {
        serial_printf("Enabling ACPI via SMI_CMD=0x%x\n", fadt->smi_cmd);
        uint16_t pm1a_sts = inw(pm1a_cnt);
        if (!(pm1a_sts & SCI_EN)) {
            outb(fadt->smi_cmd, fadt->acpi_enable);
            for (int i = 0; i < 100; i++) {
                if (inw(pm1a_cnt) & SCI_EN) break;
                for (volatile int j = 0; j < 10000; j++);
            }
        }
    }
 
    //trigger shutdown via PM1 control
    uint16_t pm1a_val = inw(pm1a_cnt);
    serial_printf("PM1a original value: 0x%x\n", pm1a_val);
    pm1a_val &= ~(7 << 10); //clear SLP_TYP
    pm1a_val |= (slp_typ << 10) | SLP_EN;
    serial_printf("PM1a shutdown value: 0x%x\n", pm1a_val);
 
    asm volatile("cli");
    if (fadt->pm1b_cnt_blk) {
        uint16_t pm1b_val = inw(fadt->pm1b_cnt_blk);
        pm1b_val &= ~(7 << 10);
        pm1b_val |= (slp_typ << 10) | SLP_EN;
        outw(fadt->pm1b_cnt_blk, pm1b_val);
    }
    outw(pm1a_cnt, pm1a_val);
 
    if (pm1a_cnt == 0x604) {
        serial_printf("Trying QEMU ACPI shutdown with 0x2000\n");
        outw(0x604, 0x2000);
    }
 
    for (volatile int i = 0; i < 1000; i++);
    vmm_unmap_page_nofree(fadt_virt);
    vmm_unmap_page_nofree(rsdt_virt);
    serial_printf("ACPI shutdown initiated\n");
    for(;;) { asm volatile("hlt"); }
 
fallback_shutdown:
    DEBUG_PRINT("ACPI shutdown failed, trying fallbacks");
    outw(0x604, 0x2000);  //QEMU
    outw(0xB004, 0x2000); //bochs
    outb(0xF4, 0x00);     //isa-debug-exit
    for(;;) { asm volatile("hlt"); }
}
