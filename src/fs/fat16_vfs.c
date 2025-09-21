#include "fat16_vfs.h"
#include "fat16.h"
#include "../drivers/serial.h"
#include "../mm/heap.h"
#include "vfs.h"
#include "fs.h"
#include <stdbool.h>
#include <string.h>

//FAT16 constants used by directory traversal
#ifndef FAT16_END_OF_CHAIN
#define FAT16_END_OF_CHAIN 0xFFF8
#endif

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
    uint16_t first_cluster;     //0 for root directory otherwise cluster of directory
    uint32_t current_sector;
    uint32_t sector_offset;
    uint32_t entries_per_sector;
    uint32_t root_dir_sectors;  //only valid for root
    uint32_t sectors_per_cluster;
    uint32_t current_index; //keep track of current position for readdir
} fat16_dir_private_t;

static int fat16v_is_root_dir(fat16_dir_private_t* d){ return d->first_cluster == 0; }

//build display name from a dir entry (8.3 to canonical string)
static void fat16v_entry_make_name(const fat16_dir_entry_t* entry, char* name_out, size_t outsz) {
    char name[9];
    memcpy(name, entry->filename, 8);
    name[8] = '\0';
    //trim spaces
    for (int j = 7; j >= 0; j--) { if (name[j] == ' ') name[j] = '\0'; else break; }
    strncpy(name_out, name, outsz-1);
    name_out[outsz-1] = '\0';
    if (entry->extension[0] != ' ') {
        size_t len = strlen(name_out);
        if (len + 1 < outsz) { name_out[len++] = '.'; name_out[len] = '\0'; }
        for (int j = 0; j < 3 && entry->extension[j] != ' '; j++) {
            size_t l2 = strlen(name_out);
            if (l2 + 1 < outsz) { name_out[l2] = entry->extension[j]; name_out[l2+1] = '\0'; }
        }
    }
}

//iterate entries in a directory and return the logical-th entry
static int fat16v_dir_get_entry(fat16_dir_private_t* dir, uint32_t logical_index, fat16_dir_entry_t* out_entry) {
    if (!dir || !out_entry) return -1;
    uint32_t per_sec = dir->entries_per_sector;
    uint8_t buffer[512];

    if (fat16v_is_root_dir(dir)) {
        uint32_t physical_index = 0;
        uint32_t current_sector = 0xFFFFFFFF;
        while (physical_index < (dir->root_dir_sectors * per_sec)) {
            uint32_t sector_index = physical_index / per_sec;
            uint32_t entry_index_in_sector = physical_index % per_sec;
            if (sector_index >= dir->root_dir_sectors) return -1;
            if (sector_index != current_sector) {
                uint32_t offset = (dir->fs->root_dir_start + sector_index) * 512;
                if (device_read(dir->fs->device, offset, buffer, 512) != 512) return -1;
                current_sector = sector_index;
            }
            fat16_dir_entry_t* entry = &((fat16_dir_entry_t*)buffer)[entry_index_in_sector];
            if (entry->filename[0] == 0x00) return -1; //end
            physical_index++;
            if (entry->filename[0] == 0xE5 || (entry->attributes & FAT16_ATTR_VOLUME_ID)) continue;
            if (logical_index == 0) { memcpy(out_entry, entry, sizeof(*out_entry)); return 0; }
            logical_index--;
        }
        return -1;
    } else {
        //iterate cluster chain
        uint16_t cluster = dir->first_cluster;
        uint32_t logical = 0;
        while (cluster >= 2 && cluster < FAT16_END_OF_CHAIN) {
            uint32_t base_lba = dir->fs->data_start + (cluster - 2) * dir->sectors_per_cluster;
            for (uint32_t s = 0; s < dir->sectors_per_cluster; s++) {
                if (device_read(dir->fs->device, (base_lba + s) * 512, buffer, 512) != 512) return -1;
                fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
                for (uint32_t i = 0; i < per_sec; i++) {
                    fat16_dir_entry_t* entry = &entries[i];
                    if (entry->filename[0] == 0x00) return -1; //end marker
                    if (entry->filename[0] == 0xE5 || (entry->attributes & FAT16_ATTR_VOLUME_ID)) continue;
                    if (logical == logical_index) { memcpy(out_entry, entry, sizeof(*out_entry)); return 0; }
                    logical++;
                }
            }
            uint16_t next = fat16_get_next_cluster(dir->fs, cluster);
            if (next >= FAT16_END_OF_CHAIN) break; else cluster = next;
        }
        return -1;
    }
}

