#include "fat16.h"
#include <string.h>
#include "../io.h"
#include "../drivers/serial.h"
#include "../debug.h"
#include "../mm/heap.h"
#include "../kernel/cga.h"
#include <stdint.h>

#define FAT16_END_OF_CHAIN 0xFFF8
#define FAT16_BAD_CLUSTER  0xFFF7
#define FAT16_FREE_CLUSTER 0x0000

#ifndef FAT16_SECTOR_SIZE
#define FAT16_SECTOR_SIZE 512u
#endif
#ifndef FAT16_READAHEAD_SECTORS
#define FAT16_READAHEAD_SECTORS 32u  //16KB read ahead by default
#endif

//forward declaration for chain free helper used by delete ops
static int fat16_free_chain(fat16_fs_t* fs, uint16_t start);

static void fat16_debug(const char* msg) {
#if LOG_FAT16
    serial_write_string("[FAT16] ");
    serial_write_string(msg);
    serial_write_string("\n");
#else
    (void)msg;
#endif
}

static void fat16_debug_hex(const char* msg, uint32_t value) {
#if LOG_FAT16
    serial_write_string("[FAT16] ");
    serial_write_string(msg);
    serial_write_string(": 0x");
    char hex[9];
    for (int i = 7; i >= 0; i--) {
        hex[7-i] = "0123456789ABCDEF"[(value >> (i*4)) & 0xF];
    }
    hex[8] = '\0';
    serial_write_string(hex);
    serial_write_string("\n");
#else
    (void)msg; (void)value;
#endif
}

static void fat16_hex_dump(const uint8_t* data, size_t size) {
#if LOG_FAT16
    serial_write_string("[FAT16] Boot sector hex dump:\n");
    for (size_t i = 0; i < size; i += 16) {
        char line[80];
        int pos = 0;
        char offset_hex[5] = {'0', '0', '0', '0', ':'};
        for (int j = 3; j >= 0; j--) {
            offset_hex[3-j] = "0123456789ABCDEF"[(i >> (j*4)) & 0xF];
        }
        for (int k = 0; k < 5; k++) {
            line[pos++] = offset_hex[k];
        }
        line[pos++] = ' ';
        for (int j = 0; j < 16; j++) {
            if (i + j < (size_t)size) {
                line[pos++] = "0123456789ABCDEF"[data[i + j] >> 4];
                line[pos++] = "0123456789ABCDEF"[data[i + j] & 0xF];
                line[pos++] = ' ';
            } else {
                line[pos++] = ' ';
                line[pos++] = ' ';
                line[pos++] = ' ';
            }
        }
        line[pos++] = ' ';
        for (int j = 0; j < 16 && i + j < (size_t)size; j++) {
            char c = data[i + j];
            line[pos++] = (c >= 32 && c <= 126) ? c : '.';
        }
        line[pos] = '\0';
        serial_write_string(line);
        serial_write_string("\n");
    }
#else
    (void)data; (void)size;
#endif
}



static void fat16_print_boot_sector_info(fat16_boot_sector_t* bs) {
    fat16_debug("=== Boot Sector Analysis ===");
    fat16_debug_hex("Bytes per sector", bs->bytes_per_sector);
    fat16_debug_hex("Sectors per cluster", bs->sectors_per_cluster);
    fat16_debug_hex("Reserved sectors", bs->reserved_sectors);
    fat16_debug_hex("Number of FATs", bs->num_fats);
    fat16_debug_hex("Root entries", bs->root_entries);
    fat16_debug_hex("Total sectors (16)", bs->total_sectors_16);
    fat16_debug_hex("Total sectors (32)", bs->total_sectors_32);
    fat16_debug_hex("Sectors per FAT", bs->sectors_per_fat);
    fat16_debug_hex("Boot signature", bs->boot_signature_end);

    if (bs->bytes_per_sector != 512) {
        fat16_debug("ERROR: Invalid sector size");
        return;
    }

    if (bs->boot_signature_end != 0xAA55) {
        fat16_debug("ERROR: Invalid boot signature");
        return;
    }

    if (bs->root_entries == 0) {
        fat16_debug("ERROR: Root entries cannot be zero for FAT16");
        return;
    }

    if (bs->sectors_per_fat == 0) {
        fat16_debug("ERROR: Sectors per FAT cannot be zero");
        return;
    }

    //print OEM name and fs type (debug)
    #if LOG_FAT16
        char oem_name[9];
        memcpy(oem_name, bs->oem_name, 8);
        oem_name[8] = '\0';
        serial_write_string("[FAT16] OEM Name: '");
        serial_write_string(oem_name);
        serial_write_string("'\n");

        char fs_type[9];
        memcpy(fs_type, bs->file_system_type, 8);
        fs_type[8] = '\0';
        serial_write_string("[FAT16] FS Type: '");
        serial_write_string(fs_type);
        serial_write_string("'\n");
    #endif

    fat16_debug("=== End Boot Sector Analysis ===");
}

int fat16_init(fat16_fs_t* fs, device_t* device) {
    if (!fs || !device) return -1;

    fs->device = device;

    //read boot sector
    if (fat16_read_boot_sector(fs) != 0) {
        fat16_debug("Failed to read boot sector");
        return -1;
    }

    //FAT16 validation
    if (fs->boot_sector.bytes_per_sector != 512) {
        fat16_debug("Invalid sector size (must be 512)");
        return -1;
    }

    if (fs->boot_sector.root_entries == 0) {
        fat16_debug("Invalid root entries (cannot be 0 for FAT16)");
        return -1;
    }

    if (fs->boot_sector.sectors_per_fat == 0) {
        fat16_debug("Invalid sectors per FAT (cannot be 0)");
        return -1;
    }

    //calculate offsets
    fs->fat_start = fs->boot_sector.reserved_sectors;
    fs->root_dir_start = fs->fat_start + (fs->boot_sector.num_fats * fs->boot_sector.sectors_per_fat);

    //calculate root directory size in sectors
    uint32_t root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;
    fs->data_start = fs->root_dir_start + root_dir_sectors;

    //calculate total clusters
    uint32_t total_sectors = fs->boot_sector.total_sectors_16 ?
                            fs->boot_sector.total_sectors_16 : fs->boot_sector.total_sectors_32;
    fs->total_clusters = (total_sectors - fs->data_start) / fs->boot_sector.sectors_per_cluster;

    //cluster count validation
    if (fs->total_clusters < 4085 || fs->total_clusters >= 65525) {
        fat16_debug("Cluster count indicates this is not FAT16");
        fat16_debug_hex("Total clusters", fs->total_clusters);
        return -1;
    }

    fat16_debug("FAT16 filesystem initialized successfully");
    fat16_debug_hex("FAT start sector", fs->fat_start);
    fat16_debug_hex("Root dir start sector", fs->root_dir_start);
    fat16_debug_hex("Data start sector", fs->data_start);
    fat16_debug_hex("Total clusters", fs->total_clusters);

    return 0;
}

int fat16_read_boot_sector(fat16_fs_t* fs) {
    uint8_t buffer[512];

    if (device_read(fs->device, 0, buffer, 512) != 512) {
        fat16_debug("Failed to read boot sector from device");
        return -1;
    }

    //dump the boot sector first
    fat16_hex_dump(buffer, 64);

    memcpy(&fs->boot_sector, buffer, sizeof(fat16_boot_sector_t));

    //check boot signature
    if (fs->boot_sector.boot_signature_end != 0xAA55) {
        fat16_debug_hex("Invalid boot signature", fs->boot_sector.boot_signature_end);
        return -1;
    }

    fat16_print_boot_sector_info(&fs->boot_sector);
    return 0;
}

