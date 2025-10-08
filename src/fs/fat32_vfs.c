#include "fat32_vfs.h"
#include "fat32.h"
#include "vfs.h"
#include "../mm/heap.h"
#include "../libc/string.h"
#include <stddef.h>

//FAT32 private data for VFS nodes
typedef struct {
    fat32_mount_t* mount;
    uint32_t start_cluster;
    uint32_t parent_cluster;    //parent directory cluster (for updating entry)
    fat32_dir_entry_t dir_entry;
} fat32_vfs_data_t;

//forward declarations for internal functions from fat32.c
extern int fat32_read_file_data(fat32_mount_t* mount, uint32_t start_cluster, uint32_t offset,
                                uint32_t size, char* buffer);
extern int fat32_write_file_data(fat32_mount_t* mount, uint32_t* start_cluster, uint32_t offset,
                                 uint32_t size, const char* buffer);
extern int fat32_find_in_dir(fat32_mount_t* mount, uint32_t dir_cluster, const char* name,
                             fat32_dir_entry_t* entry_out);

//VFS open
static int fat32_vfs_open(vfs_node_t* node, uint32_t flags) {
    (void)node;
    (void)flags;
    return 0; //nothing special needed for FAT32
}

//VFS close
static int fat32_vfs_close(vfs_node_t* node) {
    (void)node;
    return 0;
}

//VFS read
static int fat32_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer) {
    if (!node || !node->private_data || !buffer) return -1;
    
    fat32_vfs_data_t* data = (fat32_vfs_data_t*)node->private_data;
    uint32_t start_cluster = data->start_cluster;
    
    if (start_cluster == 0) return 0; //empty file
    
    return fat32_read_file_data(data->mount, start_cluster, offset, size, buffer);
}

//VFS write
static int fat32_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer) {
    if (!node || !node->private_data || !buffer) return -1;
    
    fat32_vfs_data_t* data = (fat32_vfs_data_t*)node->private_data;
    
    int written = fat32_write_file_data(data->mount, &data->start_cluster, offset, size, buffer);
    
    //update file size and cluster if changed
    if (written > 0) {
        uint32_t new_size = offset + (uint32_t)written;
        int needs_update = 0;
        
        //check if size changed
        if (new_size > data->dir_entry.file_size) {
            data->dir_entry.file_size = new_size;
            node->size = new_size;
            needs_update = 1;
        }
        
        //check if start cluster changed (file was empty before)
        uint32_t old_cluster = ((uint32_t)data->dir_entry.first_cluster_hi << 16) | 
                               data->dir_entry.first_cluster_lo;
        if (old_cluster != data->start_cluster) {
            data->dir_entry.first_cluster_hi = (data->start_cluster >> 16) & 0xFFFF;
            data->dir_entry.first_cluster_lo = data->start_cluster & 0xFFFF;
            needs_update = 1;
        }
        
        //persist changes to disk if parent cluster is valid
        if (needs_update && data->parent_cluster >= 2) {
            extern int fat32_update_dir_entry(fat32_mount_t* mount, uint32_t parent_cluster,
                                              const char* filename, fat32_dir_entry_t* updated_entry);
            if (fat32_update_dir_entry(data->mount, data->parent_cluster, node->name, 
                                       &data->dir_entry) != 0) {
                //failed to update but data was written - log warning and continue
                //need to thandle it different tbh
            }
        }
    }
    
    return written;
}

//VFS create file
static int fat32_vfs_create(vfs_node_t* parent, const char* name, uint32_t flags) {
    (void)flags;
    if (!parent || !parent->private_data || !name) return -1;
    
    fat32_vfs_data_t* dir_data = (fat32_vfs_data_t*)parent->private_data;
    uint32_t dir_cluster = dir_data->start_cluster;

    //special case: root directory
    if (dir_cluster == 0) {
        dir_cluster = dir_data->mount->root_dir_cluster;
        //validate root cluster
        if (dir_cluster < 2 || dir_cluster >= FAT32_EOC_MIN) {
            return -1; //corrupted root cluster
        }
    }
    
    return fat32_create_file(dir_data->mount, dir_cluster, name);
}

