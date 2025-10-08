#include "fs.h"
#include "../drivers/serial.h"
#include "../debug.h"
#include "fat16_vfs.h"
#include "fat32.h"
#include "fat32_vfs.h"
#include "devfs.h"
#include "procfs.h"
#include "tmpfs.h"

static void fs_debug(const char* msg) {
#if (LOG_VFS) || (DEBUG_ENABLED)
    serial_write_string("[FS] ");
    serial_write_string(msg);
    serial_write_string("\n");
#else
    (void)msg;
#endif
}

int fs_init(filesystem_t* fs, device_t* device) {
    if (!fs || !device) return -1;

    fs->device = device;

    //read boot sector manually first
    uint8_t boot_sector[512];
    if (device_read(device, 0, boot_sector, 512) != 512) {
        fs_debug("Failed to read boot sector");
        return -1;
    }

    //check boot signature
    if (boot_sector[510] != 0x55 || boot_sector[511] != 0xAA) {
        fs_debug("Invalid boot signature");
        return -1;
    }

    //check if FAT32 or FAT16 by examining root_entry_count
    //FAT32 has root_entry_count == 0, FAT16 has it > 0
    uint16_t root_entries = (uint16_t)boot_sector[17] | ((uint16_t)boot_sector[18] << 8);
    uint16_t fat_size_16 = (uint16_t)boot_sector[22] | ((uint16_t)boot_sector[23] << 8);
    
    if (root_entries == 0 && fat_size_16 == 0) {
        //likely FAT32 (root_entries=0 and fat_size_16=0 are FAT32 indicators)
        fs_debug("Detected FAT32 filesystem");
        void* mount_data = NULL;
        if (fat32_mount(device, &mount_data) == 0) {
            fs->type = FS_TYPE_FAT32;
            fs->fs_data.fat32_mount = mount_data;
            fs_debug("FAT32 filesystem mounted");
            return 0;
        }
        fs_debug("FAT32 mount failed");
    } else {
        //try FAT16
        fs_debug("Attempting FAT16 detection");
        if (fat16_init(&fs->fs_data.fat16, device) == 0) {
            fs->type = FS_TYPE_FAT16;
            fs_debug("FAT16 filesystem mounted");
            return 0;
        }
        fs_debug("FAT16 mount failed");
    }

    fs->type = FS_TYPE_NONE;
    fs_debug("No supported filesystem found");
    return -1;
}

int fs_open(filesystem_t* fs, const char* filename, fat16_file_t* file) {
    switch (fs->type) {
        case FS_TYPE_FAT16:
            return fat16_open_file(&fs->fs_data.fat16, file, filename);
        default:
            return -1;
    }
}

int fs_read(fat16_file_t* file, void* buffer, uint32_t size) {
    return fat16_read_file(file, buffer, size);
}

int fs_close(fat16_file_t* file) {
    return fat16_close_file(file);
}

int fs_list_directory(filesystem_t* fs) {
    switch (fs->type) {
        case FS_TYPE_FAT16:
            return fat16_list_directory(&fs->fs_data.fat16);
        default:
            return -1;
    }
}

int fs_vfs_init(void) {
    int ok = 0;
    //register FAT16 filesystem with VFS
    if (vfs_register_fs("fat16", &fat16_vfs_ops) == 0) {
        fs_debug("FAT16 registered with VFS");
        ok = 1;
    } else {
        fs_debug("Failed to register FAT16 with VFS");
    }
    
    //initialize and register FAT32
    if (fat32_init() == 0) {
        fs_debug("FAT32 initialized");
        if (vfs_register_fs("fat32", &fat32_vfs_ops) == 0) {
            fs_debug("FAT32 registered with VFS");
            ok = 1;
        } else {
            fs_debug("Failed to register FAT32 with VFS");
        }
    } else {
        fs_debug("Failed to initialize FAT32");
    }

    //register DevFS
    if (vfs_register_fs("devfs", &devfs_ops) == 0) {
        fs_debug("DevFS registered with VFS");
        ok = 1;
    } else {
        fs_debug("Failed to register DevFS with VFS");
    }

    //register ProcFS
    if (vfs_register_fs("procfs", &procfs_ops) == 0) {
        fs_debug("ProcFS registered with VFS");
        ok = 1;
    } else {
        fs_debug("Failed to register ProcFS with VFS");
    }

    //initialize and register TmpFS
    if (tmpfs_init() == 0) {
        fs_debug("TmpFS initialized");
        //tmpfs uses a different registration model (direct mount)
        ok = 1;
    } else {
        fs_debug("Failed to initialize TmpFS");
    }

    return ok ? 0 : -1;
}
