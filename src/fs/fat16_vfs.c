#include "fat16_vfs.h"
#include "fat16.h"
#include "../drivers/serial.h"
#include "../mm/heap.h"
#include "vfs.h"
#include "fs.h"
#include <stdbool.h>
#include <string.h>

//FAT16 VFS operations
static int fat16_vfs_open(vfs_node_t* node, uint32_t flags);
static int fat16_vfs_close(vfs_node_t* node);
static int fat16_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer);
static int fat16_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer);
static int fat16_vfs_create(vfs_node_t* parent, const char* name, uint32_t flags);
static int fat16_vfs_unlink(vfs_node_t* node);
static int fat16_vfs_mkdir(vfs_node_t* parent, const char* name, uint32_t flags);
static int fat16_vfs_rmdir(vfs_node_t* node);
static int fat16_vfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t** out);
static int fat16_vfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out);
static int fat16_vfs_get_size(vfs_node_t* node);
static int fat16_vfs_ioctl(vfs_node_t* node, uint32_t request, void* arg);

//FAT16 VFS operations structure
vfs_operations_t fat16_vfs_ops = {
    .open = fat16_vfs_open,
    .close = fat16_vfs_close,
    .read = fat16_vfs_read,
    .write = fat16_vfs_write,
    .create = fat16_vfs_create,
    .unlink = fat16_vfs_unlink,
    .mkdir = fat16_vfs_mkdir,
    .rmdir = fat16_vfs_rmdir,
    .readdir = fat16_vfs_readdir,
    .finddir = fat16_vfs_finddir,
    .get_size = fat16_vfs_get_size,
    .ioctl = fat16_vfs_ioctl
};

#define FAT16_DIR_PRIVATE_MAGIC 0xDEADBEEF

//FAT16 file private data
typedef struct {
    fat16_file_t file;
    int is_open;
} fat16_file_private_t;

//FAT16 directory private data
typedef struct {
    uint32_t magic;             //magic number to identify this struct
    fat16_fs_t* fs;
    uint32_t current_sector;
    uint32_t sector_offset;
    uint32_t entries_per_sector;
    uint32_t root_dir_sectors;
    uint32_t current_index; //keep track of current position for readdir
} fat16_dir_private_t;

static int fat16_vfs_open(vfs_node_t* node, uint32_t flags) {
    (void)flags;

    serial_write_string("[FAT16-VFS] Opening node: ");
    serial_write_string(node->name);
    serial_write_string("\n");

    if (node->type == VFS_FILE_TYPE_DIRECTORY) {
        if (node->private_data && ((fat16_dir_private_t*)node->private_data)->magic == FAT16_DIR_PRIVATE_MAGIC) {
            return 0;
        }

        fat16_fs_t* fs = NULL;
        if (node == node->mount->root) {
            filesystem_t* fs_wrapper = (filesystem_t*)node->private_data;
            fs = &fs_wrapper->fs_data.fat16;
        } else {
            if (!node->mount || !node->mount->root || !node->mount->root->private_data) {
                return -1;
            }
            void* pdata = node->mount->root->private_data;
            if (pdata && ((fat16_dir_private_t*)pdata)->magic == FAT16_DIR_PRIVATE_MAGIC) {
                fs = ((fat16_dir_private_t*)pdata)->fs;
            } else {
                fs = &((filesystem_t*)pdata)->fs_data.fat16;
            }
        }

        if (!fs) {
            return -1;
        }

        fat16_dir_private_t* dir_data = (fat16_dir_private_t*)kmalloc(sizeof(fat16_dir_private_t));
        if (!dir_data) {
            return -1;
        }

        memset(dir_data, 0, sizeof(fat16_dir_private_t));
        dir_data->magic = FAT16_DIR_PRIVATE_MAGIC;
        dir_data->fs = fs;
        dir_data->entries_per_sector = 512 / sizeof(fat16_dir_entry_t);
        dir_data->root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;

        if (node->private_data && node != node->mount->root) {
            kfree(node->private_data);
        }
        node->private_data = dir_data;
    } else if (node->type == VFS_FILE_TYPE_FILE) {
        //ensure file private data exists
        fat16_file_private_t* file_data = (fat16_file_private_t*)node->private_data;
        if (!file_data) {
            file_data = (fat16_file_private_t*)kmalloc(sizeof(fat16_file_private_t));
            if (!file_data) {
                return -1;
            }
            memset(file_data, 0, sizeof(fat16_file_private_t));
            node->private_data = file_data;
        }

        //resolve filesystem pointer from mount root
        if (!node->mount || !node->mount->root || !node->mount->root->private_data) {
            return -1;
        }
        fat16_fs_t* fs;
        void* pdata = node->mount->root->private_data;
        if (pdata && ((fat16_dir_private_t*)pdata)->magic == FAT16_DIR_PRIVATE_MAGIC) {
            fs = ((fat16_dir_private_t*)pdata)->fs;
        } else {
            fs = &((filesystem_t*)pdata)->fs_data.fat16;
        }
        if (!fs) {
            return -1;
        }

        //open the file now so subsequent operations have a valid handle
        if (!file_data->is_open) {
            if (fat16_open_file(fs, &file_data->file, node->name) != 0) {
                return -1;
            }
            file_data->is_open = 1;
        }
    }

    return 0;
}