void fat16_to_83_name(const char* name, char* fat_name) {
    memset(fat_name, ' ', 11);

    //find dot on filename
    const char* dot = 0;
    for (const char* p = name; *p; p++) {
        if (*p == '.') {
            dot = p;
            break;
        }
    }

    const char* basename = name;
    const char* ext = dot ? dot + 1 : "";

    //copy base name max 8 chars
    int i;
    for (i = 0; i < 8 && basename[i] && basename[i] != '.'; i++) {
        fat_name[i] = toupper(basename[i]);
    }

    //cpy extension max 3 chars
    if (dot) {
        for (int j = 0; j < 3 && ext[j]; j++) {
            fat_name[8 + j] = toupper(ext[j]);
        }
    }
}

int fat16_find_file(fat16_fs_t* fs, const char* filename, fat16_dir_entry_t* entry) {
    char fat_name[11];
    fat16_to_83_name(filename, fat_name);

    fat16_debug("Searching for file:");
    char debug_name[12];
    memcpy(debug_name, fat_name, 11);
    debug_name[11] = '\0';
    serial_write_string("[FAT16] FAT name: '");
    serial_write_string(debug_name);
    serial_write_string("'\n");

    //calculate root directory size in sectors
    uint32_t root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;

    uint8_t buffer[512];
    uint32_t entries_per_sector = 512 / sizeof(fat16_dir_entry_t);

    fat16_debug_hex("Root dir sectors", root_dir_sectors);
    fat16_debug_hex("Entries per sector", entries_per_sector);

    //search through root directory
    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint32_t offset = (fs->root_dir_start + sector) * 512;

        if (device_read(fs->device, offset, buffer, 512) != 512) {
            fat16_debug("Failed to read root directory sector");
            return -1;
        }

        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;

        for (uint32_t i = 0; i < entries_per_sector; i++) {
            //check if entry valid
            if (entries[i].filename[0] == 0x00) {
                fat16_debug("End of directory reached");
                return -1; //end of directory
            }
            if (entries[i].filename[0] == 0xE5) continue; //deleted entry
            if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) continue; //volume label


            char entry_name[12];
            memcpy(entry_name, entries[i].filename, 11);
            entry_name[11] = '\0';
            serial_write_string("[FAT16] Found entry: '");
            serial_write_string(entry_name);
            serial_write_string("'\n");

            //compare names
            if (memcmp(entries[i].filename, fat_name, 11) == 0) {
                fat16_debug("File found!");
                memcpy(entry, &entries[i], sizeof(fat16_dir_entry_t));
                return 0;
            }
        }
    }

    fat16_debug("File not found in directory");
    return -1; //file not found
}

int fat16_list_directory(fat16_fs_t* fs) {
    uint32_t root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;

    uint8_t buffer[512];
    uint32_t entries_per_sector = 512 / sizeof(fat16_dir_entry_t);

    fat16_debug("=== Directory Listing ===");
    fat16_debug_hex("Root dir sectors", root_dir_sectors);

    int file_count = 0;

    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint32_t offset = (fs->root_dir_start + sector) * 512;

        if (device_read(fs->device, offset, buffer, 512) != 512) {
            fat16_debug("Failed to read directory sector");
            print("Error: Failed to read directory\n", 0x0C);
            return -1;
        }

        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;

        for (uint32_t i = 0; i < entries_per_sector; i++) {
            if (entries[i].filename[0] == 0x00) {
                fat16_debug("End of directory");
                goto end_listing; //end of directory
            }
            if (entries[i].filename[0] == 0xE5) continue; //deleted entry

            if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) {
                char vol_name[12];
                memcpy(vol_name, entries[i].filename, 11);
                vol_name[11] = '\0';
                serial_write_string("[FAT16] Volume Label: '");
                serial_write_string(vol_name);
                serial_write_string("'\n");
                continue;
            }

            file_count++;

            //print filename
            char name[13];
            memcpy(name, entries[i].filename, 8);
            name[8] = '\0';

            //trim trailing spaces
            for (int j = 7; j >= 0; j--) {
                if (name[j] == ' ') name[j] = '\0';
                else break;
            }

            //add extension if present
            if (entries[i].extension[0] != ' ') {
                unsigned int len = strlen(name);
                name[len] = '.';
                name[len + 1] = '\0';

                //copy extension
                for (int j = 0; j < 3 && entries[i].extension[j] != ' '; j++) {
                    len = strlen(name);
                    name[len] = entries[i].extension[j];
                    name[len + 1] = '\0';
                }
            }

            char line_buf[80];
            if (entries[i].attributes & FAT16_ATTR_DIRECTORY) {
                ksnprintf(line_buf, sizeof(line_buf), "  %s <DIR>\n", name);
            } else {
                ksnprintf(line_buf, sizeof(line_buf), "  %s (%u bytes)\n", name, entries[i].file_size);
            }
            print(line_buf, 0x0F);

            serial_write_string("[FAT16] [");
            if (entries[i].attributes & FAT16_ATTR_DIRECTORY) {
                serial_write_string("DIR ");
            } else {
                serial_write_string("FILE");
            }
            serial_write_string("] ");
            serial_write_string(name);
            serial_write_string(" (");
            char size_str[16];
            itoa(entries[i].file_size, size_str);
            serial_write_string(size_str);
            serial_write_string(" bytes, cluster ");
            itoa(entries[i].first_cluster, size_str);
            serial_write_string(size_str);
            serial_write_string(")\n");
        }
    }

end_listing:
    char summary[40];
    ksnprintf(summary, sizeof(summary), "Total: %d file(s)\n", file_count);
    print(summary, 0x0F);

    //debug summary
    serial_write_string("[FAT16] Total files found: ");
    char count_str[16];
    itoa(file_count, count_str);
    serial_write_string(count_str);
    serial_write_string("\n");

    return 0;
}