//find an entry by name in a directory
static int fat16v_dir_find(fat16_dir_private_t* dir, const char* name, fat16_dir_entry_t* out) {
    if (!dir || !name || !out) return -1;
    uint32_t idx = 0;
    fat16_dir_entry_t e;
    while (fat16v_dir_get_entry(dir, idx, &e) == 0) {
        char ename[13];
        fat16v_entry_make_name(&e, ename, sizeof(ename));
        if (strcmp(ename, name) == 0) { memcpy(out, &e, sizeof(e)); return 0; }
        idx++;
    }
    return -1;
}

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
        dir_data->first_cluster = 0; //root by default when opening arbitrary dir
        dir_data->entries_per_sector = 512 / sizeof(fat16_dir_entry_t);
        dir_data->root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;
        dir_data->sectors_per_cluster = fs->boot_sector.sectors_per_cluster;

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

        //resolve fs and parent directory from parent->private_data
        if (!node->parent || !node->parent->private_data) {
            return -1;
        }
        fat16_dir_private_t* pdir = (fat16_dir_private_t*)node->parent->private_data;
        if (pdir->magic != FAT16_DIR_PRIVATE_MAGIC) return -1;

        if (!file_data->is_open) {
            fat16_dir_entry_t ent;
            if (fat16v_dir_find(pdir, node->name, &ent) != 0) return -1;
            //must be file
            if (ent.attributes & FAT16_ATTR_DIRECTORY) return -1;
            memset(&file_data->file, 0, sizeof(file_data->file));
            file_data->file.fs = pdir->fs;
            memcpy(&file_data->file.entry, &ent, sizeof(ent));
            file_data->file.current_cluster = ent.first_cluster;
            file_data->file.current_offset = 0;
            file_data->file.file_size = ent.file_size;
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
    (void)flags;
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
    if (!node) return -1;
    if (!node->mount || !node->mount->root || !node->mount->root->private_data) return -1;
    //only support deleting files at root for now
    fat16_fs_t* fs;
    void* pdata = node->mount->root->private_data;
    if (pdata && ((fat16_dir_private_t*)pdata)->magic == FAT16_DIR_PRIVATE_MAGIC) {
        fs = ((fat16_dir_private_t*)pdata)->fs;
    } else {
        fs = &((filesystem_t*)pdata)->fs_data.fat16;
    }
    if (!fs) return -1;
    if (node->type != VFS_FILE_TYPE_FILE) return -1;
    return fat16_delete_file_root(fs, node->name);
}

//create a directory in FAT16
static int fat16_vfs_mkdir(vfs_node_t* parent, const char* name, uint32_t flags) {
    (void)flags;
    if (!parent || !name) return -1;
    if (!parent->mount || !parent->mount->root || !parent->mount->root->private_data) return -1;
    //restrict to creating subdirs in root for now
    if (parent != parent->mount->root) return -1;
    fat16_fs_t* fs;
    void* pdata = parent->mount->root->private_data;
    if (pdata && ((fat16_dir_private_t*)pdata)->magic == FAT16_DIR_PRIVATE_MAGIC) {
        fs = ((fat16_dir_private_t*)pdata)->fs;
    } else {
        fs = &((filesystem_t*)pdata)->fs_data.fat16;
    }
    if (!fs) return -1;
    return fat16_create_dir_root(fs, name);
}