//close a FAT16 node
static int fat16_vfs_close(vfs_node_t* node) {
    serial_write_string("[FAT16-VFS] Closing node: ");
    serial_write_string(node->name);
    serial_write_string("\n");

    if (node == node->mount->root) {
        return 0;
    }

    if (node->private_data) {
        if (node->type == VFS_FILE_TYPE_FILE) {
            fat16_file_private_t* file_data = (fat16_file_private_t*)node->private_data;
            if (file_data->is_open) {
                fat16_close_file(&file_data->file);
            }
        }
        kfree(node->private_data);
        node->private_data = NULL;
    }

    return 0;
}


//read from a FAT16 file
static int fat16_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer) {
    if (!node || !buffer) {
        return -1;
    }

    serial_write_string("[FAT16-VFS] Reading from file: ");
    serial_write_string(node->name);
    serial_write_string("\n");

    //get filesystem from mount
    if (!node->mount || !node->mount->root || !node->mount->root->private_data) {
        return -1;
    }

    fat16_fs_t* fs;
    void* pdata = node->mount->root->private_data;
    //HACK determine the type of the pointer by checking for our dir_data magic numbe
    //this is needed cuz the VFS design uses one pointer for two types
    if (pdata && ((fat16_dir_private_t*)pdata)->magic == FAT16_DIR_PRIVATE_MAGIC) {
        fs = ((fat16_dir_private_t*)pdata)->fs;
    } else {
        fs = &((filesystem_t*)pdata)->fs_data.fat16;
    }

    if (!fs) {
        return -1; //could not get a valid fs pointer
    }

    //ensure file is open
    fat16_file_private_t* file_data = (fat16_file_private_t*)node->private_data;
    if (!file_data) {
        file_data = (fat16_file_private_t*)kmalloc(sizeof(fat16_file_private_t));
        if (!file_data) {
            return -1;
        }
        memset(file_data, 0, sizeof(fat16_file_private_t));
        node->private_data = file_data;
    }

    //open file if not already open
    if (!file_data->is_open) {
        if (fat16_open_file(fs, &file_data->file, node->name) != 0) {
            return -1;
        }
        file_data->is_open = 1;
    }

    //set file position
    file_data->file.current_offset = offset;

    //read data
    int bytes_read = fat16_read_file(&file_data->file, buffer, size);
    return bytes_read;
}

