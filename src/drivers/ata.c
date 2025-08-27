#include "ata.h"
#include "../io.h"
#include "../device_manager.h"
#include "serial.h"
#include <stdint.h>
#include <string.h>

static void ata_debug(const char* msg) {
    serial_write_string("[ATA] ");
    serial_write_string(msg);
    serial_write_string("\n");
}

static void ata_debug_hex(const char* msg, uint32_t value) {
    serial_write_string("[ATA] ");
    serial_write_string(msg);
    serial_write_string(": 0x");
    char hex[9];
    for (int i = 7; i >= 0; i--) {
        hex[7-i] = "0123456789ABCDEF"[(value >> (i*4)) & 0xF];
    }
    hex[8] = '\0';
    serial_write_string(hex);
    serial_write_string("\n");
}

//forward declarations
static int ata_device_init(device_t* device);
int ata_device_read(device_t* device, uint32_t offset, void* buffer, uint32_t size);
int ata_device_write(device_t* device, uint32_t offset, const void* buffer, uint32_t size);
int ata_device_ioctl(device_t* device, uint32_t cmd, void* arg);
void ata_device_cleanup(device_t* device);

//device operations for ATA
static const device_ops_t ata_ops = {
    .init = ata_device_init,
    .read = ata_device_read,
    .write = ata_device_write,
    .ioctl = ata_device_ioctl,
    .cleanup = ata_device_cleanup
};

static device_t ata_devices[4];
static ata_device_data_t ata_device_data[4];
static int ata_drive_count = 0;

void ata_init(void) {
    //idk might ad something here laer
}

int ata_wait_bsy(ata_device_data_t* data) {
    int timeout = 100000;
    uint8_t status;
    ata_debug("Waiting for BSY to clear...");
    while (((status = inb(data->data_port + 7)) & ATA_STATUS_BSY) && timeout > 0) {
        timeout--;
    }
    ata_debug_hex("Final status after BSY wait", status);
    if (timeout <= 0) {
        ata_debug("BSY wait TIMEOUT!");
        return -1;
    }
    ata_debug("BSY cleared successfully");
    return 0;
}

int ata_wait_drq(ata_device_data_t* data) {
    int timeout = 100000;
    uint8_t status;
    ata_debug("Waiting for DRQ to be set...");
    while (!((status = inb(data->data_port + 7)) & ATA_STATUS_DRQ) && timeout > 0) {
        timeout--;
    }
    ata_debug_hex("Final status after DRQ wait", status);
    if (timeout <= 0) {
        ata_debug("DRQ wait TIMEOUT!");
        return -1;
    }
    ata_debug("DRQ set successfully");
    return 0;
}

static int ata_device_init(device_t* device) {
    ata_device_data_t* data = (ata_device_data_t*)device->private_data;

    ata_debug("Initializing ATA device...");
    ata_debug_hex("Data port", data->data_port);
    ata_debug_hex("Control port", data->control_port);
    ata_debug_hex("Drive select", data->drive_select);
    
    outb(data->data_port + 6, data->drive_select); //select drive
    ata_debug("Drive selected");
    
    //wait for drive to be ready
    if (ata_wait_bsy(data) != 0) {
        ata_debug("Device init failed - BSY timeout");
        return -1; //no drive present or timeout
    }

    //send IDENTIFY command
    ata_debug("Sending IDENTIFY command");
    outb(data->data_port + 7, ATA_CMD_IDENTIFY);

    //check if drive responds
    uint8_t status = inb(data->data_port + 7);
    ata_debug_hex("Status after IDENTIFY", status);
    if (status == 0) {
        ata_debug("Drive does not exist (status = 0)");
        return -1; //drive does not exist
    }

    //poll for BSY to clear
    while(inb(data->data_port + 7) & ATA_STATUS_BSY) {}

    //check for non-zero LBA mid/high regs indicates non-ATAPI device
    uint8_t lba_mid = inb(data->data_port + 4);
    uint8_t lba_high = inb(data->data_port + 5);
    ata_debug_hex("LBA mid", lba_mid);
    ata_debug_hex("LBA high", lba_high);
    if (lba_mid != 0 || lba_high != 0) {
        ata_debug("Not an ATA drive (non-zero LBA mid/high)");
        return -1; //not an ATA drive
    }

    //poll for DRQ or ERR
    ata_debug("Polling for DRQ or ERR...");
    while(((status = inb(data->data_port + 7)) & (ATA_STATUS_DRQ | ATA_STATUS_ERR)) == 0) {}
    ata_debug_hex("Final status after DRQ/ERR poll", status);

    if (status & ATA_STATUS_ERR) {
        ata_debug("Drive error during IDENTIFY");
        return -1; //drive error
    }

    //read and discard identify data
    ata_debug("Reading IDENTIFY data...");
    for (int i = 0; i < 256; i++) {
        inw(data->data_port);
    }

    ata_debug("Device initialization successful");
    return 0; //success
}

