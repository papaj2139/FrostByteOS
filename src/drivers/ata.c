#include "ata.h"
#include "../io.h"
#include "../device_manager.h"
#include "../debug.h"
#include "serial.h"
#include <stdint.h>
#include <string.h>
#include "../kernel/uaccess.h"
#include "../libc/string.h"
#include "ata.h"

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

//partition device support (primary MBR partitions only)
typedef struct {
    device_t* base;       //underlying ATA device
    uint32_t start_lba;   //starting LBA
    uint32_t sectors;     //number of sectors
} ata_part_priv_t;

static device_t ata_devices[4];
static ata_device_data_t ata_device_data[4];
static int ata_drive_count = 0;
static device_t ata_part_devices[16];
static ata_part_priv_t ata_part_privs[16];
static int ata_part_count = 0;


#if LOG_ATA
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
#else
static void ata_debug(const char* msg) { (void)msg; }
static void ata_debug_hex(const char* msg, uint32_t value) { (void)msg; (void)value; }
#endif

static int ata_part_init(device_t* d) {
    (void)d;
    return 0;
}

static int ata_part_read(device_t* d, uint32_t offset, void* buffer, uint32_t size) {
    ata_part_priv_t* pp = (ata_part_priv_t*)d->private_data;
    if (!pp || !pp->base) return -1;
    uint64_t part_bytes = (uint64_t)pp->sectors * 512ull;
    if ((uint64_t)offset + (uint64_t)size > part_bytes) return -1;
    uint32_t abs_off = (uint32_t)(pp->start_lba * 512ull + (uint64_t)offset);
    return device_read(pp->base, abs_off, buffer, size);
}
static int ata_part_write(device_t* d, uint32_t offset, const void* buffer, uint32_t size) {
    ata_part_priv_t* pp = (ata_part_priv_t*)d->private_data;
    if (!pp || !pp->base) return -1;
    uint64_t part_bytes = (uint64_t)pp->sectors * 512ull;
    if ((uint64_t)offset + (uint64_t)size > part_bytes) return -1;
    uint32_t abs_off = (uint32_t)(pp->start_lba * 512ull + (uint64_t)offset);
    return device_write(pp->base, abs_off, buffer, size);
}
static int ata_part_ioctl(device_t* d, uint32_t cmd, void* arg) {
    if (!d) return -1;
    if (cmd == IOCTL_BLK_GET_INFO) {
        typedef struct {
            uint32_t sector_size;
            uint32_t sector_count;
        } blkdev_info_t;
        if (!arg) return -1;
        ata_part_priv_t* pp = (ata_part_priv_t*)d->private_data;
        if (!pp) return -1;
        blkdev_info_t info; info.sector_size = 512u; info.sector_count = pp->sectors;
        //copy to user
        if (copy_to_user(arg, &info, sizeof(info)) != 0) return -1;
        return 0;
    }
    return -1;
}
static void ata_part_cleanup(device_t* d) {
    (void)d;
}

static const device_ops_t ata_part_ops = {
    .init = ata_part_init,
    .read = ata_part_read,
    .write = ata_part_write,
    .ioctl = ata_part_ioctl,
    .cleanup = ata_part_cleanup
};

static void ata_register_partitions(device_t* base_dev, int drive_no) {
    if (ata_part_count >= (int)(sizeof(ata_part_devices)/sizeof(ata_part_devices[0]))) return;
    //read MBR sector
    uint8_t mbr[512];
    if (device_read(base_dev, 0, mbr, 512) != 512) return;
    if (!(mbr[510] == 0x55 && mbr[511] == 0xAA)) return;
    //partition table at 446
    for (int i = 0; i < 4 && ata_part_count < 16; i++) {
        uint8_t* e = &mbr[446 + i * 16];
        uint8_t ptype = e[4];
        uint32_t lba_start = (uint32_t)e[8] | ((uint32_t)e[9] << 8) | ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        uint32_t sectors = (uint32_t)e[12] | ((uint32_t)e[13] << 8) | ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
        if (ptype == 0 || sectors == 0) continue;
        //create partition device
        device_t* pd = &ata_part_devices[ata_part_count];
        ata_part_priv_t* pp = &ata_part_privs[ata_part_count];
        memset(pd, 0, sizeof(*pd));
        memset(pp, 0, sizeof(*pp));
        pp->base = base_dev;
        pp->start_lba = lba_start;
        pp->sectors = sectors;
        pd->private_data = pp;
        //name: ata<drive_no>p<idx+1>
        strcpy(pd->name, "ata");
        char ns[4]; itoa(drive_no, ns); strcat(pd->name, ns); strcat(pd->name, "p");
        char ps[3]; itoa(i+1, ps); strcat(pd->name, ps);
        pd->type = DEVICE_TYPE_STORAGE;
        pd->subtype = DEVICE_SUBTYPE_STORAGE_ATA;
        pd->status = DEVICE_STATUS_UNINITIALIZED;
        pd->ops = &ata_part_ops;
        if (device_register(pd) == 0) {
            device_init(pd);
            pd->status = DEVICE_STATUS_READY;
            ata_part_count++;
        }
    }
}