uint16_t fat16_get_next_cluster(fat16_fs_t* fs, uint16_t cluster) {
    if (cluster >= fs->total_clusters + 2) {
        return FAT16_BAD_CLUSTER;
    }

    //calculate FAT sector and offset
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = fs->fat_start + (fat_offset / fs->boot_sector.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs->boot_sector.bytes_per_sector;

    uint8_t buffer[512];
    if (device_read(fs->device, fat_sector * 512, buffer, 512) != 512) {
        return FAT16_BAD_CLUSTER;
    }

    uint16_t* fat_table = (uint16_t*)buffer;
    return fat_table[entry_offset / 2];
}

int fat16_open_file(fat16_fs_t* fs, fat16_file_t* file, const char* filename) {
    if (!fs || !file || !filename) return -1;

    memset(file, 0, sizeof(fat16_file_t));
    file->fs = fs;

    if (fat16_find_file(fs, filename, &file->entry) != 0) {
        fat16_debug("File not found");
        return -1;
    }

    if (file->entry.attributes & FAT16_ATTR_DIRECTORY) {
        fat16_debug("Cannot open directory as file");
        return -1;
    }

    file->current_cluster = file->entry.first_cluster;
    file->current_offset = 0;
    file->file_size = file->entry.file_size;
    file->is_open = 1;
    //initialize sequential read cache
    file->cached_offset = 0;
    file->cached_cluster = file->current_cluster;
    file->cache_valid = 1;
    //init read-ahead
    #if FAT16_USE_READAHEAD
        file->ra_buf = NULL;
        file->ra_buf_cap = 0;
        file->ra_len = 0;
        file->ra_off = 0;
    #endif

    fat16_debug("File opened successfully");
    fat16_debug_hex("Starting cluster", file->current_cluster);
    fat16_debug_hex("File size", file->file_size);

    return 0;
}

//fill the per-file read-ahead buffer starting at current_offset
//attempts to fill at least min_bytes (if possible) up to buffer capacity or EOF
#if FAT16_USE_READAHEAD
static uint32_t fat16_fill_readahead(fat16_file_t* file, uint32_t min_bytes) {
    if (!file) return 0;
    if (!file->ra_buf) {
        uint32_t cap = FAT16_READAHEAD_SECTORS * FAT16_SECTOR_SIZE;
        file->ra_buf = (uint8_t*)kmalloc(cap);
        if (!file->ra_buf) return 0;
        file->ra_buf_cap = cap;
    }
    if (min_bytes > file->ra_buf_cap) min_bytes = file->ra_buf_cap;

    file->ra_off = file->current_offset;
    file->ra_len = 0;

    if (file->ra_off >= file->file_size) return 0;

    uint32_t sectors_per_cluster = file->fs->boot_sector.sectors_per_cluster;
    uint32_t cluster_size = sectors_per_cluster * FAT16_SECTOR_SIZE;

    //determine starting cluster for ra_off using cached chain position
    uint16_t cluster;
    uint32_t target_index = file->ra_off / cluster_size;
    if (file->cache_valid && file->cached_offset == file->ra_off) {
        cluster = file->cached_cluster;
    } else {
        uint32_t start_index = 0;
        uint16_t start_cluster = file->entry.first_cluster;
        if (file->cache_valid) {
            uint32_t cached_index = file->cached_offset / cluster_size;
            if (cached_index <= target_index) {
                start_index = cached_index;
                start_cluster = file->cached_cluster;
            }
        }
        cluster = start_cluster;
        for (uint32_t i = start_index; i < target_index; i++) {
            uint16_t next = fat16_get_next_cluster(file->fs, cluster);
            cluster = next;
            if (cluster >= FAT16_END_OF_CHAIN) return 0;
        }
    }

    while (file->ra_len < file->ra_buf_cap && (file->ra_off + file->ra_len) < file->file_size && cluster >= 2 && cluster < FAT16_END_OF_CHAIN) {
        uint32_t base_sector = file->fs->data_start + (cluster - 2u) * sectors_per_cluster;
        uint32_t cur_off = file->ra_off + file->ra_len;
        uint32_t cluster_offset = cur_off % cluster_size;
        uint32_t sector_in_cluster = cluster_offset / FAT16_SECTOR_SIZE;
        uint32_t byte_in_sector = cluster_offset % FAT16_SECTOR_SIZE;

        uint32_t capacity_left = file->ra_buf_cap - file->ra_len;
        uint32_t file_left = file->file_size - cur_off;

        if (byte_in_sector != 0 || capacity_left < FAT16_SECTOR_SIZE || file_left < FAT16_SECTOR_SIZE) {
            //handle partial sector
            uint8_t sector_buffer[FAT16_SECTOR_SIZE];
            if (device_read(file->fs->device, (base_sector + sector_in_cluster) * FAT16_SECTOR_SIZE, sector_buffer, FAT16_SECTOR_SIZE) != (int)FAT16_SECTOR_SIZE) {
                break;
            }
            uint32_t bytes_in_sector = FAT16_SECTOR_SIZE - byte_in_sector;
            uint32_t to_copy = (bytes_in_sector < capacity_left) ? bytes_in_sector : capacity_left;
            if (to_copy > file_left) to_copy = file_left;
            memcpy(file->ra_buf + file->ra_len, sector_buffer + byte_in_sector, to_copy);
            file->ra_len += to_copy;
            cur_off += to_copy;
            if ((cur_off % cluster_size) == 0) {
                uint16_t next = fat16_get_next_cluster(file->fs, cluster);
                cluster = next;
                if (cluster >= FAT16_END_OF_CHAIN) break;
            }
        } else {
            //read whole sectors directly
            uint32_t sectors_left_in_cluster = sectors_per_cluster - sector_in_cluster;
            uint32_t max_sectors_cap = capacity_left / FAT16_SECTOR_SIZE;
            uint32_t max_sectors_file = file_left / FAT16_SECTOR_SIZE;
            uint32_t contig = sectors_left_in_cluster;
            if (contig > max_sectors_cap) contig = max_sectors_cap;
            if (contig > max_sectors_file) contig = max_sectors_file;
            if (contig == 0) break;
            uint32_t to_bytes = contig * FAT16_SECTOR_SIZE;
            if (device_read(file->fs->device, (base_sector + sector_in_cluster) * FAT16_SECTOR_SIZE, file->ra_buf + file->ra_len, to_bytes) != (int)to_bytes) {
                break;
            }
            file->ra_len += to_bytes;
            cur_off += to_bytes;
            if ((cur_off % cluster_size) == 0) {
                uint16_t next = fat16_get_next_cluster(file->fs, cluster);
                cluster = next;
                if (cluster >= FAT16_END_OF_CHAIN) break;
            }
        }
        if (file->ra_len >= min_bytes) break;
    }
    return file->ra_len;
}
#endif // FAT16_USE_READAHEAD

int fat16_read_file(fat16_file_t* file, void* buffer, uint32_t size) {
    if (!file || !file->is_open || !buffer) return -1;

    if (file->current_offset >= file->file_size) {
        return 0; //end of file
    }

    //dont read past end of file
    if (file->current_offset + size > file->file_size) {
        size = file->file_size - file->current_offset;
    }

    uint8_t* buf_ptr = (uint8_t*)buffer;
    uint32_t bytes_read = 0;

    //calculate  cluster size
    uint32_t sectors_per_cluster = file->fs->boot_sector.sectors_per_cluster;
    uint32_t cluster_size = sectors_per_cluster * FAT16_SECTOR_SIZE;

    #if FAT16_USE_READAHEAD
        //serve from readahead buffer if overlapping
        if (file->ra_len > 0) {
            uint32_t ra_end = file->ra_off + file->ra_len;
            if (file->current_offset >= file->ra_off && file->current_offset < ra_end) {
                uint32_t offset_in_ra = file->current_offset - file->ra_off;
                uint32_t avail = file->ra_len - offset_in_ra;
                uint32_t to_copy = (avail < size) ? avail : size;
                memcpy(buf_ptr, file->ra_buf + offset_in_ra, to_copy);
                bytes_read += to_copy;
                file->current_offset += to_copy;
                size -= to_copy;
                if (size == 0) {
                    //it no longer know the exact cluster for this offset invalidate cache
                    file->cache_valid = 0;
                    return (int)bytes_read;
                }
            }
        }

        //if the remaining request is small prefetch aggressively and serve
        if (size > 0 && size <= FAT16_READAHEAD_THRESHOLD_BYTES) {
            //prefetch up to the full RA capacity to benefit subsequent small sequential reads
            uint32_t want = FAT16_READAHEAD_SECTORS * FAT16_SECTOR_SIZE;
            uint32_t got = fat16_fill_readahead(file, want);
            if (got > 0) {
                uint32_t to_copy = (got < size) ? got : size;
                memcpy(buf_ptr + bytes_read, file->ra_buf, to_copy);
                bytes_read += to_copy;
                file->current_offset += to_copy;
                size -= to_copy;
                if (size == 0) {
                    //read-ahead consumed we don't know exact cluster so invalidate cache
                    file->cache_valid = 0;
                    return (int)bytes_read;
                }
            }
        }
    #endif

    //fallback to direct reads across clusters for any remainder
    if (size > 0) {
        //locate starting cluster using cached info
        uint16_t cluster;
        uint32_t target_index = file->current_offset / cluster_size;
        if (file->cache_valid && file->cached_offset == file->current_offset) {
            cluster = file->cached_cluster;
        } else {
            uint32_t start_index = 0;
            uint16_t start_cluster = file->entry.first_cluster;
            if (file->cache_valid) {
                uint32_t cached_index = file->cached_offset / cluster_size;
                if (cached_index <= target_index) {
                    start_index = cached_index;
                    start_cluster = file->cached_cluster;
                }
            }
            cluster = start_cluster;
            for (uint32_t i = start_index; i < target_index; i++) {
                uint16_t next = fat16_get_next_cluster(file->fs, cluster);
                cluster = next;
                if (cluster >= FAT16_END_OF_CHAIN) {
                    break;
                }
            }
            //after computing update cache only if we remain within a valid cluster
            if (cluster >= 2 && cluster < FAT16_END_OF_CHAIN) {
                file->cached_offset = file->current_offset;
                file->cached_cluster = cluster;
                file->cache_valid = 1;
            } else {
                file->cache_valid = 0;
            }
        }

        while (size > 0 && cluster >= 2 && cluster < FAT16_END_OF_CHAIN) {
            uint32_t base_sector = file->fs->data_start + (cluster - 2u) * sectors_per_cluster;
            uint32_t cluster_offset = file->current_offset % cluster_size;
            uint32_t sector_in_cluster = cluster_offset / FAT16_SECTOR_SIZE;
            uint32_t byte_in_sector = cluster_offset % FAT16_SECTOR_SIZE;

            if (byte_in_sector == 0 && size >= FAT16_SECTOR_SIZE) {
                uint32_t sectors_left_in_cluster = sectors_per_cluster - sector_in_cluster;
                uint32_t max_sectors_from_remaining = size / FAT16_SECTOR_SIZE;
                uint32_t contig = (sectors_left_in_cluster < max_sectors_from_remaining) ? sectors_left_in_cluster : max_sectors_from_remaining;
                if (contig > 0) {
                    uint32_t to_bytes = contig * FAT16_SECTOR_SIZE;
                    if (device_read(file->fs->device, (base_sector + sector_in_cluster) * FAT16_SECTOR_SIZE, buf_ptr + bytes_read, to_bytes) != (int)to_bytes) {
                        break;
                    }
                    bytes_read += to_bytes;
                    file->current_offset += to_bytes;
                    size -= to_bytes;
                    if ((file->current_offset % cluster_size) == 0) {
                        uint16_t next = fat16_get_next_cluster(file->fs, cluster);
                        cluster = next;
                        file->current_cluster = cluster;
                        if (cluster >= FAT16_END_OF_CHAIN) break;
                    }
                    file->cached_offset = file->current_offset;
                    file->cached_cluster = cluster;
                    file->cache_valid = 1;
                    continue;
                }
            }

            uint8_t sector_buffer[FAT16_SECTOR_SIZE];
            if (device_read(file->fs->device, (base_sector + sector_in_cluster) * FAT16_SECTOR_SIZE, sector_buffer, FAT16_SECTOR_SIZE) != (int)FAT16_SECTOR_SIZE) {
                break;
            }
            uint32_t bytes_in_sector = FAT16_SECTOR_SIZE - byte_in_sector;
            uint32_t bytes_to_copy = (bytes_in_sector < size) ? bytes_in_sector : size;
            memcpy(buf_ptr + bytes_read, sector_buffer + byte_in_sector, bytes_to_copy);
            bytes_read += bytes_to_copy;
            file->current_offset += bytes_to_copy;
            size -= bytes_to_copy;

            if ((file->current_offset % cluster_size) == 0) {
                uint16_t next = fat16_get_next_cluster(file->fs, cluster);
                cluster = next;
                file->current_cluster = cluster;
                if (cluster >= FAT16_END_OF_CHAIN) break;
            }
            file->cached_offset = file->current_offset;
            file->cached_cluster = cluster;
            file->cache_valid = 1;
        }
    }

    //if nothing read but not at EOF try to read one sector via a direct recompute
    if (bytes_read == 0 && size > 0 && file->current_offset < file->file_size) {
        uint32_t sectors_per_cluster = file->fs->boot_sector.sectors_per_cluster;
        uint32_t cluster_size = sectors_per_cluster * FAT16_SECTOR_SIZE;
        uint32_t target_index = file->current_offset / cluster_size;
        uint16_t cluster = file->entry.first_cluster;
        for (uint32_t i = 0; i < target_index; i++) {
            uint16_t next = fat16_get_next_cluster(file->fs, cluster);
            cluster = next;
            if (cluster >= FAT16_END_OF_CHAIN) break;
        }
        if (cluster >= 2 && cluster < FAT16_END_OF_CHAIN) {
            uint32_t base_sector = file->fs->data_start + (cluster - 2u) * sectors_per_cluster;
            uint32_t cluster_offset = file->current_offset % cluster_size;
            uint32_t sector_in_cluster = cluster_offset / FAT16_SECTOR_SIZE;
            uint32_t byte_in_sector = cluster_offset % FAT16_SECTOR_SIZE;
            uint8_t sector_buffer[FAT16_SECTOR_SIZE];
            if (device_read(file->fs->device, (base_sector + sector_in_cluster) * FAT16_SECTOR_SIZE, sector_buffer, FAT16_SECTOR_SIZE) == (int)FAT16_SECTOR_SIZE) {
                uint32_t bytes_in_sector = FAT16_SECTOR_SIZE - byte_in_sector;
                uint32_t to_copy = (bytes_in_sector < size) ? bytes_in_sector : size;
                memcpy(buf_ptr + bytes_read, sector_buffer + byte_in_sector, to_copy);
                bytes_read += to_copy;
                file->current_offset += to_copy;
            }
            //invalidate cache since we bypassed normal path
            file->cache_valid = 0;
        }
    }

    fat16_debug_hex("Bytes read from file", bytes_read);
    return (int)bytes_read;
}

static uint16_t fat16_find_free_cluster(fat16_fs_t* fs) {
    uint8_t buffer[512];
    for (uint32_t sector = 0; sector < fs->boot_sector.sectors_per_fat; ++sector) {
        uint32_t offset = (fs->fat_start + sector) * 512;
        if (device_read(fs->device, offset, buffer, 512) != 512) {
            fat16_debug("Failed to read FAT sector in find_free_cluster");
            return 0; //indicates error
        }

        uint16_t* fat_table = (uint16_t*)buffer;
        for (uint32_t i = 0; i < (fs->boot_sector.bytes_per_sector / 2); ++i) {
            if (fat_table[i] == FAT16_FREE_CLUSTER) {
                uint16_t cluster_num = (sector * (fs->boot_sector.bytes_per_sector / 2)) + i;
                //cluster 0 and 1 are reserved
                if (cluster_num >= 2) {
                    return cluster_num;
                }
            }
        }
    }
    return 0; //no free cluster found
}

static int fat16_set_cluster_value(fat16_fs_t* fs, uint16_t cluster, uint16_t value) {
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = fs->fat_start + (fat_offset / fs->boot_sector.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs->boot_sector.bytes_per_sector;

    uint8_t buffer[512];
    if (device_read(fs->device, fat_sector * 512, buffer, 512) != 512) {
        fat16_debug("Failed to read FAT sector for setting cluster value");
        return -1;
    }

    uint16_t* fat_table = (uint16_t*)buffer;
    fat_table[entry_offset / 2] = value;

    if (device_write(fs->device, fat_sector * 512, buffer, 512) != 512) {
        fat16_debug("Failed to write FAT sector for setting cluster value");
        return -1;
    }

    //also write to second FAT if it exists
    if (fs->boot_sector.num_fats > 1) {
        uint32_t second_fat_sector = fat_sector + fs->boot_sector.sectors_per_fat;
        if (device_write(fs->device, second_fat_sector * 512, buffer, 512) != 512) {
            fat16_debug("Failed to write to second FAT");
            //this is not a fatal error but still logging it
        }
    }

    return 0;
}

//update the matching directory entry on disk (root directory only) with
//the provided size and first_cluster from 'entry' returns 0 on success
static int fat16_update_dir_entry_on_disk(fat16_fs_t* fs, const fat16_dir_entry_t* entry) {
    if (!fs || !entry) return -1;

    uint32_t root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;
    uint8_t buffer[512];
    uint32_t entries_per_sector = 512 / sizeof(fat16_dir_entry_t);

    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint32_t offset = (fs->root_dir_start + sector) * 512;
        if (device_read(fs->device, offset, buffer, 512) != 512) {
            fat16_debug("Failed to read root directory sector for update");
            return -1;
        }

        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
        for (uint32_t i = 0; i < entries_per_sector; i++) {
            if (entries[i].filename[0] == 0x00) {
                //end of dir
                return -1;
            }
            if (entries[i].filename[0] == 0xE5) continue;
            if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) continue;

            if (memcmp(entries[i].filename, entry->filename, 8) == 0 &&
                memcmp(entries[i].extension, entry->extension, 3) == 0) {
                //update size and first cluster
                entries[i].file_size = entry->file_size;
                entries[i].first_cluster = entry->first_cluster;
                if (device_write(fs->device, offset, buffer, 512) != 512) {
                    fat16_debug("Failed to write updated directory sector");
                    return -1;
                }
                return 0;
            }
        }
    }
    return -1;
}