//VFS find file/dir in directory
static int fat32_vfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out) {
    if (!node || !node->private_data || !name || !out) return -1;
    
    fat32_vfs_data_t* dir_data = (fat32_vfs_data_t*)node->private_data;
    uint32_t dir_cluster = dir_data->start_cluster;
    
    //special case root directory
    if (dir_cluster == 0) {
        dir_cluster = dir_data->mount->root_dir_cluster;
        //validate root cluster
        if (dir_cluster < 2 || dir_cluster >= FAT32_EOC_MIN) {
            return -1; //corrupted root cluster
        }
    }
    
    //find entry
    fat32_dir_entry_t entry;
    if (fat32_find_in_dir(dir_data->mount, dir_cluster, name, &entry) != 0) {
        return -1; //not found
    }
    
    //create VFS node
    uint32_t file_type = (entry.attr & FAT32_ATTR_DIRECTORY) ? VFS_FILE_TYPE_DIRECTORY : VFS_FILE_TYPE_FILE;
    vfs_node_t* child = vfs_create_node(name, file_type, 0);
    if (!child) return -1;
    
    //set up private data
    fat32_vfs_data_t* child_data = (fat32_vfs_data_t*)kmalloc(sizeof(fat32_vfs_data_t));
    if (!child_data) {
        vfs_destroy_node(child);
        return -1;
    }
    
    child_data->mount = dir_data->mount;
    child_data->start_cluster = ((uint32_t)entry.first_cluster_hi << 16) | entry.first_cluster_lo;
    child_data->parent_cluster = dir_cluster; //store parent for updates
    memcpy(&child_data->dir_entry, &entry, sizeof(fat32_dir_entry_t));
    
    child->private_data = child_data;
    child->size = entry.file_size;
    child->ops = node->ops; //share same operations
    
    *out = child;
    return 0;
}

//VFS read directory
static int fat32_vfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t** out) {
    if (!node || !node->private_data || !out) return -1;
    
    fat32_vfs_data_t* dir_data = (fat32_vfs_data_t*)node->private_data;
    uint32_t dir_cluster = dir_data->start_cluster;
    
    //special case root directory
    if (dir_cluster == 0) {
        dir_cluster = dir_data->mount->root_dir_cluster;
        //validate root cluster
        if (dir_cluster < 2 || dir_cluster >= FAT32_EOC_MIN) {
            return -1; //corrupted root cluster
        }
    }
    
    //get the nth directory entry
    fat32_dir_entry_t entry;
    char name[256];
    extern int fat32_get_dir_entry(fat32_mount_t* mount, uint32_t dir_cluster, uint32_t index,
                                   fat32_dir_entry_t* entry_out, char* name_out);
    
    if (fat32_get_dir_entry(dir_data->mount, dir_cluster, index, &entry, name) != 0) {
        return -1; //no more entries
    }
    
    //create VFS node for this entry
    uint32_t file_type = (entry.attr & FAT32_ATTR_DIRECTORY) ? VFS_FILE_TYPE_DIRECTORY : VFS_FILE_TYPE_FILE;
    vfs_node_t* child = vfs_create_node(name, file_type, 0);
    if (!child) return -1;
    
    //set up private data
    fat32_vfs_data_t* child_data = (fat32_vfs_data_t*)kmalloc(sizeof(fat32_vfs_data_t));
    if (!child_data) {
        vfs_destroy_node(child);
        return -1;
    }
    
    child_data->mount = dir_data->mount;
    child_data->start_cluster = ((uint32_t)entry.first_cluster_hi << 16) | entry.first_cluster_lo;
    child_data->parent_cluster = dir_cluster; //store parent for updates
    memcpy(&child_data->dir_entry, &entry, sizeof(fat32_dir_entry_t));
    
    child->private_data = child_data;
    child->size = entry.file_size;
    child->ops = node->ops; //share same operations
    
    *out = child;
    return 0;
}

//VFS get file size
static int fat32_vfs_get_size(vfs_node_t* node) {
    if (!node) return -1;
    return (int)node->size;
}