//query helper for /proc/partitions exposure
int ata_query_device_info(device_t* dev, uint64_t* start_lba, uint64_t* sector_count, int* is_partition) {
    if (!dev || !start_lba || !sector_count || !is_partition) return -1;
    if (dev->subtype != DEVICE_SUBTYPE_STORAGE_ATA) return -1;
    //partition device if ops match ata_part_ops
    if (dev->ops == &ata_part_ops) {
        ata_part_priv_t* pp = (ata_part_priv_t*)dev->private_data;
        if (!pp || !pp->base) return -1;
        *start_lba = pp->start_lba;
        *sector_count = pp->sectors;
        *is_partition = 1;
        return 0;
    }
    //whole disk device
    ata_device_data_t* data = (ata_device_data_t*)dev->private_data;
    if (!data) return -1;
    *start_lba = 0;
    *sector_count = data->total_sectors;
    *is_partition = 0;
    return 0;
}

void ata_rescan_partitions(void) {
    //unregister existing partition devices
    for (int i = 0; i < ata_part_count; i++) {
        device_t* pd = &ata_part_devices[i];
        if (pd->status == DEVICE_STATUS_READY) {
            device_unregister(pd->device_id);
            memset(pd, 0, sizeof(*pd));
            memset(&ata_part_privs[i], 0, sizeof(ata_part_priv_t));
        }
    }
    ata_part_count = 0;

    //rescan partitions for existing drives
    for (int i = 0; i < ata_drive_count; i++) {
        device_t* dev = &ata_devices[i];
        if (dev->status == DEVICE_STATUS_READY) {
            ata_register_partitions(dev, i);
        }
    }
}

void ata_init(void) {
    //idk might ad something here laer
}

int ata_wait_bsy(ata_device_data_t* data) {
    int timeout = 100000;
    uint8_t status;
    #if LOG_ATA
    ata_debug("Waiting for BSY to clear...");
    #endif
    while (((status = inb(data->data_port + 7)) & ATA_STATUS_BSY) && timeout > 0) {
        timeout--;
    }
    #if LOG_ATA
    ata_debug_hex("Final status after BSY wait", status);
    #endif
    if (timeout <= 0) {
        #if LOG_ATA
        ata_debug("BSY wait TIMEOUT!");
        #endif
        return -1;
    }
    #if LOG_ATA
    ata_debug("BSY cleared successfully");
    #endif
    return 0;
}

int ata_wait_drq(ata_device_data_t* data) {
    int timeout = 100000;
    uint8_t status;
    #if LOG_ATA
    ata_debug("Waiting for DRQ to be set...");
    #endif
    while (!((status = inb(data->data_port + 7)) & ATA_STATUS_DRQ) && timeout > 0) {
        timeout--;
    }
    #if LOG_ATA
    ata_debug_hex("Final status after DRQ wait", status);
    #endif
    if (timeout <= 0) {
        #if LOG_ATA
        ata_debug("DRQ wait TIMEOUT!");
        #endif
        return -1;
    }
    #if LOG_ATA
    ata_debug("DRQ set successfully");
    #endif
    return 0;
}