//update a file directory entry in a specific directory (by first_cluster)
//or root when dir_first_cluster =0 returns 0 on success
int fat16_update_dir_entry_in_dir(fat16_fs_t* fs, uint16_t dir_first_cluster, const fat16_dir_entry_t* entry)
{
    if (!fs || !entry) return -1;
    uint8_t buffer[512];
    uint32_t per_sec = 512 / sizeof(fat16_dir_entry_t);

    if (dir_first_cluster == 0) {
        //root directory fixed area
        uint32_t root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;
        for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
            uint32_t offset = (fs->root_dir_start + sector) * 512;
            if (device_read(fs->device, offset, buffer, 512) != 512) return -1;
            fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
            for (uint32_t i = 0; i < per_sec; i++) {
                if (entries[i].filename[0] == 0x00) return -1; //not found
                if (entries[i].filename[0] == 0xE5) continue;
                if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) continue;
                if (memcmp(entries[i].filename, entry->filename, 8) == 0 &&
                    memcmp(entries[i].extension, entry->extension, 3) == 0) {
                    entries[i].file_size = entry->file_size;
                    entries[i].first_cluster = entry->first_cluster;
                    return (device_write(fs->device, offset, buffer, 512) == 512) ? 0 : -1;
                }
            }
        }
        return -1;
    } else {
        //subdirectory stored in data area scan cluster chain
        uint16_t cluster = dir_first_cluster;
        while (cluster >= 2 && cluster < FAT16_END_OF_CHAIN) {
            uint32_t base_lba = fs->data_start + (cluster - 2) * fs->boot_sector.sectors_per_cluster;
            for (uint32_t s = 0; s < fs->boot_sector.sectors_per_cluster; s++) {
                if (device_read(fs->device, (base_lba + s) * 512, buffer, 512) != 512) return -1;
                fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
                for (uint32_t i = 0; i < per_sec; i++) {
                    if (entries[i].filename[0] == 0x00) goto next_cluster; //end of dir
                    if (entries[i].filename[0] == 0xE5) continue;
                    if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) continue;
                    if (memcmp(entries[i].filename, entry->filename, 8) == 0 &&
                        memcmp(entries[i].extension, entry->extension, 3) == 0) {
                        entries[i].file_size = entry->file_size;
                        entries[i].first_cluster = entry->first_cluster;
                        return (device_write(fs->device, (base_lba + s) * 512, buffer, 512) == 512) ? 0 : -1;
                    }
                }
            }
        next_cluster:
            {
                uint16_t next = fat16_get_next_cluster(fs, cluster);
                if (next >= FAT16_END_OF_CHAIN) break; else cluster = next;
            }
        }
        return -1;
    }
}

