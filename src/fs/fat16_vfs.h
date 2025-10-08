#ifndef FAT16_VFS_H
#define FAT16_VFS_H

#include "vfs.h"
#include "fat16.h"

//create root VFS node for a mounted FAT16 filesystem
vfs_node_t* fat16_get_root(void* mount_data);

extern vfs_operations_t fat16_vfs_ops;

#endif