static int ata_device_init(device_t* device) {
    ata_device_data_t* data = (ata_device_data_t*)device->private_data;
    #if LOG_ATA
    ata_debug("Initializing ATA device...");
    ata_debug_hex("Data port", data->data_port);
    ata_debug_hex("Control port", data->control_port);
    ata_debug_hex("Drive select", data->drive_select);
    #endif
    outb(data->data_port + 6, data->drive_select); //select drive
    #if LOG_ATA
    ata_debug("Drive selected");
    #endif

    //wait for drive to be ready
    if (ata_wait_bsy(data) != 0) {
        #if LOG_ATA
        ata_debug("Device init failed - BSY timeout");
        #endif
        return -1; //no drive present or timeout
    }

    //send IDENTIFY command
    #if LOG_ATA
    ata_debug("Sending IDENTIFY command");
    #endif
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

    //read IDENTIFY data (256 words)
    ata_debug("Reading IDENTIFY data...");
    uint16_t id[256];
    for (int i = 0; i < 256; i++) {
        id[i] = inw(data->data_port);
    }
    //parse 28-bit LBA sector count from words 60-61 (little-endian word order)
    uint32_t lba28 = ((uint32_t)id[61] << 16) | (uint32_t)id[60];
    data->total_sectors = lba28;
    ata_debug_hex("IDENTIFY LBA28 sectors", data->total_sectors);

    ata_debug("Device initialization successful");
    return 0; //success
}

int ata_read_sectors(device_t* device, uint32_t lba, uint8_t sector_count, uint16_t* buffer) {
    ata_device_data_t* data = (ata_device_data_t*)device->private_data;
    #if LOG_ATA
    ata_debug("Starting sector read...");
    ata_debug_hex("LBA", lba);
    ata_debug_hex("Sector count", sector_count);
    ata_debug_hex("Data port", data->data_port);
    ata_debug_hex("Drive select", data->drive_select);
    #endif

    //select drive and wait 400ns
    uint8_t drive_head = data->drive_select | ((lba >> 24) & 0x0F);
    #if LOG_ATA
    ata_debug_hex("Drive/head register", drive_head);
    #endif
    outb(data->data_port + 6, drive_head);
    sleep_400ns(data->control_port);
    #if LOG_ATA
    ata_debug("Drive selected, 400ns delay complete");
    #endif

    //wait for drive to be ready
    if (ata_wait_bsy(data) != 0) {
        #if LOG_ATA
        ata_debug("Read failed - BSY timeout before command");
        #endif
        return -1;
    }
    #if LOG_ATA
    ata_debug("Setting up read command registers...");
    #endif
    outb(data->data_port + 2, sector_count);
    outb(data->data_port + 3, (uint8_t)lba);
    outb(data->data_port + 4, (uint8_t)(lba >> 8));
    outb(data->data_port + 5, (uint8_t)(lba >> 16));
    #if LOG_ATA
    ata_debug("Sending READ SECTORS command");
    #endif
    outb(data->data_port + 7, ATA_CMD_READ_SECTORS);

    for (int s = 0; s < sector_count; s++) {
        #if LOG_ATA
        ata_debug_hex("Reading sector", s);
        #endif
        if (ata_wait_bsy(data) != 0) {
            #if LOG_ATA
            ata_debug("Read failed - BSY timeout during sector read");
            #endif
            return -1; //wait for BSY to clear
        }
        if (ata_wait_drq(data) != 0) {
            #if LOG_ATA
            ata_debug("Read failed - DRQ timeout during sector read");
            #endif
            return -1;
        }
        uint8_t status = inb(data->data_port + 7);
        if (status & ATA_STATUS_ERR) {
            #if LOG_ATA
            ata_debug_hex("Read failed - error status", status);
            #endif
            return -1;
        }

        #if LOG_ATA
        ata_debug("Reading 512 bytes from data port...");
        #endif
        for (int i = 0; i < 256; i++) {
            buffer[s * 256 + i] = inw(data->data_port);
        }
        #if LOG_ATA
        ata_debug("Sector read complete");
        #endif
    }
    #if LOG_ATA
    ata_debug("All sectors read successfully");
    #endif
    return 0;
}