int fat16_delete_file_root(fat16_fs_t* fs, const char* filename) {
    if (!fs || !filename) return -1;
    //convert to 8.3
    char fat_name[11];
    fat16_to_83_name(filename, fat_name);

    uint32_t root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;
    uint8_t buffer[512];
    uint32_t entries_per_sector = 512 / sizeof(fat16_dir_entry_t);
    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint32_t offset = (fs->root_dir_start + sector) * 512;
        if (device_read(fs->device, offset, buffer, 512) != 512) return -1;
        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
        for (uint32_t i = 0; i < entries_per_sector; i++) {
            if (entries[i].filename[0] == 0x00) return -1; //not found
            if (entries[i].filename[0] == 0xE5) continue;
            if (memcmp(entries[i].filename, fat_name, 8) == 0 &&
                memcmp(entries[i].extension, fat_name + 8, 3) == 0) {
                //ensure it's not a directory
                if (entries[i].attributes & FAT16_ATTR_DIRECTORY) return -1;
                uint16_t first_cluster = entries[i].first_cluster;
                //mark deleted
                entries[i].filename[0] = 0xE5;
                if (device_write(fs->device, offset, buffer, 512) != 512) return -1;
                //free cluster chain
                if (first_cluster >= 2) {
                    if (fat16_free_chain(fs, first_cluster) != 0) return -1;
                }
                return 0;
            }
        }
    }
    return -1;
}

int fat16_delete_file_in_dir(fat16_fs_t* fs, uint16_t dir_first_cluster, const char* name)
{
    if (!fs || !name) return -1;
    if (dir_first_cluster == 0) return fat16_delete_file_root(fs, name);
    char fat_name[11];
    fat16_to_83_name(name, fat_name);
    uint8_t buffer[512];
    uint32_t per_sec = 512 / sizeof(fat16_dir_entry_t);
    uint16_t cluster = dir_first_cluster;
    while (cluster >= 2 && cluster < FAT16_END_OF_CHAIN) {
        uint32_t base = fs->data_start + (cluster - 2) * fs->boot_sector.sectors_per_cluster;
        for (uint32_t s = 0; s < fs->boot_sector.sectors_per_cluster; s++) {
            if (device_read(fs->device, (base + s) * 512, buffer, 512) != 512) return -1;
            fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
            for (uint32_t i = 0; i < per_sec; i++) {
                if (entries[i].filename[0] == 0x00) return -1; //end marker = not found
                if (entries[i].filename[0] == 0xE5) continue;
                if (entries[i].attributes & FAT16_ATTR_VOLUME_ID) continue;
                if (memcmp(entries[i].filename, fat_name, 8) == 0 && memcmp(entries[i].extension, fat_name+8, 3) == 0) {
                    //must be file
                    if (entries[i].attributes & FAT16_ATTR_DIRECTORY) return -1;
                    uint16_t first_cluster = entries[i].first_cluster;
                    entries[i].filename[0] = 0xE5; //mark deleted
                    if (device_write(fs->device, (base + s) * 512, buffer, 512) != 512) return -1;
                    if (first_cluster >= 2) fat16_free_chain(fs, first_cluster);
                    return 0;
                }
            }
        }
        uint16_t next = fat16_get_next_cluster(fs, cluster);
        if (next >= FAT16_END_OF_CHAIN) break; else cluster = next;
    }
    return -1;
}

