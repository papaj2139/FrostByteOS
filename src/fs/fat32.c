#include "fat32.h"
#include "vfs.h"
#include "../mm/heap.h"
#include "../libc/string.h"
#include "../drivers/serial.h"
#include "../drivers/rtc.h"
#include <stddef.h>

//debug logging
#define FAT32_DEBUG 1
#if FAT32_DEBUG
#define fat32_debug(msg) serial_write_string("[FAT32] " msg "\n")
#define fat32_debug_val(msg, val) do { \
    serial_write_string("[FAT32] " msg ": "); \
    serial_printf("%d", val); \
    serial_write_string("\n"); \
} while(0)
#define fat32_debug_hex(msg, val) do { \
    serial_write_string("[FAT32] " msg ": 0x"); \
    serial_printf("%x", val); \
    serial_write_string("\n"); \
} while(0)
#else
#define fat32_debug(msg)
#define fat32_debug_val(msg, val)
#define fat32_debug_hex(msg, val)
#endif

//initialize FAT32 driver
int fat32_init(void) {
    fat32_debug("FAT32 driver initialized");
    return 0;
}

//read boot sector and validate FAT32
static int fat32_read_boot_sector(device_t* device, fat32_bpb_t* bpb) {
    if (!device || !bpb) return -1;

    //read boot sector (sector 0)
    char sector[512];
    int r = device_read(device, 0, sector, 512);
    if (r != 512) {
        fat32_debug("Failed to read boot sector");
        return -1;
    }

    //copy BPB from sector
    memcpy(bpb, sector, sizeof(fat32_bpb_t));

    //validate boot signature
    if (sector[510] != 0x55 || sector[511] != (char)0xAA) {
        fat32_debug("Invalid boot signature");
        return -1;
    }

    //validate FAT32 specific fields
    if (bpb->root_entry_count != 0) {
        fat32_debug("root_entry_count must be 0 for FAT32");
        return -1;
    }

    if (bpb->fat_size_16 != 0) {
        fat32_debug("fat_size_16 must be 0 for FAT32");
        return -1;
    }

    if (bpb->total_sectors_16 != 0) {
        fat32_debug("total_sectors_16 should be 0 for FAT32");
    }

    //check filesystem type string (optional check)
    if (memcmp(bpb->fs_type, "FAT32   ", 8) != 0) {
        fat32_debug("Warning: fs_type doesn't say FAT32");
    }

    fat32_debug("Boot sector validated");
    fat32_debug_val("Bytes per sector", bpb->bytes_per_sector);
    fat32_debug_val("Sectors per cluster", bpb->sectors_per_cluster);
    fat32_debug_val("Reserved sectors", bpb->reserved_sectors);
    fat32_debug_val("Number of FATs", bpb->num_fats);
    fat32_debug_val("FAT size (sectors)", bpb->fat_size_32);
    fat32_debug_hex("Root cluster", bpb->root_cluster);

    return 0;
}

//read FSInfo structure
static int fat32_read_fsinfo(device_t* device, fat32_bpb_t* bpb, fat32_fsinfo_t* fsinfo) {
    if (!device || !bpb || !fsinfo) return -1;

    //FSInfo is at sector specified in BPB (usually sector 1)
    uint32_t fsinfo_sector = bpb->fs_info;
    uint32_t fsinfo_offset = fsinfo_sector * bpb->bytes_per_sector;

    char sector[512];
    int r = device_read(device, fsinfo_offset, sector, 512);
    if (r != 512) {
        fat32_debug("Failed to read FSInfo sector");
        return -1;
    }

    //copy FSInfo structure
    memcpy(fsinfo, sector, sizeof(fat32_fsinfo_t));

    //validate signatures
    if (fsinfo->lead_signature != 0x41615252) {
        fat32_debug_hex("Invalid FSInfo lead signature", fsinfo->lead_signature);
        return -1;
    }

    if (fsinfo->struct_signature != 0x61417272) {
        fat32_debug_hex("Invalid FSInfo struct signature", fsinfo->struct_signature);
        return -1;
    }

    if (fsinfo->trail_signature != 0xAA550000) {
        fat32_debug_hex("Invalid FSInfo trail signature", fsinfo->trail_signature);
        return -1;
    }

    fat32_debug("FSInfo validated");
    fat32_debug_val("Free clusters", fsinfo->free_count);
    fat32_debug_val("Next free cluster", fsinfo->next_free);

    return 0;
}

//mount FAT32 filesystem
int fat32_mount(device_t* device, void** mount_data_out) {
    if (!device || !mount_data_out) {
        fat32_debug("Invalid parameters to fat32_mount");
        return -1;
    }

    fat32_debug("Mounting FAT32 filesystem...");

    //allocate mount structure
    fat32_mount_t* mount = (fat32_mount_t*)kmalloc(sizeof(fat32_mount_t));
    if (!mount) {
        fat32_debug("Failed to allocate mount structure");
        return -1;
    }
    memset(mount, 0, sizeof(fat32_mount_t));

    mount->device = device;

    //read and validate boot sector
    if (fat32_read_boot_sector(device, &mount->bpb) != 0) {
        kfree(mount);
        return -1;
    }

    //read FSInfo
    if (fat32_read_fsinfo(device, &mount->bpb, &mount->fsinfo) != 0) {
        fat32_debug("Warning: Failed to read FSInfo (continuing anyway)");
        //not fatal just continue without FSInfo
    }

    //calculate important values
    mount->sectors_per_cluster = mount->bpb.sectors_per_cluster;
    mount->bytes_per_cluster = mount->bpb.bytes_per_sector * mount->sectors_per_cluster;
    mount->root_dir_cluster = mount->bpb.root_cluster;

    //calculate FAT and data region locations
    mount->fat_begin_lba = mount->bpb.reserved_sectors;
    uint32_t fat_size = mount->bpb.fat_size_32;
    uint32_t num_fats = mount->bpb.num_fats;
    mount->cluster_begin_lba = mount->bpb.reserved_sectors + (num_fats * fat_size);

    //calculate total clusters
    uint32_t total_sectors = mount->bpb.total_sectors_32;
    if (total_sectors == 0) {
        total_sectors = mount->bpb.total_sectors_16;
    }
    uint32_t data_sectors = total_sectors - mount->cluster_begin_lba;
    mount->total_clusters = data_sectors / mount->sectors_per_cluster;

    fat32_debug("Mount calculations complete:");
    fat32_debug_val("Bytes per cluster", mount->bytes_per_cluster);
    fat32_debug_val("FAT begin LBA", mount->fat_begin_lba);
    fat32_debug_val("Cluster begin LBA", mount->cluster_begin_lba);
    fat32_debug_val("Total clusters", mount->total_clusters);

    mount->fat_cache = NULL;
    mount->fat_cache_size = 0;

    *mount_data_out = mount;
    fat32_debug("FAT32 mount successful!");
    return 0;
}