int ata_write_sectors(device_t* device, uint32_t lba, uint8_t sector_count, const uint16_t* buffer) {
    ata_device_data_t* data = (ata_device_data_t*)device->private_data;
    #if LOG_ATA
    ata_debug("Starting sector write...");
    ata_debug_hex("LBA", lba);
    ata_debug_hex("Sector count", sector_count);
    #endif

    //select drive and wait 400ns
    uint8_t drive_head = data->drive_select | ((lba >> 24) & 0x0F);
    outb(data->data_port + 6, drive_head);
    sleep_400ns(data->control_port);

    //wait for drive to be ready
    if (ata_wait_bsy(data) != 0) {
        #if LOG_ATA
        ata_debug("Write failed - BSY timeout before command");
        #endif
        return -1;
    }

    outb(data->data_port + 2, sector_count);
    outb(data->data_port + 3, (uint8_t)lba);
    outb(data->data_port + 4, (uint8_t)(lba >> 8));
    outb(data->data_port + 5, (uint8_t)(lba >> 16));
    outb(data->data_port + 7, ATA_CMD_WRITE_SECTORS);

    for (int s = 0; s < sector_count; s++) {
        #if LOG_ATA
        ata_debug_hex("Writing sector", s);
        #endif
        if (ata_wait_bsy(data) != 0) {
            #if LOG_ATA
            ata_debug("Write failed - BSY timeout during sector write");
            #endif
            return -1;
        }
        if (ata_wait_drq(data) != 0) {
            #if LOG_ATA
            ata_debug("Write failed - DRQ timeout during sector write");
            #endif
            return -1;
        }
        if (inb(data->data_port + 7) & ATA_STATUS_ERR) {
            #if LOG_ATA
            ata_debug("Write failed - error status");
            #endif
            return -1;
        }
        #if LOG_ATA
        ata_debug("Writing 512 bytes to data port...");
        #endif
        for (int i = 0; i < 256; i++) {
            outw(data->data_port, buffer[s * 256 + i]);
        }
        //after writing the data for a sector, the drive needs a cache flush
        outb(data->data_port + 7, ATA_CMD_CACHE_FLUSH);
        if (ata_wait_bsy(data) != 0) {
            #if LOG_ATA
            ata_debug("Write failed - BSY timeout after cache flush");
            #endif
            return -1;
        }
        #if LOG_ATA
        ata_debug("Sector write complete");
        #endif
    }
    #if LOG_ATA
    ata_debug("All sectors written successfully");
    #endif
    return 0;
}

int ata_device_read(device_t* device, uint32_t offset, void* buffer, uint32_t size) {
    uint32_t lba = offset / 512;
    uint8_t sector_count = (size + 511) / 512;
    #if LOG_ATA
    ata_debug("Device read request:");
    ata_debug_hex("Offset", offset);
    ata_debug_hex("Size", size);
    ata_debug_hex("Calculated LBA", lba);
    ata_debug_hex("Calculated sector count", sector_count);
    #endif
    if (ata_read_sectors(device, lba, sector_count, (uint16_t*)buffer) != 0) {
        #if LOG_ATA
        ata_debug("ata_read_sectors failed");
        #endif
        return -1;
    }
    #if LOG_ATA
    ata_debug_hex("Device read successful, returning bytes", size);
    #endif
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
    if (!device) return -1;
    if (cmd == IOCTL_BLK_GET_INFO) {
        typedef struct {
            uint32_t sector_size;
            uint32_t sector_count;
        } blkdev_info_t;
        if (!arg) return -1;
        ata_device_data_t* data = (ata_device_data_t*)device->private_data;
        if (!data) return -1;
        blkdev_info_t info; info.sector_size = 512u; info.sector_count = data->total_sectors;
        if (copy_to_user(arg, &info, sizeof(info)) != 0) return -1;
        return 0;
    }
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
                int drive_no = ata_drive_count;
                strcpy(dev->name, "ata");
                char num_str[2];
                itoa(drive_no, num_str);
                strcat(dev->name, num_str);

                dev->type = DEVICE_TYPE_STORAGE;
                dev->status = DEVICE_STATUS_UNINITIALIZED; //will be set to READY by devicd manager
                dev->subtype = DEVICE_SUBTYPE_STORAGE_ATA;
                dev->ops = &ata_ops;
                dev->next = NULL;

                if (device_register(dev) == 0) {
                    device_init(dev); //finalize initialization through device manager
                    //scan MBR and register primary partitions
                    ata_register_partitions(dev, drive_no);
                    ata_drive_count++;
                }
            }
        }
    }
}