int fat16_write_file(fat16_file_t* file, const void* buffer, uint32_t size) {
    if (!file || !file->is_open || !buffer || size == 0) {
        return -1;
    }

    fat16_debug("Writing to file");

    fat16_fs_t* fs = file->fs;
    uint32_t bytes_per_sector = fs->boot_sector.bytes_per_sector; //expect 512
    uint32_t sectors_per_cluster = fs->boot_sector.sectors_per_cluster;
    uint32_t cluster_size = sectors_per_cluster * bytes_per_sector;

    const uint8_t* src = (const uint8_t*)buffer;
    uint32_t bytes_written = 0;

    //ensure the file has a starting cluster
    if (file->entry.first_cluster < 2) {
        uint16_t new_cluster = fat16_find_free_cluster(fs);
        if (new_cluster == 0) return -1;
        if (fat16_set_cluster_value(fs, new_cluster, 0xFFFF) != 0) return -1; //mark EOC
        file->entry.first_cluster = new_cluster;
        file->current_cluster = new_cluster;
    }

    //find cluster corresponding to current_offset
    uint32_t cluster_index = file->current_offset / cluster_size;
    uint32_t offset_in_cluster = file->current_offset % cluster_size;

    uint16_t cluster = file->entry.first_cluster;
    for (uint32_t i = 0; i < cluster_index; i++) {
        uint16_t next = fat16_get_next_cluster(fs, cluster);
        if (next >= FAT16_END_OF_CHAIN) {
            // allocate a new cluster and chain it
            uint16_t new_cluster = fat16_find_free_cluster(fs);
            if (new_cluster == 0) return -1;
            if (fat16_set_cluster_value(fs, cluster, new_cluster) != 0) return -1;
            if (fat16_set_cluster_value(fs, new_cluster, 0xFFFF) != 0) return -1;
            cluster = new_cluster;
        } else {
            cluster = next;
        }
    }

    uint32_t sector_in_cluster = offset_in_cluster / bytes_per_sector;
    uint32_t byte_in_sector = offset_in_cluster % bytes_per_sector;

    uint8_t sector_buffer[512];

    while (bytes_written < size) {
        uint32_t sector_lba = fs->data_start + (cluster - 2) * sectors_per_cluster + sector_in_cluster;

        //read and modify and write the sector
        if (device_read(fs->device, sector_lba * bytes_per_sector, sector_buffer, bytes_per_sector) != (int)bytes_per_sector) {
            fat16_debug("Device read failed during write");
            return -1;
        }

        uint32_t space_in_sector = bytes_per_sector - byte_in_sector;
        uint32_t remain = size - bytes_written;
        uint32_t to_copy = (remain < space_in_sector) ? remain : space_in_sector;
        memcpy(sector_buffer + byte_in_sector, src + bytes_written, to_copy);

        if (device_write(fs->device, sector_lba * bytes_per_sector, sector_buffer, bytes_per_sector) != (int)bytes_per_sector) {
            fat16_debug("Device write failed during write");
            return -1;
        }

        bytes_written += to_copy;
        file->current_offset += to_copy;
        if (file->current_offset > file->file_size) {
            file->file_size = file->current_offset;
        }

        //advance position
        byte_in_sector += to_copy;
        if (byte_in_sector >= bytes_per_sector) {
            byte_in_sector = 0;
            sector_in_cluster++;
            if (sector_in_cluster >= sectors_per_cluster) {
                //move to next cluster allocate if needed
                uint16_t next = fat16_get_next_cluster(fs, cluster);
                if (next >= FAT16_END_OF_CHAIN) {
                    uint16_t new_cluster = fat16_find_free_cluster(fs);
                    if (new_cluster == 0) return -1;
                    if (fat16_set_cluster_value(fs, cluster, new_cluster) != 0) return -1;
                    if (fat16_set_cluster_value(fs, new_cluster, 0xFFFF) != 0) return -1;
                    cluster = new_cluster;
                } else {
                    cluster = next;
                }
                sector_in_cluster = 0;
            }
        }
    }

    //update file entry (size may have changed)
    file->entry.file_size = file->file_size;
    //persist directory entry update (best-effort)
    if (fat16_update_dir_entry_on_disk(fs, &file->entry) != 0) {
        fat16_debug("Warning: failed to update dir entry on disk after write");
    }

    fat16_debug_hex("Bytes written to file", bytes_written);
    return (int)bytes_written;
}

int fat16_create_file(fat16_fs_t* fs, const char* filename) {
    fat16_debug("Attempting to create file:");
    #if DEBUG_ENABLED
    serial_write_string(filename);
    serial_write_string("\n");
    #endif

    //check if file already exists
    fat16_dir_entry_t existing_entry;

    if (fat16_find_file(fs, filename, &existing_entry) == 0) {
        fat16_debug("File already exists");
        return -1; //EEXIST
    }

    char fat_name[11];
    fat16_to_83_name(filename, fat_name);

    fat16_debug_hex("Boot sector root_entries", fs->boot_sector.root_entries);
    uint32_t root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;
    uint8_t buffer[512];
    uint32_t entries_per_sector = 512 / sizeof(fat16_dir_entry_t);

    int dir_entry_sector = -1;
    int dir_entry_index = -1;

    //find an empty directory entry
    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint32_t offset = (fs->root_dir_start + sector) * 512;
        if (device_read(fs->device, offset, buffer, 512) != 512) {
            fat16_debug("Failed to read root directory sector for creation");
            return -1;
        }

        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
        for (uint32_t i = 0; i < entries_per_sector; i++) {
            if (entries[i].filename[0] == 0x00 || entries[i].filename[0] == 0xE5) {
                dir_entry_sector = fs->root_dir_start + sector;
                dir_entry_index = i;
                goto found_entry;
            }
        }
    }

found_entry:
    if (dir_entry_sector == -1) {
        fat16_debug("Root directory is full");
        return -1; //ENOSPC
    }

    //find a free cluster for the file content
    uint16_t free_cluster = fat16_find_free_cluster(fs);
    if (free_cluster == 0) {
        fat16_debug("No free clusters available");
        return -1; //ENOSPC
    }

    //mark the new cluster as the end of the chain in the FAT
    if (fat16_set_cluster_value(fs, free_cluster, 0xFFFF) != 0) {
        fat16_debug("Failed to update FAT for new file");
        return -1;
    }

    //read the directory sectora again to be safe
    if (device_read(fs->device, dir_entry_sector * 512, buffer, 512) != 512) {
        fat16_debug("Failed to re-read root directory sector");
        return -1;
    }

    //create the new directory entry
    fat16_dir_entry_t* new_entry = &((fat16_dir_entry_t*)buffer)[dir_entry_index];
    memcpy(new_entry->filename, fat_name, 8);
    memcpy(new_entry->extension, fat_name + 8, 3);
    new_entry->attributes = FAT16_ATTR_ARCHIVE; //default attribute
    new_entry->reserved[0] = 0;
    new_entry->time = 0; //TODO: set current time
    new_entry->date = 0; //TODO: set current date
    new_entry->first_cluster = free_cluster;
    new_entry->file_size = 0; //new file is empty

    //write the modified directory sector back to disk
    if (device_write(fs->device, dir_entry_sector * 512, buffer, 512) != 512) {
        fat16_debug("Failed to write updated root directory sector");
        //attempt to revert FAT change
        fat16_set_cluster_value(fs, free_cluster, FAT16_FREE_CLUSTER);
        return -1;
    }

    fat16_debug("File created successfully!");
    fat16_debug_hex("Directory Sector", dir_entry_sector);
    fat16_debug_hex("Directory Index", dir_entry_index);
    fat16_debug_hex("Allocated Cluster", free_cluster);

    return 0;
}


int fat16_close_file(fat16_file_t* file) {
    if (!file || !file->is_open) return -1;

    //free read-ahead buffer if allocated
    #if FAT16_USE_READAHEAD
        if (file->ra_buf) {
            kfree(file->ra_buf);
            file->ra_buf = NULL;
            file->ra_buf_cap = 0;
            file->ra_len = 0;
            file->ra_off = 0;
        }
    #endif

    file->is_open = 0;
    return 0;
}

//free a cluster chain starting at 'start' (inclusive)
static int fat16_free_chain(fat16_fs_t* fs, uint16_t start) {
    uint16_t cluster = start;
    while (cluster >= 2 && cluster < FAT16_END_OF_CHAIN) {
        uint16_t next = fat16_get_next_cluster(fs, cluster);
        if (fat16_set_cluster_value(fs, cluster, FAT16_FREE_CLUSTER) != 0) return -1;
        if (next >= FAT16_END_OF_CHAIN) break;
        cluster = next;
    }
    return 0;
}