//unmount FAT32 filesystem
int fat32_unmount(void* mount_data) {
    if (!mount_data) return -1;

    fat32_mount_t* mount = (fat32_mount_t*)mount_data;

    //write updated FSInfo back to disk
    if (mount->fsinfo.lead_signature == 0x41615252) {
        uint32_t fsinfo_offset = mount->bpb.fs_info * mount->bpb.bytes_per_sector;
        int w = device_write(mount->device, fsinfo_offset, &mount->fsinfo, sizeof(fat32_fsinfo_t));
        if (w == sizeof(fat32_fsinfo_t)) {
            fat32_debug("FSInfo updated on unmount");
        } else {
            fat32_debug("WARNING: Failed to write FSInfo");
        }
    }

    //free FAT cache if allocated
    if (mount->fat_cache) {
        kfree(mount->fat_cache);
    }

    //free mount structure
    kfree(mount);

    fat32_debug("FAT32 unmounted");
    return 0;
}

//convert cluster number to LBA (sector number)
uint32_t fat32_cluster_to_lba(fat32_mount_t* mount, uint32_t cluster) {
    if (!mount) return 0;

    //cluster 0 and 1 are reserved data starts at cluster 2
    if (cluster < 2) return 0;

    uint32_t lba = mount->cluster_begin_lba + ((cluster - 2) * mount->sectors_per_cluster);
    return lba;
}

//read FAT entry for a given cluster
uint32_t fat32_get_fat_entry(fat32_mount_t* mount, uint32_t cluster) {
    if (!mount || cluster < 2 || cluster >= mount->total_clusters + 2) {
        return FAT32_BAD_CLUSTER;
    }

    //calculate FAT sector and offset
    //each FAT entry is 4 bytes (32 bits)
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = mount->fat_begin_lba + (fat_offset / mount->bpb.bytes_per_sector);
    uint32_t entry_offset = fat_offset % mount->bpb.bytes_per_sector;

    //read FAT sector
    char sector[512];
    uint32_t sector_offset = fat_sector * mount->bpb.bytes_per_sector;
    int r = device_read(mount->device, sector_offset, sector, mount->bpb.bytes_per_sector);
    if (r != (int)mount->bpb.bytes_per_sector) {
        fat32_debug("Failed to read FAT sector");
        return FAT32_BAD_CLUSTER;
    }

    //extract 32-bit FAT entry and mask out top 4 bits (only 28 bits used)
    uint32_t* entry_ptr = (uint32_t*)(&sector[entry_offset]);
    uint32_t entry = *entry_ptr & 0x0FFFFFFF;

    return entry;
}

//write FAT entry for a given cluster
int fat32_set_fat_entry(fat32_mount_t* mount, uint32_t cluster, uint32_t value) {
    if (!mount || cluster < 2 || cluster >= mount->total_clusters + 2) {
        return -1;
    }

    //mask value to 28 bits
    value &= 0x0FFFFFFF;

    //calculate FAT sector and offset
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = mount->fat_begin_lba + (fat_offset / mount->bpb.bytes_per_sector);
    uint32_t entry_offset = fat_offset % mount->bpb.bytes_per_sector;

    //read FAT sector
    char sector[512];
    uint32_t sector_offset = fat_sector * mount->bpb.bytes_per_sector;
    int r = device_read(mount->device, sector_offset, sector, mount->bpb.bytes_per_sector);
    if (r != (int)mount->bpb.bytes_per_sector) {
        fat32_debug("Failed to read FAT sector");
        return -1;
    }

    //update entry (preserve top 4 bits)
    uint32_t* entry_ptr = (uint32_t*)(&sector[entry_offset]);
    *entry_ptr = (*entry_ptr & 0xF0000000) | value;

    //write back FAT sector to all FAT copies
    for (uint32_t i = 0; i < mount->bpb.num_fats; i++) {
        uint32_t fat_base = mount->fat_begin_lba + (i * mount->bpb.fat_size_32);
        uint32_t write_sector = fat_base + (fat_offset / mount->bpb.bytes_per_sector);
        uint32_t write_offset = write_sector * mount->bpb.bytes_per_sector;

        int w = device_write(mount->device, write_offset, sector, mount->bpb.bytes_per_sector);
        if (w != (int)mount->bpb.bytes_per_sector) {
            fat32_debug("Failed to write FAT sector");
            return -1;
        }
    }

    return 0;
}

//allocate a new cluster from the FAT
uint32_t fat32_allocate_cluster(fat32_mount_t* mount) {
    if (!mount) return 0;

    //start search from FSInfo hint if available
    uint32_t start_cluster = 2;
    if (mount->fsinfo.next_free != 0xFFFFFFFF && mount->fsinfo.next_free >= 2) {
        start_cluster = mount->fsinfo.next_free;
    }

    //search for free cluster
    uint32_t cluster = start_cluster;
    for (uint32_t i = 0; i < mount->total_clusters; i++) {
        uint32_t entry = fat32_get_fat_entry(mount, cluster);
        if (entry == FAT32_FREE_CLUSTER) {
            //mark as end of chain
            if (fat32_set_fat_entry(mount, cluster, FAT32_EOC) != 0) {
                return 0;
            }

            //update FSInfo
            if (mount->fsinfo.free_count != 0xFFFFFFFF && mount->fsinfo.free_count > 0) {
                mount->fsinfo.free_count--;
            }
            mount->fsinfo.next_free = cluster + 1;

            fat32_debug_hex("Allocated cluster", cluster);
            return cluster;
        }

        cluster++;
        if (cluster >= mount->total_clusters + 2) {
            cluster = 2;
        }
        if (cluster == start_cluster) {
            break; //Wrapped around, no free clusters
        }
    }

    fat32_debug("No free clusters available");
    return 0;
}

//free a cluster chain starting from the given cluster
int fat32_free_cluster_chain(fat32_mount_t* mount, uint32_t start_cluster) {
    if (!mount || start_cluster < 2) return -1;

    uint32_t cluster = start_cluster;
    uint32_t freed_count = 0;

    while (cluster >= 2 && cluster < FAT32_EOC_MIN) {
        //get next cluster in chain
        uint32_t next_cluster = fat32_get_fat_entry(mount, cluster);

        //mark current cluster as free
        if (fat32_set_fat_entry(mount, cluster, FAT32_FREE_CLUSTER) != 0) {
            return -1;
        }

        freed_count++;
        cluster = next_cluster;
    }

    //update FSInfo
    if (mount->fsinfo.free_count != 0xFFFFFFFF) {
        mount->fsinfo.free_count += freed_count;
    }

    fat32_debug_val("Freed clusters", freed_count);
    return 0;
}

//read an entire cluster
int fat32_read_cluster(fat32_mount_t* mount, uint32_t cluster, void* buffer) {
    if (!mount || !buffer || cluster < 2) return -1;

    uint32_t lba = fat32_cluster_to_lba(mount, cluster);
    if (lba == 0) return -1;

    uint32_t offset = lba * mount->bpb.bytes_per_sector;
    int r = device_read(mount->device, offset, buffer, mount->bytes_per_cluster);

    return (r == (int)mount->bytes_per_cluster) ? 0 : -1;
}

//write an entire cluster
int fat32_write_cluster(fat32_mount_t* mount, uint32_t cluster, const void* buffer) {
    if (!mount || !buffer || cluster < 2) return -1;

    uint32_t lba = fat32_cluster_to_lba(mount, cluster);
    if (lba == 0) return -1;

    uint32_t offset = lba * mount->bpb.bytes_per_sector;
    int w = device_write(mount->device, offset, buffer, mount->bytes_per_cluster);

    return (w == (int)mount->bytes_per_cluster) ? 0 : -1;
}