//VFS delete file
static int fat32_vfs_unlink(vfs_node_t* node) {
    if (!node || !node->private_data || !node->parent || !node->parent->private_data) return -1;
    
    fat32_vfs_data_t* file_data = (fat32_vfs_data_t*)node->private_data;
    fat32_vfs_data_t* parent_data = (fat32_vfs_data_t*)node->parent->private_data;
    
    uint32_t dir_cluster = parent_data->start_cluster;
    
    //special case root directory
    if (dir_cluster == 0) {
        dir_cluster = parent_data->mount->root_dir_cluster;
        //validate root cluster
        if (dir_cluster < 2 || dir_cluster >= FAT32_EOC_MIN) {
            return -1; //corrupted root cluster
        }
    }
    
    //call the FAT32 delete function
    extern int fat32_delete_file(fat32_mount_t* mount, uint32_t dir_cluster, const char* filename);
    return fat32_delete_file(file_data->mount, dir_cluster, node->name);
}

//VFS create directory
static int fat32_vfs_mkdir(vfs_node_t* parent, const char* name, uint32_t flags) {
    (void)flags;
    if (!parent || !parent->private_data || !name) return -1;
    
    fat32_vfs_data_t* parent_data = (fat32_vfs_data_t*)parent->private_data;
    uint32_t parent_cluster = parent_data->start_cluster;
    
    //special case root directory
    if (parent_cluster == 0) {
        parent_cluster = parent_data->mount->root_dir_cluster;
        //validate root cluster
        if (parent_cluster < 2 || parent_cluster >= FAT32_EOC_MIN) {
            return -1; //corrupted root cluster
        }
    }
    
    //call FAT32 directory creation function
    extern int fat32_create_directory(fat32_mount_t* mount, uint32_t parent_cluster, const char* dirname);
    return fat32_create_directory(parent_data->mount, parent_cluster, name);
}

//VFS remove directory
static int fat32_vfs_rmdir(vfs_node_t* node) {
    if (!node || !node->private_data || !node->parent || !node->parent->private_data) return -1;
    
    fat32_vfs_data_t* dir_data = (fat32_vfs_data_t*)node->private_data;
    fat32_vfs_data_t* parent_data = (fat32_vfs_data_t*)node->parent->private_data;
    
    uint32_t parent_cluster = parent_data->start_cluster;
    
    //special case root directory as parent
    if (parent_cluster == 0) {
        parent_cluster = parent_data->mount->root_dir_cluster;
        //validate root cluster
        if (parent_cluster < 2 || parent_cluster >= FAT32_EOC_MIN) {
            return -1; //corrupted root cluster
        }
    }
    
    //call FAT32 delete directory function
    extern int fat32_delete_directory(fat32_mount_t* mount, uint32_t parent_cluster, const char* dirname);
    return fat32_delete_directory(dir_data->mount, parent_cluster, node->name);
}

//VFS operations table
vfs_operations_t fat32_vfs_ops = {
    .open = fat32_vfs_open,
    .close = fat32_vfs_close,
    .read = fat32_vfs_read,
    .write = fat32_vfs_write,
    .create = fat32_vfs_create,
    .unlink = fat32_vfs_unlink,
    .mkdir = fat32_vfs_mkdir,
    .rmdir = fat32_vfs_rmdir,
    .readdir = fat32_vfs_readdir,
    .finddir = fat32_vfs_finddir,
    .get_size = fat32_vfs_get_size,
    .ioctl = NULL,
    .readlink = NULL,
    .symlink = NULL,
    .link = NULL,
};

//create root VFS node for a mounted FAT32 filesystem
vfs_node_t* fat32_get_root(void* mount_data) {
    if (!mount_data) return NULL;
    
    fat32_mount_t* mount = (fat32_mount_t*)mount_data;
    
    vfs_node_t* root = vfs_create_node("fat32_root", VFS_FILE_TYPE_DIRECTORY, 0);
    if (!root) return NULL;
    
    //set up private data
    fat32_vfs_data_t* root_data = (fat32_vfs_data_t*)kmalloc(sizeof(fat32_vfs_data_t));
    if (!root_data) {
        vfs_destroy_node(root);
        return NULL;
    }
    
    root_data->mount = mount;
    root_data->start_cluster = 0; //special: root directory
    root_data->parent_cluster = 0; //root has no parent
    memset(&root_data->dir_entry, 0, sizeof(fat32_dir_entry_t));
    
    root->private_data = root_data;
    root->ops = &fat32_vfs_ops;
    root->mode = 0755;
    
    return root;
}
