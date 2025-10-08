#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>
#include "../device_manager.h"

//AHCI HBA (host bus adapter) memory registers
typedef volatile struct {
    //generic Host Control
    uint32_t cap;        //0x00 - host capabilities
    uint32_t ghc;        //0x04 - global host control
    uint32_t is;         //0x08 - interrupt status
    uint32_t pi;         //0x0C - ports implemented
    uint32_t vs;         //0x10 - version
    uint32_t ccc_ctl;    //0x14 - command completion coalescing control
    uint32_t ccc_ports;  //0x18 - command completion coalescing ports
    uint32_t em_loc;     //0x1C - enclosure management location
    uint32_t em_ctl;     //0x20 - enclosure management control
    uint32_t cap2;       //0x24 - host capabilities extended
    uint32_t bohc;       //0x28 - BIOS/OS handoff control and status
    uint8_t reserved[0xA0 - 0x2C];
    uint8_t vendor[0x100 - 0xA0];
} ahci_hba_mem_t;

//AHCI Port registers
typedef volatile struct {
    uint32_t clb;        //0x00 - command list base address
    uint32_t clbu;       //0x04 - command list base address upper
    uint32_t fb;         //0x08 - FIS base address
    uint32_t fbu;        //0x0C - FIS base address upper
    uint32_t is;         //0x10 - interrupt status
    uint32_t ie;         //0x14 - interrupt enable
    uint32_t cmd;        //0x18 - command and status
    uint32_t reserved0;  //0x1C
    uint32_t tfd;        //0x20 - task file data
    uint32_t sig;        //0x24 - signature
    uint32_t ssts;       //0x28 - SATA status (SCR0: SStatus)
    uint32_t sctl;       //0x2C - SATA control (SCR2: SControl)
    uint32_t serr;       //0x30 - SATA error (SCR1: SError)
    uint32_t sact;       //0x34 - SATA active (SCR3: SActive)
    uint32_t ci;         //0x38 - command issue
    uint32_t sntf;       //0x3C - SATA notification (SCR4: SNotification)
    uint32_t fbs;        //0x40 - FIS-based switching control
    uint32_t reserved1[11];
    uint32_t vendor[4];
} ahci_hba_port_t;

//command header
typedef struct {
    uint8_t  cfl:5;      //command FIS length in DWORDS
    uint8_t  a:1;        //ATAPI
    uint8_t  w:1;        //write
    uint8_t  p:1;        //prefetchable
    uint8_t  r:1;        //reset
    uint8_t  b:1;        //BIST
    uint8_t  c:1;        //clear busy upon R_OK
    uint8_t  reserved0:1;
    uint8_t  pmp:4;      //port multiplier port
    uint16_t prdtl;      //physical region descriptor table length
    uint32_t prdbc;      //physical region descriptor byte count
    uint32_t ctba;       //command table descriptor base address
    uint32_t ctbau;      //command table descriptor base address upper
    uint32_t reserved1[4];
} __attribute__((packed)) ahci_cmd_header_t;

//physical region descriptor table entry
typedef struct {
    uint32_t dba;        //data base address
    uint32_t dbau;       //data base address upper
    uint32_t reserved0;
    uint32_t dbc:22;     //byte count (0-based, max 4MB)
    uint32_t reserved1:9;
    uint32_t i:1;        //interrupt on completion
} __attribute__((packed)) ahci_prdt_entry_t;

//command table
typedef struct {
    uint8_t  cfis[64];   //command FIS
    uint8_t  acmd[16];   //ATAPI command
    uint8_t  reserved[48];
    ahci_prdt_entry_t prdt_entry[1]; //physical region descriptor table (we'll allocate more)
} __attribute__((packed)) ahci_cmd_table_t;

