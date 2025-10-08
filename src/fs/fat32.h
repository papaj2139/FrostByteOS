#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include "../device_manager.h"

//FAT32 BIOS Parameter Block (BPB)
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];                //0x00: jump instruction
    uint8_t  oem_name[8];           //0x03: OEM name
    uint16_t bytes_per_sector;      //0x0B: bytes per sector
    uint8_t  sectors_per_cluster;   //0x0D: sectors per cluster
    uint16_t reserved_sectors;      //0x0E: reserved sectors (incl boot sector)
    uint8_t  num_fats;              //0x10: number of FAT copies
    uint16_t root_entry_count;      //0x11: root entries (0 for FAT32)
    uint16_t total_sectors_16;      //0x13: total sectors (if < 65536)
    uint8_t  media_type;            //0x15: media type
    uint16_t fat_size_16;           //0x16: FAT size (0 for FAT32)
    uint16_t sectors_per_track;     //0x18: sectors per track
    uint16_t num_heads;             //0x1A: number of heads
    uint32_t hidden_sectors;        //0x1C: hidden sectors
    uint32_t total_sectors_32;      //0x20: total sectors
    
    //FAT32-specific fields
    uint32_t fat_size_32;           //0x24: FAT size in sectors
    uint16_t ext_flags;             //0x28: extended flags
    uint16_t fs_version;            //0x2A: filesystem version
    uint32_t root_cluster;          //0x2C: root directory cluster
    uint16_t fs_info;               //0x30: FSInfo sector
    uint16_t backup_boot_sector;    //0x32: backup boot sector
    uint8_t  reserved[12];          //0x34: reserved
    uint8_t  drive_number;          //0x40: drive number
    uint8_t  reserved1;             //0x41: reserved
    uint8_t  boot_signature;        //0x42: extended boot signature (0x29)
    uint32_t volume_id;             //0x43: volume ID
    uint8_t  volume_label[11];      //0x47: volume label
    uint8_t  fs_type[8];            //0x52: filesystem type ("FAT32   ")
} fat32_bpb_t;

//FAT32 FSInfo structure
typedef struct __attribute__((packed)) {
    uint32_t lead_signature;        //0x41615252
    uint8_t  reserved1[480];
    uint32_t struct_signature;      //0x61417272
    uint32_t free_count;            //free cluster count (0xFFFFFFFF = unknown)
    uint32_t next_free;             //next free cluster hint
    uint8_t  reserved2[12];
    uint32_t trail_signature;       //0xAA550000
} fat32_fsinfo_t;

//FAT32 Directory Entry (short filename)
typedef struct __attribute__((packed)) {
    uint8_t  name[11];              //8.3 filename
    uint8_t  attr;                  //attributes
    uint8_t  nt_reserved;           //reserved for Windows NT
    uint8_t  create_time_tenth;     //creation time (tenths of second)
    uint16_t create_time;           //creation time
    uint16_t create_date;           //creation date
    uint16_t access_date;           //last access date
    uint16_t first_cluster_hi;      //high word of first cluster
    uint16_t write_time;            //last write time
    uint16_t write_date;            //last write date
    uint16_t first_cluster_lo;      //low word of first cluster
    uint32_t file_size;             //file size in bytes
} fat32_dir_entry_t;

//FAT32 Long Filename Entry
typedef struct __attribute__((packed)) {
    uint8_t  order;                 //sequence number
    uint16_t name1[5];              //first 5 characters (UTF-16)
    uint8_t  attr;                  //always 0x0F for LFN
    uint8_t  type;                  //always 0 for LFN
    uint8_t  checksum;              //checksum of short name
    uint16_t name2[6];              //next 6 characters
    uint16_t first_cluster;         //always 0 for LFN
    uint16_t name3[2];              //last 2 characters
} fat32_lfn_entry_t;

//directory entry attributes
#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LONG_NAME  (FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN | FAT32_ATTR_SYSTEM | FAT32_ATTR_VOLUME_ID)

//special cluster values
#define FAT32_FREE_CLUSTER    0x00000000
#define FAT32_BAD_CLUSTER     0x0FFFFFF7
#define FAT32_EOC_MIN         0x0FFFFFF8  //end of chain minimum
#define FAT32_EOC             0x0FFFFFFF  //end of chain marker

//FAT32 mount data
typedef struct {
    device_t* device;
    fat32_bpb_t bpb;
    fat32_fsinfo_t fsinfo;
    
    uint32_t fat_begin_lba;         //first FAT sector
    uint32_t cluster_begin_lba;     //first data sector
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t root_dir_cluster;
    uint32_t total_clusters;
    
    uint32_t* fat_cache;            //cached FAT table (optional)
    uint32_t fat_cache_size;
} fat32_mount_t;

//function declarations
int fat32_init(void);
int fat32_mount(device_t* device, void** mount_data_out);
int fat32_unmount(void* mount_data);

//cluster operations
uint32_t fat32_get_fat_entry(fat32_mount_t* mount, uint32_t cluster);
int fat32_set_fat_entry(fat32_mount_t* mount, uint32_t cluster, uint32_t value);
uint32_t fat32_allocate_cluster(fat32_mount_t* mount);
int fat32_free_cluster_chain(fat32_mount_t* mount, uint32_t start_cluster);
uint32_t fat32_cluster_to_lba(fat32_mount_t* mount, uint32_t cluster);

//file/directory operations
int fat32_read_cluster(fat32_mount_t* mount, uint32_t cluster, void* buffer);
int fat32_write_cluster(fat32_mount_t* mount, uint32_t cluster, const void* buffer);
int fat32_create_file(fat32_mount_t* mount, uint32_t dir_cluster, const char* filename);
int fat32_delete_file(fat32_mount_t* mount, uint32_t dir_cluster, const char* filename);
int fat32_create_directory(fat32_mount_t* mount, uint32_t parent_cluster, const char* dirname);
int fat32_delete_directory(fat32_mount_t* mount, uint32_t parent_cluster, const char* dirname);
int fat32_update_dir_entry(fat32_mount_t* mount, uint32_t parent_cluster, const char* filename,
                           fat32_dir_entry_t* updated_entry);

//directory enumeration (returns nth entry from directory and -1 if no more entries)
int fat32_get_dir_entry(fat32_mount_t* mount, uint32_t dir_cluster, uint32_t index,
                        fat32_dir_entry_t* entry_out, char* name_out);

#endif