//write to a FAT16 file
static int fat16_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer) {
    if (!node || !buffer || size == 0) {
        return -1;
    }

    serial_write_string("[FAT16-VFS] Writing to file: ");
    serial_write_string(node->name);
    serial_write_string("\n");

    //ensure file private data exists
    fat16_file_private_t* file_data = (fat16_file_private_t*)node->private_data;
    if (!file_data) {
        file_data = (fat16_file_private_t*)kmalloc(sizeof(fat16_file_private_t));
        if (!file_data) {
            serial_write_string("[FAT16-VFS] Failed to allocate file private data\n");
            return -1;
        }
        memset(file_data, 0, sizeof(fat16_file_private_t));
        node->private_data = file_data;
    }

    //lazy open file if needed (mirror read path logic)
    if (!file_data->is_open) {
        if (!node->mount || !node->mount->root || !node->mount->root->private_data) {
            serial_write_string("[FAT16-VFS] No mount root for write open\n");
            return -1;
        }

        fat16_fs_t* fs;
        void* pdata = node->mount->root->private_data;
        if (pdata && ((fat16_dir_private_t*)pdata)->magic == FAT16_DIR_PRIVATE_MAGIC) {
            fs = ((fat16_dir_private_t*)pdata)->fs;
        } else {
            fs = &((filesystem_t*)pdata)->fs_data.fat16;
        }

        if (!fs) {
            serial_write_string("[FAT16-VFS] Failed to resolve filesystem for write open\n");
            return -1;
        }

        if (fat16_open_file(fs, &file_data->file, node->name) != 0) {
            serial_write_string("[FAT16-VFS] fat16_open_file failed in write\n");
            return -1;
        }
        file_data->is_open = 1;
    }

    //set file position
    file_data->file.current_offset = offset;

    //call the FAT16 write function
    int result = fat16_write_file(&file_data->file, buffer, size);
    if (result > 0) {
        serial_write_string("[FAT16-VFS] Write operation completed\n");
        //update node size to reflect on VFS layer
        if ((uint32_t)node->size < file_data->file.file_size) {
            node->size = file_data->file.file_size;
        }
    } else {
        serial_write_string("[FAT16-VFS] Write operation failed\n");
    }
    return result;
}

//create a file in FAT16
static int fat16_vfs_create(vfs_node_t* parent, const char* name, uint32_t flags) {
    serial_write_string("[FAT16-VFS] Create called for: ");
    serial_write_string(name);
    serial_write_string("\n");
    
    if (!parent || !name) {
        serial_write_string("[FAT16-VFS] Create failed - invalid parameters\n");
        return -1;
    }

    //get the FAT16 filesystem from the parent node
    fat16_fs_t* fs = NULL;
    if (!parent->mount || !parent->mount->root || !parent->mount->root->private_data) {
        serial_write_string("[FAT16-VFS] Create failed - no mount root\n");
        return -1;
    }
    //always resolve FS from the mount root which may either hold a filesystem_t (before open) 
    //or a fat16_dir_private_t (after open)   detect via magic
    void* pdata = parent->mount->root->private_data;
    if (pdata && ((fat16_dir_private_t*)pdata)->magic == FAT16_DIR_PRIVATE_MAGIC) {
        fs = ((fat16_dir_private_t*)pdata)->fs;
    } else {
        fs = &((filesystem_t*)pdata)->fs_data.fat16;
    }
    
    if (!fs) {
        serial_write_string("[FAT16-VFS] Create failed - no filesystem\n");
        return -1;
    }

    serial_write_string("[FAT16-VFS] Calling fat16_create_file\n");
    serial_write_string("[FAT16-VFS] Debug - fs pointer: ");
    char hex_buf[20];
    for (int i = 0; i < 8; i++) {
        hex_buf[i] = "0123456789ABCDEF"[((uint32_t)fs >> (28 - i*4)) & 0xF];
    }
    hex_buf[8] = '\n';
    hex_buf[9] = '\0';
    serial_write_string(hex_buf);
    
    serial_write_string("[FAT16-VFS] Debug - fs->boot_sector.root_entries: ");
    for (int i = 0; i < 8; i++) {
        hex_buf[i] = "0123456789ABCDEF"[((uint32_t)fs->boot_sector.root_entries >> (28 - i*4)) & 0xF];
    }
    hex_buf[8] = '\n';
    hex_buf[9] = '\0';
    serial_write_string(hex_buf);
    
    int result = fat16_create_file(fs, name);
    if (result == 0) {
        serial_write_string("[FAT16-VFS] Create succeeded\n");
    } else {
        serial_write_string("[FAT16-VFS] Create failed\n");
    }
    return result;
}