int ata_read_sectors(device_t* device, uint32_t lba, uint8_t sector_count, uint16_t* buffer) {
    ata_device_data_t* data = (ata_device_data_t*)device->private_data;

    ata_debug("Starting sector read...");
    ata_debug_hex("LBA", lba);
    ata_debug_hex("Sector count", sector_count);
    ata_debug_hex("Data port", data->data_port);
    ata_debug_hex("Drive select", data->drive_select);

    //select drive and wait 400ns
    uint8_t drive_head = data->drive_select | ((lba >> 24) & 0x0F);
    ata_debug_hex("Drive/head register", drive_head);
    outb(data->data_port + 6, drive_head);
    sleep_400ns(data->control_port);
    ata_debug("Drive selected, 400ns delay complete");

    //wait for drive to be ready
    if (ata_wait_bsy(data) != 0) {
        ata_debug("Read failed - BSY timeout before command");
        return -1;
    }

    ata_debug("Setting up read command registers...");
    outb(data->data_port + 2, sector_count);
    outb(data->data_port + 3, (uint8_t)lba);
    outb(data->data_port + 4, (uint8_t)(lba >> 8));
    outb(data->data_port + 5, (uint8_t)(lba >> 16));
    ata_debug("Sending READ SECTORS command");
    outb(data->data_port + 7, ATA_CMD_READ_SECTORS);

    for (int s = 0; s < sector_count; s++) {
        ata_debug_hex("Reading sector", s);
        if (ata_wait_bsy(data) != 0) {
            ata_debug("Read failed - BSY timeout during sector read");
            return -1; //wait for BSY to clear
        }
        if (ata_wait_drq(data) != 0) {
            ata_debug("Read failed - DRQ timeout during sector read");
            return -1;
        }
        uint8_t status = inb(data->data_port + 7);
        if (status & ATA_STATUS_ERR) {
            ata_debug_hex("Read failed - error status", status);
            return -1;
        }

        ata_debug("Reading 512 bytes from data port...");
        for (int i = 0; i < 256; i++) {
            buffer[s * 256 + i] = inw(data->data_port);
        }
        ata_debug("Sector read complete");
    }
    ata_debug("All sectors read successfully");
    return 0;
}

int ata_write_sectors(device_t* device, uint32_t lba, uint8_t sector_count, const uint16_t* buffer) {
    ata_device_data_t* data = (ata_device_data_t*)device->private_data;

    // Select drive and wait 400ns
    outb(data->data_port + 6, data->drive_select | ((lba >> 24) & 0x0F));
    sleep_400ns(data->control_port);

    // Wait for drive to be ready
    if (ata_wait_bsy(data) != 0) return -1;

    outb(data->data_port + 2, sector_count);
    outb(data->data_port + 3, (uint8_t)lba);
    outb(data->data_port + 4, (uint8_t)(lba >> 8));
    outb(data->data_port + 5, (uint8_t)(lba >> 16));
    outb(data->data_port + 7, ATA_CMD_WRITE_SECTORS);

    for (int s = 0; s < sector_count; s++) {
        if (ata_wait_drq(data) != 0) return -1;
        if (inb(data->data_port + 7) & ATA_STATUS_ERR) return -1;

        for (int i = 0; i < 256; i++) {
            outw(data->data_port, buffer[s * 256 + i]);
        }
    }
    return 0;
}

int ata_device_read(device_t* device, uint32_t offset, void* buffer, uint32_t size) {
    uint32_t lba = offset / 512;
    uint8_t sector_count = (size + 511) / 512;
    ata_debug("Device read request:");
    ata_debug_hex("Offset", offset);
    ata_debug_hex("Size", size);
    ata_debug_hex("Calculated LBA", lba);
    ata_debug_hex("Calculated sector count", sector_count);
    
    if (ata_read_sectors(device, lba, sector_count, (uint16_t*)buffer) != 0) {
        ata_debug("ata_read_sectors failed");
        return -1;
    }
    ata_debug_hex("Device read successful, returning bytes", size);
    return size; //return bytes read
}

int ata_device_write(device_t* device, uint32_t offset, const void* buffer, uint32_t size) {
    uint32_t lba = offset / 512;
    uint8_t sector_count = (size + 511) / 512;
    if (ata_write_sectors(device, lba, sector_count, (const uint16_t*)buffer) != 0) {
        return -1;
    }
    return size; //return bytes written
}

int ata_device_ioctl(device_t* device, uint32_t cmd, void* arg) {
    (void)device; (void)cmd; (void)arg;
    return -1;
}

void ata_device_cleanup(device_t* device) {
    if (device && device->private_data) {
        //nothing yet theres no memory managment
    }
}

void ata_probe_and_register(void) {
    uint16_t ports[] = {ATA_PRIMARY_DATA, ATA_SECONDARY_DATA};
    uint16_t ctrl_ports[] = {ATA_PRIMARY_CONTROL, ATA_SECONDARY_CONTROL};

    for (int i = 0; i < 2; i++) { //loop through primary and secondary controllers
        for (int j = 0; j < 2; j++) { //loop through master and slave
            if (ata_drive_count >= 4) return;

            device_t* dev = &ata_devices[ata_drive_count];
            ata_device_data_t* data = &ata_device_data[ata_drive_count];

            //setup device data
            data->data_port = ports[i];
            data->control_port = ctrl_ports[i];
            data->is_slave = j;
            data->drive_select = j ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER;
            
            dev->private_data = data;

            //try to initialize the device
            if (ata_device_init(dev) == 0) {
                //device found configure and register it
                strcpy(dev->name, "ata");
                char num_str[2];
                itoa(ata_drive_count, num_str);
                strcat(dev->name, num_str);
                
                dev->type = DEVICE_TYPE_STORAGE;
                dev->status = DEVICE_STATUS_UNINITIALIZED; //will be set to READY by devicd manager
                dev->ops = &ata_ops;
                dev->next = NULL;

                if (device_register(dev) == 0) {
                    device_init(dev); //finalize initialization through device manager
                    ata_drive_count++;
                }
            }
        }
    }
}
