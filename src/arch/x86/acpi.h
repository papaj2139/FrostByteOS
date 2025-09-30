#ifndef ARCH_X86_ACPI_H
#define ARCH_X86_ACPI_H

#include <stdint.h>

//signatures and flags
#define RSDP_SIG       "RSD PTR "
#define ACPI_SIG_RSDT  "RSDT"
#define ACPI_SIG_XSDT  "XSDT"
#define ACPI_SIG_FADT  "FACP"
#define ACPI_SIG_DSDT  "DSDT"

#define SLP_EN (1 << 13)
#define SCI_EN (1 << 0)

//perform ACPI shutdown (includes fallbacks for emulators)
void acpi_shutdown(void);
//ACPI types 
typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oemid[6];
    char oemtableid[8];
    uint32_t oemrevision;
    uint32_t creatorid;
    uint32_t creatorrev;
} acpi_table_header_t;

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt_address;   //ACPI v1
    uint32_t length;         //ACPI v2+
    uint64_t xsdt_address;   //ACPI v2+
    uint8_t extended_checksum; //ACPI v2+
    uint8_t reserved[3];
} rsdp_descriptor_t;

//fixed ACPI description table
typedef struct __attribute__((packed)) {
    acpi_table_header_t header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved1;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved2;
    uint32_t flags;
} fadt_t;

#endif // ARCH_X86_ACPI_H