//elete a file in FAT16
static int fat16_vfs_unlink(vfs_node_t* node) {
    (void)node;

    return -1;
}

//create a directory in FAT16
static int fat16_vfs_mkdir(vfs_node_t* parent, const char* name, uint32_t flags) {
    (void)parent;
    (void)name;
    (void)flags;

    return -1;
}

//remove a directory in FAT16
static int fat16_vfs_rmdir(vfs_node_t* node) {
    (void)node;

    return -1;
}

//read a directory entry in FAT16
static int fat16_vfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t** out) {
    if (!node || !out) {
        return -1;
    }

    if (node->type != VFS_FILE_TYPE_DIRECTORY) {
        return -1;
    }

    fat16_dir_private_t* dir_data = (fat16_dir_private_t*)node->private_data;
    if (!dir_data) {
        return -1;
    }

    fat16_fs_t* fs = dir_data->fs;
    if (!fs) {
        return -1;
    }

    uint32_t current_logical_index = 0;
    uint32_t physical_index = 0;
    uint8_t buffer[512];
    uint32_t current_sector = 0xFFFFFFFF;
    
    while (physical_index < (dir_data->root_dir_sectors * dir_data->entries_per_sector)) {
        uint32_t sector_index = physical_index / dir_data->entries_per_sector;
        uint32_t entry_index_in_sector = physical_index % dir_data->entries_per_sector;

        if (sector_index >= dir_data->root_dir_sectors) {
            return -1; //end of directory
        }

        //read sector if needed
        if (sector_index != current_sector) {
            uint32_t offset = (fs->root_dir_start + sector_index) * 512;
            if (device_read(fs->device, offset, buffer, 512) != 512) {
                return -1;
            }
            current_sector = sector_index;
        }

        fat16_dir_entry_t* entry = &((fat16_dir_entry_t*)buffer)[entry_index_in_sector];

        if (entry->filename[0] == 0x00) {
            return -1; //end of directory no more entries
        }

        physical_index++;

        if (entry->filename[0] == 0xE5 || (entry->attributes & FAT16_ATTR_VOLUME_ID)) {
            continue; //skip deleted entries and volume labels
        }

        if (current_logical_index == index) {
            //found the entry
            char name[13];
            memcpy(name, entry->filename, 8);
            name[8] = '\0';

            //trim trailing spaces
            for (int j = 7; j >= 0; j--) {
                if (name[j] == ' ') name[j] = '\0';
                else break;
            }

            //add extension if present
            if (entry->extension[0] != ' ') {
                unsigned int len = strlen(name);
                name[len] = '.';
                name[len + 1] = '\0';

                for (int j = 0; j < 3 && entry->extension[j] != ' '; j++) {
                    len = strlen(name);
                    name[len] = entry->extension[j];
                    name[len + 1] = '\0';
                }
            }

            uint32_t type = (entry->attributes & FAT16_ATTR_DIRECTORY) ?
                            VFS_FILE_TYPE_DIRECTORY : VFS_FILE_TYPE_FILE;

            vfs_node_t* entry_node = vfs_create_node(name, type, VFS_FLAG_READ | VFS_FLAG_WRITE);
            if (!entry_node) {
                return -1;
            }

            entry_node->size = entry->file_size;
            entry_node->ops = node->ops;
            entry_node->device = node->device;
            entry_node->mount = node->mount;
            entry_node->parent = node;

            if (type == VFS_FILE_TYPE_FILE) {
                fat16_file_private_t* file_data = (fat16_file_private_t*)kmalloc(sizeof(fat16_file_private_t));
                if (!file_data) {
                    vfs_destroy_node(entry_node);
                    return -1;
                }
                memset(file_data, 0, sizeof(fat16_file_private_t));
                entry_node->private_data = file_data;
            }

            *out = entry_node;
            return 0;
        }

        current_logical_index++;
    }

    return -1; //should not be reached
}

