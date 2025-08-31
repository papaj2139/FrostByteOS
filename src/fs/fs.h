#ifndef FS_H
#define FS_H

#include <stdint.h>
#include "../device_manager.h"
#include "fat16.h"
#include "vfs.h"

//filesystem types
typedef enum {
    FS_TYPE_NONE,
    FS_TYPE_FAT16,
    FS_TYPE_FAT32,
    FS_TYPE_EXT2
} fs_type_t;

//filesystem structure
typedef struct {
    fs_type_t type;
    device_t* device;
    union {
        fat16_fs_t fat16;

    } fs_data;
} filesystem_t;

//file operations
int fs_init(filesystem_t* fs, device_t* device);
int fs_open(filesystem_t* fs, const char* filename, fat16_file_t* file);
int fs_read(fat16_file_t* file, void* buffer, uint32_t size);
int fs_close(fat16_file_t* file);
int fs_list_directory(filesystem_t* fs);

//VFS integration
int fs_vfs_init(void);

#endif