//calculate checksum for short name (used in LFN entries)
static uint8_t fat32_lfn_checksum(const uint8_t* short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) << 7) + (sum >> 1) + short_name[i];
    }
    return sum;
}

//convert UTF-16 character to ASCII
static char fat32_utf16_to_ascii(uint16_t c) {
    if (c == 0 || c == 0xFFFF) return 0;
    if (c < 128) return (char)c;
    return '?'; //non-ASCII character
}

//extract filename from LFN entries
static void fat32_extract_lfn(fat32_lfn_entry_t* lfn, char* name, int* pos) {
    //LFN entries store 13 characters in UTF-16
    for (int i = 0; i < 5; i++) {
        char c = fat32_utf16_to_ascii(lfn->name1[i]);
        if (c == 0) return;
        name[(*pos)++] = c;
    }
    for (int i = 0; i < 6; i++) {
        char c = fat32_utf16_to_ascii(lfn->name2[i]);
        if (c == 0) return;
        name[(*pos)++] = c;
    }
    for (int i = 0; i < 2; i++) {
        char c = fat32_utf16_to_ascii(lfn->name3[i]);
        if (c == 0) return;
        name[(*pos)++] = c;
    }
}

//convert 8.3 name to regular filename
static void fat32_83_to_name(const uint8_t* short_name, char* name) {
    int pos = 0;

    //copy name part (8 chars)
    for (int i = 0; i < 8; i++) {
        if (short_name[i] != ' ') {
            name[pos++] = (char)short_name[i];
        }
    }

    //add extension if present
    if (short_name[8] != ' ') {
        name[pos++] = '.';
        for (int i = 8; i < 11; i++) {
            if (short_name[i] != ' ') {
                name[pos++] = (char)short_name[i];
            }
        }
    }

    name[pos] = '\0';
}

//structure to hold directory entry iteration state
typedef struct {
    fat32_mount_t* mount;
    uint32_t dir_cluster;
    uint32_t current_cluster;
    uint32_t cluster_offset;  //offset within current cluster
    char* cluster_buffer;     //buffer for current cluster

    //LFN accumulation
    char lfn_name[256];
    int lfn_pos;
    uint8_t lfn_checksum;
} fat32_dir_iter_t;

//initialize directory iterator
static fat32_dir_iter_t* fat32_dir_iter_init(fat32_mount_t* mount, uint32_t dir_cluster) {
    if (!mount || dir_cluster < 2) return NULL;

    fat32_dir_iter_t* iter = (fat32_dir_iter_t*)kmalloc(sizeof(fat32_dir_iter_t));
    if (!iter) return NULL;

    memset(iter, 0, sizeof(fat32_dir_iter_t));
    iter->mount = mount;
    iter->dir_cluster = dir_cluster;
    iter->current_cluster = dir_cluster;
    iter->cluster_offset = 0;

    //allocate cluster buffer
    iter->cluster_buffer = (char*)kmalloc(mount->bytes_per_cluster);
    if (!iter->cluster_buffer) {
        kfree(iter);
        return NULL;
    }

    //read first cluster
    if (fat32_read_cluster(mount, dir_cluster, iter->cluster_buffer) != 0) {
        kfree(iter->cluster_buffer);
        kfree(iter);
        return NULL;
    }

    return iter;
}

//free directory iterator
static void fat32_dir_iter_free(fat32_dir_iter_t* iter) {
    if (!iter) return;
    if (iter->cluster_buffer) kfree(iter->cluster_buffer);
    kfree(iter);
}

//get next directory entry
static int fat32_dir_iter_next(fat32_dir_iter_t* iter, fat32_dir_entry_t** entry_out, char* name_out) {
    if (!iter || !entry_out) return -1;

    while (1) {
        //check if we need to read next cluster
        if (iter->cluster_offset >= iter->mount->bytes_per_cluster) {
            //move to next cluster in chain
            uint32_t next_cluster = fat32_get_fat_entry(iter->mount, iter->current_cluster);
            if (next_cluster < 2 || next_cluster >= FAT32_EOC_MIN) {
                return -1; //end of directory
            }

            iter->current_cluster = next_cluster;
            iter->cluster_offset = 0;

            if (fat32_read_cluster(iter->mount, next_cluster, iter->cluster_buffer) != 0) {
                return -1;
            }
        }

        fat32_dir_entry_t* entry = (fat32_dir_entry_t*)(iter->cluster_buffer + iter->cluster_offset);
        iter->cluster_offset += sizeof(fat32_dir_entry_t);

        //check for end of directory
        if (entry->name[0] == 0x00) {
            return -1; //no more entries
        }

        //skip deleted entries
        if (entry->name[0] == 0xE5) {
            continue;
        }

        //check for LFN entry
        if (entry->attr == FAT32_ATTR_LONG_NAME) {
            fat32_lfn_entry_t* lfn = (fat32_lfn_entry_t*)entry;

            //check if this is the first LFN entry (highest sequence number)
            if (lfn->order & 0x40) {
                //start of new LFN sequence
                iter->lfn_pos = 0;
                memset(iter->lfn_name, 0, sizeof(iter->lfn_name));
                iter->lfn_checksum = lfn->checksum;
            }

            //extract characters from this LFN entry
            fat32_extract_lfn(lfn, iter->lfn_name, &iter->lfn_pos);
            continue;
        }

        //skip volume labels
        if (entry->attr & FAT32_ATTR_VOLUME_ID) {
            continue;
        }

        //regular entry - check if we have a valid LFN
        if (iter->lfn_pos > 0) {
            //verify checksum
            uint8_t checksum = fat32_lfn_checksum(entry->name);
            if (checksum == iter->lfn_checksum) {
                //use LFN
                if (name_out) {
                    strncpy(name_out, iter->lfn_name, 255);
                    name_out[255] = '\0';
                }
            } else {
                //checksum mismatch, use 8.3 name
                if (name_out) {
                    fat32_83_to_name(entry->name, name_out);
                }
            }
            //reset LFN state
            iter->lfn_pos = 0;
        } else {
            //no LFN, use 8.3 name
            if (name_out) {
                fat32_83_to_name(entry->name, name_out);
            }
        }

        *entry_out = entry;
        return 0;
    }
}

//find a file/directory in a directory cluster
int fat32_find_in_dir(fat32_mount_t* mount, uint32_t dir_cluster, const char* name,
                             fat32_dir_entry_t* entry_out) {
    if (!mount || !name || !entry_out) return -1;

    fat32_dir_iter_t* iter = fat32_dir_iter_init(mount, dir_cluster);
    if (!iter) return -1;

    int found = -1;
    fat32_dir_entry_t* entry;
    char entry_name[256];

    while (fat32_dir_iter_next(iter, &entry, entry_name) == 0) {
        //case-insensitive comparison
        int match = 1;
        for (int i = 0; name[i] || entry_name[i]; i++) {
            char c1 = name[i];
            char c2 = entry_name[i];
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) {
                match = 0;
                break;
            }
        }

        if (match) {
            memcpy(entry_out, entry, sizeof(fat32_dir_entry_t));
            found = 0;
            break;
        }
    }

    fat32_dir_iter_free(iter);
    return found;
}