//find a directory entry by name
static int fat16_vfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out) {
    if (!node || !name || !out) {
        return -1;
    }

    fat16_dir_private_t* dir_data = (fat16_dir_private_t*)node->private_data;
    if (!dir_data) {
        return -1;
    }

    fat16_fs_t* fs = dir_data->fs;
    if (!fs) {
        return -1;
    }

    uint8_t buffer[512];
    uint32_t current_sector = 0xFFFFFFFF;
    
    //search through all directory entries directly
    for (uint32_t physical_index = 0; physical_index < (dir_data->root_dir_sectors * dir_data->entries_per_sector); physical_index++) {
        uint32_t sector_index = physical_index / dir_data->entries_per_sector;
        uint32_t entry_index_in_sector = physical_index % dir_data->entries_per_sector;

        if (sector_index >= dir_data->root_dir_sectors) {
            break; //end of directory
        }

        //read sector if needed
        if (sector_index != current_sector) {
            uint32_t offset = (fs->root_dir_start + sector_index) * 512;
            if (device_read(fs->device, offset, buffer, 512) != 512) {
                return -1;
            }
            current_sector = sector_index;
        }

        fat16_dir_entry_t* entry = &((fat16_dir_entry_t*)buffer)[entry_index_in_sector];

        if (entry->filename[0] == 0x00) {
            break; //end of directory no more entries
        }

        if (entry->filename[0] == 0xE5 || (entry->attributes & FAT16_ATTR_VOLUME_ID)) {
            continue; //skip deleted entries and volume labels
        }

        //extract filename
        char entry_name[13];
        memcpy(entry_name, entry->filename, 8);
        entry_name[8] = '\0';

        //trim trailing spaces
        for (int j = 7; j >= 0; j--) {
            if (entry_name[j] == ' ') entry_name[j] = '\0';
            else break;
        }

        //add extension if present
        if (entry->extension[0] != ' ') {
            unsigned int len = strlen(entry_name);
            entry_name[len] = '.';
            entry_name[len + 1] = '\0';

            for (int j = 0; j < 3 && entry->extension[j] != ' '; j++) {
                len = strlen(entry_name);
                entry_name[len] = entry->extension[j];
                entry_name[len + 1] = '\0';
            }
        }

        if (strcmp(entry_name, name) == 0) {
            //found the file create node
            uint32_t type = (entry->attributes & FAT16_ATTR_DIRECTORY) ?
                            VFS_FILE_TYPE_DIRECTORY : VFS_FILE_TYPE_FILE;

            vfs_node_t* entry_node = vfs_create_node(entry_name, type, VFS_FLAG_READ | VFS_FLAG_WRITE);
            if (!entry_node) {
                return -1;
            }

            entry_node->size = entry->file_size;
            entry_node->ops = node->ops;
            entry_node->device = node->device;
            entry_node->mount = node->mount;
            entry_node->parent = node;

            if (type == VFS_FILE_TYPE_FILE) {
                fat16_file_private_t* file_data = (fat16_file_private_t*)kmalloc(sizeof(fat16_file_private_t));
                if (!file_data) {
                    vfs_destroy_node(entry_node);
                    return -1;
                }
                memset(file_data, 0, sizeof(fat16_file_private_t));
                entry_node->private_data = file_data;
            }

            *out = entry_node;
            return 0;
        }
    }

    return -1; //not found
}

//get file size
static int fat16_vfs_get_size(vfs_node_t* node) {
    if (!node) {
        return -1;
    }

    return node->size;
}

//IOCTL operation
static int fat16_vfs_ioctl(vfs_node_t* node, uint32_t request, void* arg) {
    (void)node;
    (void)request;
    (void)arg;

    return -1;
}
