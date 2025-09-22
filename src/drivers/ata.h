#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include "../device_manager.h"
#include "../io.h"


//ATA I/O ports
#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_SECTOR_COUNT 0x1F2
#define ATA_PRIMARY_LBA_LOW     0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HIGH    0x1F5
#define ATA_PRIMARY_DRIVE       0x1F6
#define ATA_PRIMARY_COMMAND     0x1F7
#define ATA_PRIMARY_CONTROL     0x3F6

#define ATA_SECONDARY_DATA      0x170
#define ATA_SECONDARY_ERROR     0x171
#define ATA_SECONDARY_SECTOR_COUNT 0x172
#define ATA_SECONDARY_LBA_LOW   0x173
#define ATA_SECONDARY_LBA_MID   0x174
#define ATA_SECONDARY_LBA_HIGH  0x175
#define ATA_SECONDARY_DRIVE     0x176
#define ATA_SECONDARY_COMMAND   0x177
#define ATA_SECONDARY_CONTROL   0x376

//ATA commands
#define ATA_CMD_READ_SECTORS    0x20
#define ATA_CMD_WRITE_SECTORS   0x30
#define ATA_CMD_CACHE_FLUSH   0xE7
#define ATA_CMD_IDENTIFY        0xEC

//ATA status bits
#define ATA_STATUS_BSY          0x80  //busy
#define ATA_STATUS_DRDY         0x40  //drive ready
#define ATA_STATUS_DRQ          0x08  //data request
#define ATA_STATUS_ERR          0x01  //error

//ATA drive selection bits (for drive select register)
#define ATA_DRIVE_MASTER    0xE0
#define ATA_DRIVE_SLAVE     0xF0

//ATA device-specific data stuff
typedef struct {
    uint16_t data_port;
    uint16_t control_port;
    uint8_t drive_select; //ATA_DRIVE_MASTER or ATA_DRIVE_SLAVE
    int is_slave;
} ata_device_data_t;

//function declarations
void ata_init(void);
void ata_probe_and_register(void);
void ata_rescan_partitions(void);

//ATA requires a 400ns delay after drive selection
static inline void sleep_400ns(uint16_t control_port) {
    inb(control_port);
    inb(control_port);
    inb(control_port);
    inb(control_port);
}
int ata_device_read(device_t* device, uint32_t offset, void* buffer, uint32_t size);
int ata_device_write(device_t* device, uint32_t offset, const void* buffer, uint32_t size);
int ata_device_ioctl(device_t* device, uint32_t cmd, void* arg);
void ata_device_cleanup(device_t* device);

//internal functions
int ata_wait_bsy(ata_device_data_t* data);
int ata_wait_drq(ata_device_data_t* data);
int ata_read_sectors(device_t* device, uint32_t lba, uint8_t sector_count, uint16_t* buffer);
int ata_write_sectors(device_t* device, uint32_t lba, uint8_t sector_count, const uint16_t* buffer);

#endif