//get the nth directory entry
int fat32_get_dir_entry(fat32_mount_t* mount, uint32_t dir_cluster, uint32_t index,
                        fat32_dir_entry_t* entry_out, char* name_out) {
    if (!mount || !entry_out) return -1;

    fat32_dir_iter_t* iter = fat32_dir_iter_init(mount, dir_cluster);
    if (!iter) return -1;

    fat32_dir_entry_t* entry;
    char entry_name[256];
    uint32_t current_index = 0;

    //skip to the requested index
    while (fat32_dir_iter_next(iter, &entry, entry_name) == 0) {
        if (current_index == index) {
            //found it
            memcpy(entry_out, entry, sizeof(fat32_dir_entry_t));
            if (name_out) {
                strncpy(name_out, entry_name, 255);
                name_out[255] = '\0';
            }
            fat32_dir_iter_free(iter);
            return 0;
        }
        current_index++;
    }

    //index out of range
    fat32_dir_iter_free(iter);
    return -1;
}

//read data from a file following cluster chain
int fat32_read_file_data(fat32_mount_t* mount, uint32_t start_cluster, uint32_t offset,
                                uint32_t size, char* buffer) {
    if (!mount || !buffer || start_cluster < 2) return -1;
    if (size == 0) return 0;

    uint32_t bytes_per_cluster = mount->bytes_per_cluster;
    uint32_t current_cluster = start_cluster;
    uint32_t skip_clusters = offset / bytes_per_cluster;
    uint32_t cluster_offset = offset % bytes_per_cluster;
    uint32_t bytes_read = 0;

    //skip to starting cluster
    for (uint32_t i = 0; i < skip_clusters; i++) {
        current_cluster = fat32_get_fat_entry(mount, current_cluster);
        if (current_cluster < 2 || current_cluster >= FAT32_EOC_MIN) {
            return -1; //offset beyond file size
        }
    }

    //allocate cluster buffer
    char* cluster_buf = (char*)kmalloc(bytes_per_cluster);
    if (!cluster_buf) return -1;

    //read data
    while (bytes_read < size && current_cluster >= 2 && current_cluster < FAT32_EOC_MIN) {
        //read cluster
        if (fat32_read_cluster(mount, current_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }

        //copy requested portion
        uint32_t to_copy = bytes_per_cluster - cluster_offset;
        if (to_copy > size - bytes_read) {
            to_copy = size - bytes_read;
        }

        memcpy(buffer + bytes_read, cluster_buf + cluster_offset, to_copy);
        bytes_read += to_copy;
        cluster_offset = 0; //only applies to first cluster

        //move to next cluster if needed
        if (bytes_read < size) {
            current_cluster = fat32_get_fat_entry(mount, current_cluster);
        }
    }

    kfree(cluster_buf);
    return (int)bytes_read;
}

//write data to a file extending cluster chain as needed
int fat32_write_file_data(fat32_mount_t* mount, uint32_t* start_cluster, uint32_t offset,
                                 uint32_t size, const char* buffer) {
    if (!mount || !buffer || !start_cluster) return -1;
    if (size == 0) return 0;

    uint32_t bytes_per_cluster = mount->bytes_per_cluster;
    uint32_t skip_clusters = offset / bytes_per_cluster;
    uint32_t cluster_offset = offset % bytes_per_cluster;
    uint32_t bytes_written = 0;

    //allocate first cluster if needed
    if (*start_cluster == 0) {
        *start_cluster = fat32_allocate_cluster(mount);
        if (*start_cluster == 0) return -1;
    }

    //navigate to starting cluster allocating if necessary
    uint32_t current_cluster = *start_cluster;
    uint32_t prev_cluster = 0;

    for (uint32_t i = 0; i < skip_clusters; i++) {
        prev_cluster = current_cluster;
        current_cluster = fat32_get_fat_entry(mount, current_cluster);

        if (current_cluster < 2 || current_cluster >= FAT32_EOC_MIN) {
            //need to allocate new cluster
            current_cluster = fat32_allocate_cluster(mount);
            if (current_cluster == 0) return -1;

            //link it
            fat32_set_fat_entry(mount, prev_cluster, current_cluster);
        }
    }

    //allocate cluster buffer
    char* cluster_buf = (char*)kmalloc(bytes_per_cluster);
    if (!cluster_buf) return -1;

    //write data
    while (bytes_written < size) {
        //read-modify-write if not writing full cluster
        if (cluster_offset != 0 || size - bytes_written < bytes_per_cluster) {
            if (fat32_read_cluster(mount, current_cluster, cluster_buf) != 0) {
                //cluster might be uninitialized so zero it
                memset(cluster_buf, 0, bytes_per_cluster);
            }
        }

        //copy data to cluster buffer
        uint32_t to_write = bytes_per_cluster - cluster_offset;
        if (to_write > size - bytes_written) {
            to_write = size - bytes_written;
        }

        memcpy(cluster_buf + cluster_offset, buffer + bytes_written, to_write);

        //write cluster back
        if (fat32_write_cluster(mount, current_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }

        bytes_written += to_write;
        cluster_offset = 0;

        //does it need next cluster?
        if (bytes_written < size) {
            prev_cluster = current_cluster;
            current_cluster = fat32_get_fat_entry(mount, current_cluster);

            if (current_cluster < 2 || current_cluster >= FAT32_EOC_MIN) {
                //allocate and link new cluster
                current_cluster = fat32_allocate_cluster(mount);
                if (current_cluster == 0) {
                    kfree(cluster_buf);
                    return -1;
                }
                fat32_set_fat_entry(mount, prev_cluster, current_cluster);
            }
        }
    }

    kfree(cluster_buf);
    return (int)bytes_written;
}

//convert date/time to FAT32 format
static uint16_t fat32_encode_time(unsigned hour, unsigned minute, unsigned second) {
    return ((hour & 0x1F) << 11) | ((minute & 0x3F) << 5) | ((second / 2) & 0x1F);
}

static uint16_t fat32_encode_date(unsigned year, unsigned month, unsigned day) {
    //year is stored as offset from 1980
    unsigned year_offset = (year >= 1980) ? (year - 1980) : 0;
    return ((year_offset & 0x7F) << 9) | ((month & 0x0F) << 5) | (day & 0x1F);
}

//get current timestamp from RTC and encode for FAT32
static void fat32_get_current_timestamp(uint16_t* date_out, uint16_t* time_out) {
    rtc_time_t rtc;
    if (rtc_read(&rtc) == 1) {
        *date_out = fat32_encode_date(rtc.year, rtc.month, rtc.day);
        *time_out = fat32_encode_time(rtc.hour, rtc.minute, rtc.second);
    } else {
        //fallback: use epoch date (Jan 1, 1980)
        *date_out = fat32_encode_date(1980, 1, 1);
        *time_out = fat32_encode_time(0, 0, 0);
    }
}

//generate 8.3 basis name from long filename (for LFN)
static void fat32_generate_basis_name(const char* lfn, uint8_t* basis_name) {
    memset(basis_name, ' ', 11);

    //find extension
    const char* ext = NULL;
    for (const char* p = lfn; *p; p++) {
        if (*p == '.') ext = p;
    }

    //copy base name (up to 6 chars to leave room for ~N)
    int pos = 0;
    for (const char* p = lfn; *p && p != ext && pos < 6; p++) {
        char c = *p;
        if (c == ' ' || c == '.') continue; //skip invalid chars
        if (c >= 'a' && c <= 'z') c -= 32; //uppercase
        basis_name[pos++] = c;
    }

    //add ~1 numeric tail (should check for collisions, but simplified for now)
    basis_name[pos++] = '~';
    basis_name[pos++] = '1';

    //copy extension
    if (ext) {
        int ext_pos = 0;
        for (const char* p = ext + 1; *p && ext_pos < 3; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c -= 32;
            basis_name[8 + ext_pos++] = c;
        }
    }
}

//check if filename needs LFN entries (contains lowercase, >8.3, etc)
static int fat32_needs_lfn(const char* name) {
    int len = 0;
    int has_dot = 0;
    int before_dot = 0;
    int after_dot = 0;

    for (const char* p = name; *p; p++) {
        len++;
        if (*p == '.') {
            if (has_dot) return 1; //multiple dots need LFN
            has_dot = 1;
        } else {
            if (has_dot) after_dot++;
            else before_dot++;

            //lowercase needs LFN
            if (*p >= 'a' && *p <= 'z') return 1;
            //spaces need LFN
            if (*p == ' ') return 1;
        }
    }

    //check 8.3 limits
    if (before_dot > 8 || after_dot > 3) return 1;

    return 0;
}

//convert filename to 8.3 format (for simple names that don't need LFN)
static void fat32_name_to_83(const char* name, uint8_t* fat_name) {
    memset(fat_name, ' ', 11);

    const char* dot = NULL;
    for (const char* p = name; *p; p++) {
        if (*p == '.') dot = p;
    }

    //copy name part (up to 8 chars)
    int i = 0;
    for (const char* p = name; *p && p != dot && i < 8; p++, i++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c -= 32; //uppercase
        fat_name[i] = c;
    }

    //copy extension (up to 3 chars)
    if (dot) {
        int j = 0;
        for (const char* p = dot + 1; *p && j < 3; p++, j++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c -= 32; //uppercase
            fat_name[8 + j] = c;
        }
    }
}

//generate LFN entries for a long filename
static int fat32_generate_lfn_entries(const char* lfn, fat32_lfn_entry_t* lfn_entries,
                                      uint8_t* basis_name, int max_entries) {
    int lfn_len = 0;
    for (const char* p = lfn; *p; p++) lfn_len++;

    //calculate number of LFN entries needed (13 chars per entry)
    int num_lfn = (lfn_len + 12) / 13;
    if (num_lfn > max_entries) return -1;

    //generate basis name
    fat32_generate_basis_name(lfn, basis_name);

    //calculate checksum
    uint8_t checksum = fat32_lfn_checksum(basis_name);

    //generate LFN entries
    //entries are stored in reverse order on disk (last chars first)
    //but characters within each entry are in forward order
    for (int i = 0; i < num_lfn; i++) {
        fat32_lfn_entry_t* entry = &lfn_entries[num_lfn - 1 - i];
        memset(entry, 0xFF, sizeof(fat32_lfn_entry_t)); //initialize with 0xFFFF

        entry->attr = FAT32_ATTR_LONG_NAME;
        entry->type = 0;
        entry->checksum = checksum;
        entry->first_cluster = 0;

        //set sequence number (entry 0 has highest sequence number)
        entry->order = i + 1;
        if (i == num_lfn - 1) entry->order |= 0x40; //last entry marker

        //fill in characters for this entry (13 chars per entry)
        int char_start = i * 13;
        int char_end = (i + 1) * 13;
        if (char_end > lfn_len) char_end = lfn_len;

        int char_pos = char_start;

        //name1: 5 chars
        for (int j = 0; j < 5; j++) {
            if (char_pos < lfn_len) {
                entry->name1[j] = (uint16_t)lfn[char_pos++];
            } else if (char_pos == lfn_len) {
                entry->name1[j] = 0x0000; //null terminator
                char_pos++;
            } else {
                entry->name1[j] = 0xFFFF; //padding
            }
        }

        //name2: 6 chars
        for (int j = 0; j < 6; j++) {
            if (char_pos < lfn_len) {
                entry->name2[j] = (uint16_t)lfn[char_pos++];
            } else if (char_pos == lfn_len) {
                entry->name2[j] = 0x0000;
                char_pos++;
            } else {
                entry->name2[j] = 0xFFFF;
            }
        }

        //name3: 2 chars
        for (int j = 0; j < 2; j++) {
            if (char_pos < lfn_len) {
                entry->name3[j] = (uint16_t)lfn[char_pos++];
            } else if (char_pos == lfn_len) {
                entry->name3[j] = 0x0000;
                char_pos++;
            } else {
                entry->name3[j] = 0xFFFF;
            }
        }
    }

    return num_lfn;
}

//create a new file in a directory
int fat32_create_file(fat32_mount_t* mount, uint32_t dir_cluster, const char* filename) {
    if (!mount || !filename || dir_cluster < 2) return -1;

    fat32_debug("Creating file");

    //check if we need LFN entries
    int needs_lfn = fat32_needs_lfn(filename);
    int num_lfn = 0;
    fat32_lfn_entry_t lfn_entries[20]; //max 20 LFN entries (255 char name)
    uint8_t fat_name[11];

    if (needs_lfn) {
        //generate LFN entries and basis name
        num_lfn = fat32_generate_lfn_entries(filename, lfn_entries, fat_name, 20);
        if (num_lfn < 0) {
            fat32_debug("ERROR: Filename too long");
            return -1;
        }
    } else {
        //simple 8.3 name
        fat32_name_to_83(filename, fat_name);
    }

    //total entries needed (LFN entries + 1 short entry)
    int entries_needed = num_lfn + 1;

    fat32_debug_val("Bytes per cluster", mount->bytes_per_cluster);
    fat32_debug("Allocating cluster buffer");

    //find contiguous free directory entries
    char* cluster_buf = (char*)kmalloc(mount->bytes_per_cluster);
    if (!cluster_buf) {
        fat32_debug("ERROR: kmalloc failed");
        return -1;
    }
    fat32_debug("Cluster buffer allocated");

    uint32_t current_cluster = dir_cluster;
    uint32_t entries_per_cluster = mount->bytes_per_cluster / sizeof(fat32_dir_entry_t);

    fat32_debug_hex("Starting dir cluster", current_cluster);
    fat32_debug_val("Entries per cluster", entries_per_cluster);

    uint32_t loop_count = 0;
    while (current_cluster >= 2 && current_cluster < FAT32_EOC_MIN) {
        loop_count++;
        fat32_debug_hex("Reading directory cluster", current_cluster);
        fat32_debug_val("Loop iteration", loop_count);

        //prevent infinite loops due to corrupted FAT
        if (loop_count > mount->total_clusters) {
            fat32_debug("ERROR: Loop limit exceeded (possible FAT corruption)");
            kfree(cluster_buf);
            return -1;
        }

        //read directory cluster
        if (fat32_read_cluster(mount, current_cluster, cluster_buf) != 0) {
            fat32_debug("ERROR: Failed to read cluster");
            kfree(cluster_buf);
            return -1;
        }

        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;

        //find contiguous free slots
        for (uint32_t i = 0; i <= entries_per_cluster - entries_needed; i++) {
            //check if we have enough contiguous free entries
            int found = 1;
            for (int j = 0; j < entries_needed; j++) {
                if (entries[i + j].name[0] != 0x00 && entries[i + j].name[0] != 0xE5) {
                    found = 0;
                    break;
                }
            }

            if (found) {
                //write LFN entries first (if any)
                for (int j = 0; j < num_lfn; j++) {
                    memcpy(&entries[i + j], &lfn_entries[j], sizeof(fat32_lfn_entry_t));
                }

                //write short entry
                memset(&entries[i + num_lfn], 0, sizeof(fat32_dir_entry_t));
                memcpy(entries[i + num_lfn].name, fat_name, 11);
                entries[i + num_lfn].attr = FAT32_ATTR_ARCHIVE;
                entries[i + num_lfn].first_cluster_hi = 0;
                entries[i + num_lfn].first_cluster_lo = 0;
                entries[i + num_lfn].file_size = 0;
                
                //set timestamps
                uint16_t date, time;
                fat32_get_current_timestamp(&date, &time);
                entries[i + num_lfn].create_date = date;
                entries[i + num_lfn].create_time = time;
                entries[i + num_lfn].write_date = date;
                entries[i + num_lfn].write_time = time;
                entries[i + num_lfn].access_date = date;

                //write cluster back
                if (fat32_write_cluster(mount, current_cluster, cluster_buf) != 0) {
                    kfree(cluster_buf);
                    return -1;
                }

                kfree(cluster_buf);
                fat32_debug("File created successfully");
                return 0;
            }
        }

        //move to next cluster
        uint32_t next_cluster = fat32_get_fat_entry(mount, current_cluster);
        if (next_cluster < 2 || next_cluster >= FAT32_EOC_MIN) {
            //need to allocate new directory cluster
            next_cluster = fat32_allocate_cluster(mount);
            if (next_cluster == 0) {
                kfree(cluster_buf);
                return -1;
            }

            //link it
            fat32_set_fat_entry(mount, current_cluster, next_cluster);

            //zero new cluster
            memset(cluster_buf, 0, mount->bytes_per_cluster);
            if (fat32_write_cluster(mount, next_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                return -1;
            }
        }
        current_cluster = next_cluster;
    }

    kfree(cluster_buf);
    fat32_debug("Directory full - could not create file");
    return -1;
}

//delete a file from a directory
int fat32_delete_file(fat32_mount_t* mount, uint32_t dir_cluster, const char* filename) {
    if (!mount || !filename || dir_cluster < 2) return -1;

    fat32_debug("Deleting file");

    //first use fat32_find_in_dir to locate the file and get its cluster
    extern int fat32_find_in_dir(fat32_mount_t* mount, uint32_t dir_cluster,
                                  const char* name, fat32_dir_entry_t* entry_out);

    fat32_dir_entry_t file_entry;
    if (fat32_find_in_dir(mount, dir_cluster, filename, &file_entry) != 0) {
        fat32_debug("File not found");
        return -1;
    }

    //don't delete directories
    if (file_entry.attr & FAT32_ATTR_DIRECTORY) {
        fat32_debug("Cannot unlink directory");
        return -1;
    }

    //get the file's start cluster
    uint32_t file_start_cluster = ((uint32_t)file_entry.first_cluster_hi << 16) | file_entry.first_cluster_lo;

    //now search through the directory again to mark entries as deleted
    char* cluster_buf = (char*)kmalloc(mount->bytes_per_cluster);
    if (!cluster_buf) {
        fat32_debug("ERROR: kmalloc failed");
        return -1;
    }

    uint32_t current_cluster = dir_cluster;
    uint32_t entries_per_cluster = mount->bytes_per_cluster / sizeof(fat32_dir_entry_t);
    int found_and_deleted = 0;

    uint32_t loop_count = 0;
    while (current_cluster >= 2 && current_cluster < FAT32_EOC_MIN) {
        loop_count++;

        //prevent infinite loops due to corrupted FAT
        if (loop_count > mount->total_clusters) {
            fat32_debug("ERROR: Loop limit exceeded (possible FAT corruption)");
            kfree(cluster_buf);
            return -1;
        }

        //read directory cluster
        if (fat32_read_cluster(mount, current_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }

        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;
        int modified = 0;

        //look for LFN entries and the short entry to delete
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = &entries[i];

            //end of directory
            if (entry->name[0] == 0x00) break;

            //skip already deleted
            if (entry->name[0] == 0xE5) continue;

            //check if this is the short entry we're looking for
            if (entry->attr != FAT32_ATTR_LONG_NAME) {
                //compare the entry
                int match = 1;
                for (int j = 0; j < 11; j++) {
                    if (entry->name[j] != file_entry.name[j]) {
                        match = 0;
                        break;
                    }
                }

                if (match) {
                    //mark this entry and any preceding LFN entries as deleted
                    entry->name[0] = 0xE5;
                    modified = 1;

                    //also mark preceding LFN entries as deleted
                    for (int j = (int)i - 1; j >= 0; j--) {
                        if (entries[j].attr == FAT32_ATTR_LONG_NAME) {
                            entries[j].name[0] = 0xE5;
                        } else {
                            break; //stop at first non-LFN entry
                        }
                    }

                    found_and_deleted = 1;
                    break;
                }
            }
        }

        //write cluster back if modified
        if (modified) {
            if (fat32_write_cluster(mount, current_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                return -1;
            }
            break; //found and deleted we're done
        }

        //move to next cluster
        uint32_t next_cluster = fat32_get_fat_entry(mount, current_cluster);
        if (next_cluster < 2 || next_cluster >= FAT32_EOC_MIN) break;
        current_cluster = next_cluster;
    }

    kfree(cluster_buf);

    if (!found_and_deleted) {
        fat32_debug("Failed to delete directory entry");
        return -1;
    }

    //free the file cluster chain if it has one
    if (file_start_cluster >= 2 && file_start_cluster < FAT32_EOC_MIN) {
        if (fat32_free_cluster_chain(mount, file_start_cluster) != 0) {
            fat32_debug("WARNING: Failed to free cluster chain");
            //continue anyway directory entry is already deleted
        }
    }

    fat32_debug("File deleted successfully");
    return 0;
}

//create a directory in a parent directory
int fat32_create_directory(fat32_mount_t* mount, uint32_t parent_cluster, const char* dirname) {
    if (!mount || !dirname || parent_cluster < 2) return -1;

    fat32_debug("Creating directory");

    //check if we need LFN entries
    int needs_lfn = fat32_needs_lfn(dirname);
    int num_lfn = 0;
    fat32_lfn_entry_t lfn_entries[20];
    uint8_t fat_name[11];

    if (needs_lfn) {
        num_lfn = fat32_generate_lfn_entries(dirname, lfn_entries, fat_name, 20);
        if (num_lfn < 0) {
            fat32_debug("ERROR: Directory name too long");
            return -1;
        }
    } else {
        fat32_name_to_83(dirname, fat_name);
    }

    //allocate a cluster for the new directory's contents
    uint32_t dir_cluster = fat32_allocate_cluster(mount);
    if (dir_cluster == 0) {
        fat32_debug("ERROR: Failed to allocate cluster for directory");
        return -1;
    }

    //initialize the directory cluster with . and .. entries
    char* cluster_buf = (char*)kmalloc(mount->bytes_per_cluster);
    if (!cluster_buf) {
        fat32_free_cluster_chain(mount, dir_cluster);
        return -1;
    }
    memset(cluster_buf, 0, mount->bytes_per_cluster);

    fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;

    //create . entry (current directory)
    memset(&entries[0], 0, sizeof(fat32_dir_entry_t));
    memcpy(entries[0].name, ".          ", 11);
    entries[0].attr = FAT32_ATTR_DIRECTORY;
    entries[0].first_cluster_hi = (dir_cluster >> 16) & 0xFFFF;
    entries[0].first_cluster_lo = dir_cluster & 0xFFFF;
    entries[0].file_size = 0;

    //create .. entry (parent directory)
    memset(&entries[1], 0, sizeof(fat32_dir_entry_t));
    memcpy(entries[1].name, "..         ", 11);
    entries[1].attr = FAT32_ATTR_DIRECTORY;
    //for root directory parent use cluster 0
    uint32_t parent_for_dotdot = (parent_cluster == mount->root_dir_cluster) ? 0 : parent_cluster;
    entries[1].first_cluster_hi = (parent_for_dotdot >> 16) & 0xFFFF;
    entries[1].first_cluster_lo = parent_for_dotdot & 0xFFFF;
    entries[1].file_size = 0;

    //write the initialized directory cluster
    if (fat32_write_cluster(mount, dir_cluster, cluster_buf) != 0) {
        kfree(cluster_buf);
        fat32_free_cluster_chain(mount, dir_cluster);
        return -1;
    }

    //now add the directory entry to the parent directory
    //similar to file creation but with DIRECTORY attribute
    int entries_needed = num_lfn + 1;
    uint32_t current_cluster = parent_cluster;
    uint32_t entries_per_cluster = mount->bytes_per_cluster / sizeof(fat32_dir_entry_t);

    uint32_t loop_count = 0;
    while (current_cluster >= 2 && current_cluster < FAT32_EOC_MIN) {
        loop_count++;

        //prevent infinite loops due to corrupted FAT
        if (loop_count > mount->total_clusters) {
            fat32_debug("ERROR: Loop limit exceeded (possible FAT corruption)");
            kfree(cluster_buf);
            fat32_free_cluster_chain(mount, dir_cluster);
            return -1;
        }

        //read parent directory cluster
        if (fat32_read_cluster(mount, current_cluster, cluster_buf) != 0) {
            fat32_debug("ERROR: Failed to read parent cluster");
            kfree(cluster_buf);
            fat32_free_cluster_chain(mount, dir_cluster);
            return -1;
        }

        fat32_dir_entry_t* parent_entries = (fat32_dir_entry_t*)cluster_buf;

        //find contiguous free slots
        for (uint32_t i = 0; i <= entries_per_cluster - entries_needed; i++) {
            //check if we have enough contiguous free entries
            int found = 1;
            for (int j = 0; j < entries_needed; j++) {
                if (parent_entries[i + j].name[0] != 0x00 && parent_entries[i + j].name[0] != 0xE5) {
                    found = 0;
                    break;
                }
            }

            if (found) {
                //write LFN entries first (if any)
                for (int j = 0; j < num_lfn; j++) {
                    memcpy(&parent_entries[i + j], &lfn_entries[j], sizeof(fat32_lfn_entry_t));
                }

                //write short entry for directory
                memset(&parent_entries[i + num_lfn], 0, sizeof(fat32_dir_entry_t));
                memcpy(parent_entries[i + num_lfn].name, fat_name, 11);
                parent_entries[i + num_lfn].attr = FAT32_ATTR_DIRECTORY;
                parent_entries[i + num_lfn].first_cluster_hi = (dir_cluster >> 16) & 0xFFFF;
                parent_entries[i + num_lfn].first_cluster_lo = dir_cluster & 0xFFFF;
                parent_entries[i + num_lfn].file_size = 0; //directories have size 0
                
                //set timestamps
                uint16_t date, time;
                fat32_get_current_timestamp(&date, &time);
                parent_entries[i + num_lfn].create_date = date;
                parent_entries[i + num_lfn].create_time = time;
                parent_entries[i + num_lfn].write_date = date;
                parent_entries[i + num_lfn].write_time = time;
                parent_entries[i + num_lfn].access_date = date;

                //write parent cluster back
                if (fat32_write_cluster(mount, current_cluster, cluster_buf) != 0) {
                    kfree(cluster_buf);
                    fat32_free_cluster_chain(mount, dir_cluster);
                    return -1;
                }

                kfree(cluster_buf);
                fat32_debug("Directory created successfully");
                return 0;
            }
        }

        //move to next cluster
        uint32_t next_cluster = fat32_get_fat_entry(mount, current_cluster);
        if (next_cluster < 2 || next_cluster >= FAT32_EOC_MIN) {
            //need to allocate new directory cluster for parent
            next_cluster = fat32_allocate_cluster(mount);
            if (next_cluster == 0) {
                kfree(cluster_buf);
                fat32_free_cluster_chain(mount, dir_cluster);
                return -1;
            }

            //link it
            fat32_set_fat_entry(mount, current_cluster, next_cluster);

            //zero new cluster
            memset(cluster_buf, 0, mount->bytes_per_cluster);
            if (fat32_write_cluster(mount, next_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                fat32_free_cluster_chain(mount, dir_cluster);
                return -1;
            }
        }
        current_cluster = next_cluster;
    }

    kfree(cluster_buf);
    fat32_free_cluster_chain(mount, dir_cluster);
    fat32_debug("Parent directory full - could not create directory");
    return -1;
}

//delete a directory (must be empty except for . and ..)
int fat32_delete_directory(fat32_mount_t* mount, uint32_t parent_cluster, const char* dirname) {
    if (!mount || !dirname || parent_cluster < 2) return -1;
    
    fat32_debug("Deleting directory");
    
    //first find the directory and get its cluster
    extern int fat32_find_in_dir(fat32_mount_t* mount, uint32_t dir_cluster,
                                  const char* name, fat32_dir_entry_t* entry_out);
    
    fat32_dir_entry_t dir_entry;
    if (fat32_find_in_dir(mount, parent_cluster, dirname, &dir_entry) != 0) {
        fat32_debug("Directory not found");
        return -1;
    }
    
    //verify it's actually a directory
    if (!(dir_entry.attr & FAT32_ATTR_DIRECTORY)) {
        fat32_debug("Not a directory");
        return -1;
    }
    
    //get the directory's cluster
    uint32_t dir_cluster = ((uint32_t)dir_entry.first_cluster_hi << 16) | dir_entry.first_cluster_lo;
    
    //check if directory is empty (only . and .. entries allowed)
    char* cluster_buf = (char*)kmalloc(mount->bytes_per_cluster);
    if (!cluster_buf) {
        fat32_debug("ERROR: kmalloc failed");
        return -1;
    }
    
    uint32_t entries_per_cluster = mount->bytes_per_cluster / sizeof(fat32_dir_entry_t);
    uint32_t current_cluster = dir_cluster;
    uint32_t loop_count = 0;
    
    while (current_cluster >= 2 && current_cluster < FAT32_EOC_MIN) {
        loop_count++;
        //prevent infinite loops due to corrupted FAT
        if (loop_count > mount->total_clusters) {
            fat32_debug("ERROR: Loop limit exceeded (possible FAT corruption)");
            kfree(cluster_buf);
            return -1;
        }
        
        if (fat32_read_cluster(mount, current_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;
        
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = &entries[i];
            
            //end of directory
            if (entry->name[0] == 0x00) break;
            
            //skip deleted and LFN entries
            if (entry->name[0] == 0xE5 || entry->attr == FAT32_ATTR_LONG_NAME) continue;
            
            //skip volume labels
            if (entry->attr & FAT32_ATTR_VOLUME_ID) continue;
            
            //check if it's . or ..
            if (entry->name[0] == '.' && entry->name[1] == ' ') continue; //'.'
            if (entry->name[0] == '.' && entry->name[1] == '.' && entry->name[2] == ' ') continue; //'..'
            
            //found a real entry - directory is not empty
            kfree(cluster_buf);
            fat32_debug("Directory not empty");
            return -1;
        }
        
        //move to next cluster in directory
        uint32_t next_cluster = fat32_get_fat_entry(mount, current_cluster);
        if (next_cluster < 2 || next_cluster >= FAT32_EOC_MIN) break;
        current_cluster = next_cluster;
    }
    
    //directory is empty, now delete its entry from parent
    current_cluster = parent_cluster;
    int found_and_deleted = 0;
    
    loop_count = 0;
    while (current_cluster >= 2 && current_cluster < FAT32_EOC_MIN) {
        loop_count++;
        //prevent infinite loops due to corrupted FAT
        if (loop_count > mount->total_clusters) {
            fat32_debug("ERROR: Loop limit exceeded (possible FAT corruption)");
            kfree(cluster_buf);
            return -1;
        }
        
        if (fat32_read_cluster(mount, current_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;
        int modified = 0;
        
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = &entries[i];
            
            if (entry->name[0] == 0x00) break;
            if (entry->name[0] == 0xE5) continue;
            
            //check if this is the directory entry we're looking for
            if (entry->attr != FAT32_ATTR_LONG_NAME) {
                int match = 1;
                for (int j = 0; j < 11; j++) {
                    if (entry->name[j] != dir_entry.name[j]) {
                        match = 0;
                        break;
                    }
                }
                
                if (match) {
                    //mark entry and LFN entries as deleted
                    entry->name[0] = 0xE5;
                    modified = 1;
                    
                    //mark preceding LFN entries as deleted
                    for (int j = (int)i - 1; j >= 0; j--) {
                        if (entries[j].attr == FAT32_ATTR_LONG_NAME) {
                            entries[j].name[0] = 0xE5;
                        } else {
                            break;
                        }
                    }
                    
                    found_and_deleted = 1;
                    break;
                }
            }
        }
        
        if (modified) {
            if (fat32_write_cluster(mount, current_cluster, cluster_buf) != 0) {
                kfree(cluster_buf);
                return -1;
            }
            break;
        }
        
        uint32_t next_cluster = fat32_get_fat_entry(mount, current_cluster);
        if (next_cluster < 2 || next_cluster >= FAT32_EOC_MIN) break;
        current_cluster = next_cluster;
    }
    
    kfree(cluster_buf);
    
    if (!found_and_deleted) {
        fat32_debug("Failed to delete directory entry");
        return -1;
    }
    
    //free the directory cluster chain
    if (dir_cluster >= 2 && dir_cluster < FAT32_EOC_MIN) {
        if (fat32_free_cluster_chain(mount, dir_cluster) != 0) {
            fat32_debug("WARNING: Failed to free cluster chain");
        }
    }
    
    fat32_debug("Directory deleted successfully");
    return 0;
}

//update a directory entry
int fat32_update_dir_entry(fat32_mount_t* mount, uint32_t parent_cluster, const char* filename,
                           fat32_dir_entry_t* updated_entry) {
    if (!mount || !filename || !updated_entry || parent_cluster < 2) return -1;

    fat32_debug("Updating directory entry");

    //allocate cluster buffer
    char* cluster_buf = (char*)kmalloc(mount->bytes_per_cluster);
    if (!cluster_buf) {
        fat32_debug("ERROR: kmalloc failed");
        return -1;
    }

    uint32_t current_cluster = parent_cluster;
    uint32_t entries_per_cluster = mount->bytes_per_cluster / sizeof(fat32_dir_entry_t);

    //search for the file in the parent directory
    uint32_t loop_count = 0;
    while (current_cluster >= 2 && current_cluster < FAT32_EOC_MIN) {
        loop_count++;

        //prevent infinite loops due to corrupted FAT
        if (loop_count > mount->total_clusters) {
            fat32_debug("ERROR: Loop limit exceeded (possible FAT corruption)");
            kfree(cluster_buf);
            return -1;
        }

        //read directory cluster
        if (fat32_read_cluster(mount, current_cluster, cluster_buf) != 0) {
            kfree(cluster_buf);
            return -1;
        }

        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buf;

        //search for the matching entry by comparing the 8.3 name
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_dir_entry_t* entry = &entries[i];

            //end of directory
            if (entry->name[0] == 0x00) {
                kfree(cluster_buf);
                fat32_debug("File not found for update");
                return -1;
            }

            //skip deleted or LFN entries
            if (entry->name[0] == 0xE5 || entry->attr == FAT32_ATTR_LONG_NAME) {
                continue;
            }

            //compare the 8.3 name
            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (entry->name[j] != updated_entry->name[j]) {
                    match = 0;
                    break;
                }
            }

            if (match) {
                //found it update the entry
                //preserve the name and LFN-related fields but update data fields
                entry->file_size = updated_entry->file_size;
                entry->first_cluster_hi = updated_entry->first_cluster_hi;
                entry->first_cluster_lo = updated_entry->first_cluster_lo;
                entry->attr = updated_entry->attr; //in case it changed
                
                //update write timestamp
                uint16_t date, time;
                fat32_get_current_timestamp(&date, &time);
                entry->write_date = date;
                entry->write_time = time;

                //write the modified cluster back
                if (fat32_write_cluster(mount, current_cluster, cluster_buf) != 0) {
                    kfree(cluster_buf);
                    fat32_debug("ERROR: Failed to write updated entry");
                    return -1;
                }

                kfree(cluster_buf);
                fat32_debug("Directory entry updated successfully");
                return 0;
            }
        }

        //move to next cluster
        uint32_t next_cluster = fat32_get_fat_entry(mount, current_cluster);
        if (next_cluster < 2 || next_cluster >= FAT32_EOC_MIN) break;
        current_cluster = next_cluster;
    }

    kfree(cluster_buf);
    fat32_debug("File not found for update");
    return -1;
}