//find or create an empty directory entry slot in a directory cluster chain returns 0 and outputs LBA and index
static int fat16_dir_find_slot_in_dir(fat16_fs_t* fs, uint16_t dir_first_cluster, uint32_t* out_lba, uint32_t* out_index)
{
    if (!fs || !out_lba || !out_index) return -1;
    uint8_t buffer[512];
    uint32_t entries_per_sector = 512 / sizeof(fat16_dir_entry_t);
    if (dir_first_cluster == 0) {
        //root directory fixed size scan for deleted (0xE5) or 0x00 empty
        uint32_t root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;
        for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
            uint32_t lba = fs->root_dir_start + sector;
            if (device_read(fs->device, lba * 512, buffer, 512) != 512) return -1;
            fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
            for (uint32_t i = 0; i < entries_per_sector; i++) {
                if (entries[i].filename[0] == 0xE5 || entries[i].filename[0] == 0x00) {
                    *out_lba = lba; *out_index = i; return 0;
                }
            }
        }
        return -1; //no space in root
    } else {
        //subdirectory scan cluster chain if none found extend chain with a new zeroed cluster
        uint16_t cluster = dir_first_cluster;
        for (;;) {
            uint32_t base_lba = fs->data_start + (cluster - 2) * fs->boot_sector.sectors_per_cluster;
            for (uint32_t s = 0; s < fs->boot_sector.sectors_per_cluster; s++) {
                if (device_read(fs->device, (base_lba + s) * 512, buffer, 512) != 512) return -1;
                fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
                for (uint32_t i = 0; i < entries_per_sector; i++) {
                    if (entries[i].filename[0] == 0xE5 || entries[i].filename[0] == 0x00) {
                        *out_lba = base_lba + s; *out_index = i; return 0;
                    }
                }
            }
            //advance cluster if EOC allocate new cluster and zero it chain it
            uint16_t next = fat16_get_next_cluster(fs, cluster);
            if (next >= FAT16_END_OF_CHAIN) {
                uint16_t newc = fat16_find_free_cluster(fs);
                if (newc == 0) return -1;
                if (fat16_set_cluster_value(fs, cluster, newc) != 0) return -1;
                if (fat16_set_cluster_value(fs, newc, 0xFFFF) != 0) return -1;
                //zero all sectors in the new cluster
                uint32_t new_base = fs->data_start + (newc - 2) * fs->boot_sector.sectors_per_cluster;
                memset(buffer, 0, 512);
                for (uint32_t s = 0; s < fs->boot_sector.sectors_per_cluster; s++) {
                    if (device_write(fs->device, (new_base + s) * 512, buffer, 512) != 512) return -1;
                }
                //first slot of new cluster
                *out_lba = new_base; *out_index = 0; return 0;
            } else {
                cluster = next;
            }
        }
    }
}

int fat16_create_file_in_dir(fat16_fs_t* fs, uint16_t dir_first_cluster, const char* name)
{
    if (!fs || !name) return -1;
    //convert to 8.3 and check existence first
    char fat_name[11];
    fat16_to_83_name(name, fat_name);

    //if root reuse existing finder else scan dir
    if (dir_first_cluster == 0) {
        //reuse root-level create if not exists
        fat16_dir_entry_t tmp;
        if (fat16_find_file(fs, name, &tmp) == 0) return -1; //EEXIST
        return fat16_create_file(fs, name);
    }

    //check if exists in this subdir
    //scan the directory entries
    uint8_t buffer[512];
    uint32_t per_sec = 512 / sizeof(fat16_dir_entry_t);
    uint16_t cluster = dir_first_cluster;
    while (cluster >= 2 && cluster < FAT16_END_OF_CHAIN) {
        uint32_t base = fs->data_start + (cluster - 2) * fs->boot_sector.sectors_per_cluster;
        for (uint32_t s = 0; s < fs->boot_sector.sectors_per_cluster; s++) {
            if (device_read(fs->device, (base + s) * 512, buffer, 512) != 512) return -1;
            fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
            for (uint32_t i = 0; i < per_sec; i++) {
                if (entries[i].filename[0] == 0x00) break; //end marker
                if (entries[i].filename[0] == 0xE5) continue;
                if (memcmp(entries[i].filename, fat_name, 8) == 0 && memcmp(entries[i].extension, fat_name+8, 3) == 0) {
                    return -1; //EEXIST
                }
            }
        }
        uint16_t next = fat16_get_next_cluster(fs, cluster);
        if (next >= FAT16_END_OF_CHAIN) break; else cluster = next;
    }

    //allocate a new data cluster for the file
    uint16_t file_cluster = fat16_find_free_cluster(fs);
    if (file_cluster == 0) return -1;
    if (fat16_set_cluster_value(fs, file_cluster, 0xFFFF) != 0) return -1;

    //find slot in directory (may extend dir if needed)
    uint32_t lba = 0, idx = 0;
    if (fat16_dir_find_slot_in_dir(fs, dir_first_cluster, &lba, &idx) != 0) return -1;
    if (device_read(fs->device, lba * 512, buffer, 512) != 512) return -1;
    fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
    fat16_dir_entry_t* e = &entries[idx];
    memset(e, 0, sizeof(*e));
    memcpy(e->filename, fat_name, 8);
    memcpy(e->extension, fat_name + 8, 3);
    e->attributes = FAT16_ATTR_ARCHIVE;
    e->first_cluster = file_cluster;
    e->file_size = 0;
    if (device_write(fs->device, lba * 512, buffer, 512) != 512) return -1;
    return 0;
}

int fat16_create_dir_in_dir(fat16_fs_t* fs, uint16_t parent_first_cluster, const char* name)
{
    if (!fs || !name) return -1;
    //don't allow "." or ".."
    if ((name[0] == '.' && name[1] == '\0') || (name[0] == '.' && name[1] == '.' && name[2] == '\0')) return -1;

    //convert to 8.3
    char fat_name[11];
    fat16_to_83_name(name, fat_name);

    //if parent is root reuse root mkdir
    if (parent_first_cluster == 0) {
        return fat16_create_dir_root(fs, name);
    }

    //check existence in subdir
    uint8_t buffer[512];
    uint32_t per_sec = 512 / sizeof(fat16_dir_entry_t);
    uint16_t cluster = parent_first_cluster;
    while (cluster >= 2 && cluster < FAT16_END_OF_CHAIN) {
        uint32_t base = fs->data_start + (cluster - 2) * fs->boot_sector.sectors_per_cluster;
        for (uint32_t s = 0; s < fs->boot_sector.sectors_per_cluster; s++) {
            if (device_read(fs->device, (base + s) * 512, buffer, 512) != 512) return -1;
            fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
            for (uint32_t i = 0; i < per_sec; i++) {
                if (entries[i].filename[0] == 0x00) break;
                if (entries[i].filename[0] == 0xE5) continue;
                if (memcmp(entries[i].filename, fat_name, 8) == 0 && memcmp(entries[i].extension, fat_name+8, 3) == 0) return -1; //EEXIST
            }
        }
        uint16_t next = fat16_get_next_cluster(fs, cluster);
        if (next >= FAT16_END_OF_CHAIN) break; else cluster = next;
    }

    //allocate new cluster for directory
    uint16_t newc = fat16_find_free_cluster(fs);
    if (newc == 0) return -1;
    if (fat16_set_cluster_value(fs, newc, 0xFFFF) != 0) return -1;

    //initialize directory cluster '.' and '..'
    memset(buffer, 0, sizeof(buffer));
    fat16_dir_entry_t* d0 = (fat16_dir_entry_t*)buffer;
    fat16_dir_entry_t* d1 = d0 + 1;
    memset(d0, 0, sizeof(*d0)); memset(d1, 0, sizeof(*d1));
    memset(d0->filename, ' ', 8); memset(d0->extension, ' ', 3); d0->filename[0] = '.'; d0->attributes = FAT16_ATTR_DIRECTORY; d0->first_cluster = newc; d0->file_size = 0;
    memset(d1->filename, ' ', 8); memset(d1->extension, ' ', 3); d1->filename[0] = '.'; d1->filename[1] = '.'; d1->attributes = FAT16_ATTR_DIRECTORY; d1->first_cluster = parent_first_cluster; d1->file_size = 0;
    uint32_t base_lba = fs->data_start + (newc - 2) * fs->boot_sector.sectors_per_cluster;
    if (device_write(fs->device, base_lba * 512, buffer, 512) != 512) return -1;
    if (fs->boot_sector.sectors_per_cluster > 1) {
        memset(buffer, 0, 512);
        for (uint32_t s = 1; s < fs->boot_sector.sectors_per_cluster; s++) {
            if (device_write(fs->device, (base_lba + s) * 512, buffer, 512) != 512) return -1;
        }
    }

    //find slot in parent directory (may extend)
    uint32_t lba = 0, idx = 0;
    if (fat16_dir_find_slot_in_dir(fs, parent_first_cluster, &lba, &idx) != 0) return -1;
    if (device_read(fs->device, lba * 512, buffer, 512) != 512) return -1;
    fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
    fat16_dir_entry_t* e = &entries[idx];
    memset(e, 0, sizeof(*e));
    memcpy(e->filename, fat_name, 8);
    memcpy(e->extension, fat_name + 8, 3);
    e->attributes = FAT16_ATTR_DIRECTORY;
    e->first_cluster = newc;
    e->file_size = 0;
    return (device_write(fs->device, lba * 512, buffer, 512) == 512) ? 0 : -1;
}