//received FIS structure
typedef struct {
    uint8_t  dsfis[0x20];  //DMA setup FIS
    uint8_t  reserved0[4];
    uint8_t  psfis[0x20];  //PIO setup FIS
    uint8_t  reserved1[12];
    uint8_t  rfis[0x18];   //D2H register FIS
    uint8_t  reserved2[4];
    uint8_t  sdbfis[8];    //set device bits FIS
    uint8_t  ufis[64];     //unknown FIS
    uint8_t  reserved3[96];
} __attribute__((packed)) ahci_received_fis_t;

//FIS types
#define FIS_TYPE_REG_H2D   0x27  //register FIS - host to device
#define FIS_TYPE_REG_D2H   0x34  //register FIS - device to host
#define FIS_TYPE_DMA_ACT   0x39  //DMA activate FIS - device to host
#define FIS_TYPE_DMA_SETUP 0x41  //DMA setup FIS - bidirectional
#define FIS_TYPE_DATA      0x46  //data FIS - bidirectional
#define FIS_TYPE_BIST      0x58  //BIST activate FIS - bidirectional
#define FIS_TYPE_PIO_SETUP 0x5F  //PIO setup FIS - device to host
#define FIS_TYPE_DEV_BITS  0xA1  //set device bits FIS - device to host

//register host to device FIS
typedef struct {
    uint8_t  fis_type;   //FIS_TYPE_REG_H2D
    uint8_t  pmport:4;   //port multiplier
    uint8_t  reserved0:3;
    uint8_t  c:1;        //1: command 0: control
    uint8_t  command;    //command register
    uint8_t  featurel;   //feature register low byte
    uint8_t  lba0;       //LBA low byte
    uint8_t  lba1;       //LBA mid byte
    uint8_t  lba2;       //LBA high byte
    uint8_t  device;     //device register
    uint8_t  lba3;       //LBA 3rd byte
    uint8_t  lba4;       //LBA 4th byte
    uint8_t  lba5;       //LBA 5th byte
    uint8_t  featureh;   //feature register high byte
    uint8_t  countl;     //count register low byte
    uint8_t  counth;     //count register high byte
    uint8_t  icc;        //isochronous command completion
    uint8_t  control;    //control register
    uint8_t  reserved1[4];
} __attribute__((packed)) fis_reg_h2d_t;

//ATA commands
#define ATA_CMD_READ_DMA_EX     0x25
#define ATA_CMD_WRITE_DMA_EX    0x35
#define ATA_CMD_FLUSH_CACHE_EXT 0xEA
#define ATA_CMD_IDENTIFY        0xEC

//port command and status bits
#define HBA_PxCMD_ST    0x0001
#define HBA_PxCMD_FRE   0x0010
#define HBA_PxCMD_FR    0x4000
#define HBA_PxCMD_CR    0x8000

//port signature types
#define SATA_SIG_ATA    0x00000101  //SATA drive
#define SATA_SIG_ATAPI  0xEB140101  //SATAPI drive
#define SATA_SIG_SEMB   0xC33C0101  //enclosure management bridge
#define SATA_SIG_PM     0x96690101  //port multiplier

//device types
#define AHCI_DEV_NULL   0
#define AHCI_DEV_SATA   1
#define AHCI_DEV_SATAPI 2
#define AHCI_DEV_SEMB   3
#define AHCI_DEV_PM     4

//HBA capabilities
#define HBA_CAP_S64A    (1 << 31)  //supports 64-bit addressing

//global HBA control
#define HBA_GHC_AHCI_ENABLE (1 << 31)
#define HBA_GHC_RESET       (1 << 0)

//port SATA status
#define HBA_PxSSTS_DET_PRESENT 3

//function declarations
void ahci_init(void);
int ahci_probe_and_register(void);
int ahci_read_sectors(device_t* device, uint32_t offset, void* buffer, uint32_t size);
int ahci_write_sectors(device_t* device, uint32_t offset, const void* buffer, uint32_t size);
void ahci_rescan_partitions(void);

#endif
