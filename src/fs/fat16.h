#ifndef FAT16_H
#define FAT16_H

#include <stdint.h>
#include "../device_manager.h"

//FAT16 boot sector structure
typedef struct __attribute__((packed)) {
    uint8_t     jmp_boot[3];
    uint8_t     oem_name[8];
    uint16_t    bytes_per_sector;
    uint8_t     sectors_per_cluster;
    uint16_t    reserved_sectors;
    uint8_t     num_fats;
    uint16_t    root_entries;
    uint16_t    total_sectors_16;
    uint8_t     media_type;
    uint16_t    sectors_per_fat;
    uint16_t    sectors_per_track;
    uint16_t    num_heads;
    uint32_t    hidden_sectors;
    uint32_t    total_sectors_32;
    uint8_t     drive_number;
    uint8_t     reserved1;
    uint8_t     boot_signature;
    uint32_t    volume_id;
    uint8_t     volume_label[11];
    uint8_t     file_system_type[8];
    uint8_t     boot_code[448];
    uint16_t    boot_signature_end;
} fat16_boot_sector_t;

//directory entry
typedef struct __attribute__((packed)) {
    uint8_t     filename[8];
    uint8_t     extension[3];
    uint8_t     attributes;
    uint8_t     reserved[10];
    uint16_t    time;
    uint16_t    date;
    uint16_t    first_cluster;
    uint32_t    file_size;
} fat16_dir_entry_t;

//file attributes
#define FAT16_ATTR_READ_ONLY  0x01
#define FAT16_ATTR_HIDDEN     0x02
#define FAT16_ATTR_SYSTEM     0x04
#define FAT16_ATTR_VOLUME_ID  0x08
#define FAT16_ATTR_DIRECTORY  0x10
#define FAT16_ATTR_ARCHIVE    0x20

//filesystem structure
typedef struct {
    device_t* device;
    fat16_boot_sector_t boot_sector;
    uint32_t fat_start;
    uint32_t root_dir_start;
    uint32_t data_start;
    uint32_t total_clusters;
} fat16_fs_t;

//file handle
typedef struct {
    fat16_fs_t* fs;
    fat16_dir_entry_t entry;
    uint32_t current_cluster;
    uint32_t current_offset;
    uint32_t file_size;
    uint8_t is_open;
} fat16_file_t;

//function declarations
int fat16_init(fat16_fs_t* fs, device_t* device);
int fat16_read_boot_sector(fat16_fs_t* fs);
int fat16_open_file(fat16_fs_t* fs, fat16_file_t* file, const char* filename);
int fat16_read_file(fat16_file_t* file, void* buffer, uint32_t size);
int fat16_write_file(fat16_file_t* file, const void* buffer, uint32_t size);
int fat16_close_file(fat16_file_t* file);
int fat16_list_directory(fat16_fs_t* fs);
int fat16_create_file(fat16_fs_t* fs, const char* filename);
uint16_t fat16_get_next_cluster(fat16_fs_t* fs, uint16_t cluster);
int fat16_create_dir_root(fat16_fs_t* fs, const char* name);
int fat16_remove_dir_root(fat16_fs_t* fs, const char* name);
int fat16_delete_file_root(fat16_fs_t* fs, const char* filename);

//util functions
void fat16_to_83_name(const char* name, char* fat_name);
int fat16_find_file(fat16_fs_t* fs, const char* filename, fat16_dir_entry_t* entry);

#endif