//create a subdirectory in the root directory
int fat16_create_dir_root(fat16_fs_t* fs, const char* name) {
    if (!fs || !name) return -1;

    //disallow names "." and ".."
    if ((name[0] == '.' && name[1] == '\0') ||
        (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
        return -1;
    }

    //check if entry already exists
    fat16_dir_entry_t existing;
    if (fat16_find_file(fs, name, &existing) == 0) {
        return -1; //EEXIST
    }

    //convert to 8.3
    char fat_name[11];
    fat16_to_83_name(name, fat_name);

    //find empty directory entry in root
    uint32_t root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;
    uint8_t buffer[512];
    uint32_t entries_per_sector = 512 / sizeof(fat16_dir_entry_t);
    int dir_entry_sector = -1;
    int dir_entry_index = -1;
    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint32_t offset = (fs->root_dir_start + sector) * 512;
        if (device_read(fs->device, offset, buffer, 512) != 512) return -1;
        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
        for (uint32_t i = 0; i < entries_per_sector; i++) {
            if (entries[i].filename[0] == 0x00 || entries[i].filename[0] == 0xE5) {
                dir_entry_sector = fs->root_dir_start + sector;
                dir_entry_index = (int)i;
                goto found_slot;
            }
        }
    }
found_slot:
    if (dir_entry_sector == -1) return -1; //no space in root

    //allocate one cluster for directory
    uint16_t new_cluster = fat16_find_free_cluster(fs);
    if (new_cluster == 0) return -1;
    if (fat16_set_cluster_value(fs, new_cluster, 0xFFFF) != 0) return -1; //EOC

    //initialize directory cluster: write '.' and '..' entries, zero the rest
    //build a 512-byte sector with the two entries
    memset(buffer, 0, sizeof(buffer));
    fat16_dir_entry_t* d0 = (fat16_dir_entry_t*)buffer;
    fat16_dir_entry_t* d1 = d0 + 1;
    //'.' entry
    memset(d0, 0, sizeof(*d0));
    memset(d0->filename, ' ', 8);
    memset(d0->extension, ' ', 3);
    d0->filename[0] = '.';
    d0->attributes = FAT16_ATTR_DIRECTORY;
    d0->first_cluster = new_cluster;
    d0->file_size = 0;
    //'..' entry
    memset(d1, 0, sizeof(*d1));
    memset(d1->filename, ' ', 8);
    memset(d1->extension, ' ', 3);
    d1->filename[0] = '.';
    d1->filename[1] = '.';
    d1->attributes = FAT16_ATTR_DIRECTORY;
    d1->first_cluster = 0; //parent is root for a subdir of root
    d1->file_size = 0;

    uint32_t sectors_per_cluster = fs->boot_sector.sectors_per_cluster;
    uint32_t base_lba = fs->data_start + (new_cluster - 2) * sectors_per_cluster;
    //write first sector
    if (device_write(fs->device, base_lba * 512, buffer, 512) != 512) return -1;
    //zero remaining sectors (if any)
    if (sectors_per_cluster > 1) {
        memset(buffer, 0, sizeof(buffer));
        for (uint32_t s = 1; s < sectors_per_cluster; s++) {
            if (device_write(fs->device, (base_lba + s) * 512, buffer, 512) != 512) return -1;
        }
    }

    //create directory entry in root
    if (device_read(fs->device, dir_entry_sector * 512, buffer, 512) != 512) return -1;
    fat16_dir_entry_t* new_entry = &((fat16_dir_entry_t*)buffer)[dir_entry_index];
    memcpy(new_entry->filename, fat_name, 8);
    memcpy(new_entry->extension, fat_name + 8, 3);
    new_entry->attributes = FAT16_ATTR_DIRECTORY;
    new_entry->reserved[0] = 0;
    new_entry->time = 0;
    new_entry->date = 0;
    new_entry->first_cluster = new_cluster;
    new_entry->file_size = 0;
    if (device_write(fs->device, dir_entry_sector * 512, buffer, 512) != 512) {
        //rollback FAT allocation
        fat16_set_cluster_value(fs, new_cluster, FAT16_FREE_CLUSTER);
        return -1;
    }

    return 0;
}

static int fat16_dir_is_empty(fat16_fs_t* fs, uint16_t first_cluster) {
    if (first_cluster < 2) return -1;
    uint32_t sectors_per_cluster = fs->boot_sector.sectors_per_cluster;
    uint32_t base_lba = fs->data_start + (first_cluster - 2) * sectors_per_cluster;
    uint8_t buffer[512];
    //only check first cluster for entries other than '.' and '..'
    for (uint32_t s = 0; s < sectors_per_cluster; s++) {
        if (device_read(fs->device, (base_lba + s) * 512, buffer, 512) != 512) return -1;
        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
        uint32_t per_sec = 512 / sizeof(fat16_dir_entry_t);
        for (uint32_t i = 0; i < per_sec; i++) {
            if (entries[i].filename[0] == 0x00) return 1; //no more entries
            if (entries[i].filename[0] == 0xE5) continue; //deleted
            //skip '.' and '..'
            if ((entries[i].filename[0] == '.' && entries[i].filename[1] == ' ') ||
                (entries[i].filename[0] == '.' && entries[i].filename[1] == '.')) {
                continue;
            }
            //any other entry => not empty
            return 0;
        }
    }
    return 1;
}

int fat16_remove_dir_root(fat16_fs_t* fs, const char* name) {
    if (!fs || !name) return -1;
    //find entry in root
    char fat_name[11];
    fat16_to_83_name(name, fat_name);
    uint32_t root_dir_sectors = (fs->boot_sector.root_entries * sizeof(fat16_dir_entry_t) + 511) / 512;
    uint8_t buffer[512];
    uint32_t entries_per_sector = 512 / sizeof(fat16_dir_entry_t);
    for (uint32_t sector = 0; sector < root_dir_sectors; sector++) {
        uint32_t offset = (fs->root_dir_start + sector) * 512;
        if (device_read(fs->device, offset, buffer, 512) != 512) return -1;
        fat16_dir_entry_t* entries = (fat16_dir_entry_t*)buffer;
        for (uint32_t i = 0; i < entries_per_sector; i++) {
            if (entries[i].filename[0] == 0x00) return -1; //not found
            if (entries[i].filename[0] == 0xE5) continue;
            if (memcmp(entries[i].filename, fat_name, 8) == 0 &&
                memcmp(entries[i].extension, fat_name + 8, 3) == 0) {
                //must be directory
                if (!(entries[i].attributes & FAT16_ATTR_DIRECTORY)) return -1;
                uint16_t first_cluster = entries[i].first_cluster;
                //ensure empty
                int empty = fat16_dir_is_empty(fs, first_cluster);
                if (empty != 1) return -1;
                //delete entry
                entries[i].filename[0] = 0xE5; //mark deleted
                if (device_write(fs->device, offset, buffer, 512) != 512) return -1;
                //free cluster chain
                if (first_cluster >= 2) fat16_free_chain(fs, first_cluster);
                return 0;
            }
        }
    }
    return -1;
}
