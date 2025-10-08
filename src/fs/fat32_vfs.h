#ifndef FAT32_VFS_H
#define FAT32_VFS_H

#include "vfs.h"
#include "fat32.h"

//create root VFS node for a mounted FAT32 filesystem
vfs_node_t* fat32_get_root(void* mount_data);

//VFS operations for FAT32
extern vfs_operations_t fat32_vfs_ops;

#endif