//remove a directory in FAT16
static int fat16_vfs_rmdir(vfs_node_t* node) {
    if (!node) return -1;
    if (node->type != VFS_FILE_TYPE_DIRECTORY) return -1;
    if (!node->mount || !node->mount->root || !node->mount->root->private_data) return -1;
    fat16_fs_t* fs;
    void* pdata = node->mount->root->private_data;
    if (pdata && ((fat16_dir_private_t*)pdata)->magic == FAT16_DIR_PRIVATE_MAGIC) {
        fs = ((fat16_dir_private_t*)pdata)->fs;
    } else {
        fs = &((filesystem_t*)pdata)->fs_data.fat16;
    }
    if (!fs) return -1;
    return fat16_remove_dir_root(fs, node->name);
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
    if (!dir_data) return -1;
    fat16_dir_entry_t entry;
    if (fat16v_dir_get_entry(dir_data, index, &entry) != 0) return -1;

    char name[13];
    fat16v_entry_make_name(&entry, name, sizeof(name));
    uint32_t type = (entry.attributes & FAT16_ATTR_DIRECTORY) ? VFS_FILE_TYPE_DIRECTORY : VFS_FILE_TYPE_FILE;
    vfs_node_t* entry_node = vfs_create_node(name, type, VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (!entry_node) return -1;
    entry_node->size = entry.file_size;
    entry_node->ops = node->ops;
    entry_node->device = node->device;
    entry_node->mount = node->mount;
    entry_node->parent = node;
    if (type == VFS_FILE_TYPE_DIRECTORY) {
        fat16_dir_private_t* sub = (fat16_dir_private_t*)kmalloc(sizeof(fat16_dir_private_t));
        if (!sub) { 
            vfs_destroy_node(entry_node); 
            return -1; 
        }
        memset(sub, 0, sizeof(*sub));
        sub->magic = FAT16_DIR_PRIVATE_MAGIC;
        sub->fs = dir_data->fs;
        sub->first_cluster = entry.first_cluster;
        sub->entries_per_sector = 512 / sizeof(fat16_dir_entry_t);
        sub->root_dir_sectors = 0; //not used for subdir
        sub->sectors_per_cluster = dir_data->fs->boot_sector.sectors_per_cluster;
        entry_node->private_data = sub;
    } else {
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
static int fat16_vfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out) {
    if (!node || !name || !out) return -1;
    fat16_dir_private_t* dir_data = (fat16_dir_private_t*)node->private_data;
    if (!dir_data) return -1;
    fat16_dir_entry_t entry;
    if (fat16v_dir_find(dir_data, name, &entry) != 0) return -1;
    char entry_name[13];
    fat16v_entry_make_name(&entry, entry_name, sizeof(entry_name));
    uint32_t type = (entry.attributes & FAT16_ATTR_DIRECTORY) ? VFS_FILE_TYPE_DIRECTORY : VFS_FILE_TYPE_FILE;
    vfs_node_t* entry_node = vfs_create_node(entry_name, type, VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (!entry_node) return -1;
    entry_node->size = entry.file_size;
    entry_node->ops = node->ops;
    entry_node->device = node->device;
    entry_node->mount = node->mount;
    entry_node->parent = node;
    if (type == VFS_FILE_TYPE_DIRECTORY) {
        fat16_dir_private_t* sub = (fat16_dir_private_t*)kmalloc(sizeof(fat16_dir_private_t));
        if (!sub) { 
            vfs_destroy_node(entry_node); 
            return -1;
        }
        memset(sub, 0, sizeof(*sub));
        sub->magic = FAT16_DIR_PRIVATE_MAGIC;
        sub->fs = dir_data->fs;
        sub->first_cluster = entry.first_cluster;
        sub->entries_per_sector = 512 / sizeof(fat16_dir_entry_t);
        sub->root_dir_sectors = 0;
        sub->sectors_per_cluster = dir_data->fs->boot_sector.sectors_per_cluster;
        entry_node->private_data = sub;
    } else {
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

static int fat16_vfs_get_size(vfs_node_t* node) {
    if (!node) return -1;
    return (int)node->size;
}

//IOCTL operation
static int fat16_vfs_ioctl(vfs_node_t* node, uint32_t request, void* arg) {
    (void)node;
    (void)request;
    (void)arg;

    return -1;
}
