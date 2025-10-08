#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/block.h>

//FAT32 Boot Sector structure
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} fat32_bpb_t;

//FAT32 FSInfo structure
typedef struct __attribute__((packed)) {
    uint32_t lead_signature;
    uint8_t  reserved1[480];
    uint32_t struct_signature;
    uint32_t free_count;
    uint32_t next_free;
    uint8_t  reserved2[12];
    uint32_t trail_signature;
} fat32_fsinfo_t;

static void usage(const char* prog) {
    printf("Usage: %s [options] <device>\n", prog);
    printf("Format a disk with FAT32 filesystem\n\n");
    printf("Options:\n");
    printf("  -s SIZE       Size in MB (default: auto-detect)\n");
    printf("  -l LABEL      Volume label (default: FROSTBYTE)\n");
    printf("  -c CLUSTER    Cluster size in sectors (default: auto)\n");
    printf("\nExample: mkfat32 -l MYUSB -s 128 /dev/ata0p1\n");
}

static uint32_t generate_volume_id(void) {
    //Simple volume ID based on time
    //In a real system, you'd use time() + some randomness
    return 0x12345678;
}

static int write_sector(int fd, uint32_t sector, const void* data, uint32_t sector_size) {
    uint32_t offset = sector * sector_size;
    if (lseek(fd, offset, SEEK_SET) < 0) {
        return -1;
    }
    return write(fd, data, sector_size) == (int)sector_size ? 0 : -1;
}

static int format_fat32(int fd, uint32_t size_mb, const char* label, uint32_t cluster_sectors) {
    uint32_t bytes_per_sector = 512;
    uint32_t total_sectors = (size_mb * 1024 * 1024) / bytes_per_sector;
    
    //auto-select cluster size if not specified
    if (cluster_sectors == 0) {
        if (size_mb < 260) {
            cluster_sectors = 1;  //512 bytes
        } else if (size_mb < 8192) {
            cluster_sectors = 8;  //4 KB
        } else if (size_mb < 16384) {
            cluster_sectors = 16; //8 KB
        } else if (size_mb < 32768) {
            cluster_sectors = 32; //16 KB
        } else {
            cluster_sectors = 64; //32 KB
        }
    }
    
    printf("Formatting FAT32:\n");
    printf("  Size: %u MB (%u sectors)\n", size_mb, total_sectors);
    printf("  Cluster size: %u sectors (%u bytes)\n", cluster_sectors, cluster_sectors * bytes_per_sector);
    
    //calculate FAT32 parameters
    uint16_t reserved_sectors = 32;
    uint8_t num_fats = 2;
    
    //estimate FAT size
    uint32_t tmp_data_sectors = total_sectors - reserved_sectors;
    uint32_t tmp_clusters = tmp_data_sectors / cluster_sectors;
    uint32_t fat_size_sectors = (tmp_clusters * 4 + bytes_per_sector - 1) / bytes_per_sector;
    fat_size_sectors += 8; //add some padding
    
    uint32_t fat_begin = reserved_sectors;
    uint32_t data_begin = reserved_sectors + (num_fats * fat_size_sectors);
    uint32_t data_sectors = total_sectors - data_begin;
    uint32_t total_clusters = data_sectors / cluster_sectors;
    
    printf("  FAT begin: sector %u\n", fat_begin);
    printf("  Data begin: sector %u\n", data_begin);
    printf("  Total clusters: %u\n", total_clusters);
    
    //allocate and zero a sector buffer
    char* sector = (char*)malloc(bytes_per_sector);
    if (!sector) {
        printf("Out of memory\n");
        return -1;
    }
    memset(sector, 0, bytes_per_sector);
    
    //build boot sector
    fat32_bpb_t* bpb = (fat32_bpb_t*)sector;
    
    //jump instruction
    bpb->jmp[0] = 0xEB;
    bpb->jmp[1] = 0x58;
    bpb->jmp[2] = 0x90;
    
    //OEM name
    memcpy(bpb->oem_name, "FROSTBYT", 8);
    
    //BPB parameters
    bpb->bytes_per_sector = bytes_per_sector;
    bpb->sectors_per_cluster = cluster_sectors;
    bpb->reserved_sectors = reserved_sectors;
    bpb->num_fats = num_fats;
    bpb->root_entry_count = 0; //FAT32 has no root dir entries
    bpb->total_sectors_16 = 0; //use 32-bit field
    bpb->media_type = 0xF8; //hard disk
    bpb->fat_size_16 = 0; //use 32-bit field
    bpb->sectors_per_track = 63;
    bpb->num_heads = 255;
    bpb->hidden_sectors = 0;
    bpb->total_sectors_32 = total_sectors;
    
    //FAT32-specific fields
    bpb->fat_size_32 = fat_size_sectors;
    bpb->ext_flags = 0;
    bpb->fs_version = 0;
    bpb->root_cluster = 2; //root dir starts at cluster 2
    bpb->fs_info = 1;
    bpb->backup_boot_sector = 6;
    bpb->drive_number = 0x80;
    bpb->reserved1 = 0;
    bpb->boot_signature = 0x29;
    bpb->volume_id = generate_volume_id();
    
    //volume label
    memset(bpb->volume_label, ' ', 11);
    if (label) {
        int len = strlen(label);
        if (len > 11) len = 11;
        memcpy(bpb->volume_label, label, len);
        //pad with spaces
        for (int i = len; i < 11; i++) {
            bpb->volume_label[i] = ' ';
        }
    }
    
    //filesystem type
    memcpy(bpb->fs_type, "FAT32   ", 8);
    
    //boot signature
    sector[510] = 0x55;
    sector[511] = 0xAA;
    
    printf("Writing boot sector...\n");
    if (write_sector(fd, 0, sector, bytes_per_sector) != 0) {
        printf("Failed to write boot sector\n");
        free(sector);
        return -1;
    }
    
    //write backup boot sector
    if (write_sector(fd, 6, sector, bytes_per_sector) != 0) {
        printf("Failed to write backup boot sector\n");
        free(sector);
        return -1;
    }
    
    //build FSInfo sector
    memset(sector, 0, bytes_per_sector);
    fat32_fsinfo_t* fsinfo = (fat32_fsinfo_t*)sector;
    fsinfo->lead_signature = 0x41615252;
    fsinfo->struct_signature = 0x61417272;
    fsinfo->free_count = total_clusters - 1; //minus root dir cluster
    fsinfo->next_free = 3; //start allocating from cluster 3
    fsinfo->trail_signature = 0xAA550000;
    
    printf("Writing FSInfo sector...\n");
    if (write_sector(fd, 1, sector, bytes_per_sector) != 0) {
        printf("Failed to write FSInfo\n");
        free(sector);
        return -1;
    }
    
    //initialize FAT tables
    printf("Writing FAT tables...\n");
    for (uint32_t fat_num = 0; fat_num < num_fats; fat_num++) {
        uint32_t fat_sector = fat_begin + (fat_num * fat_size_sectors);
        
        //first FAT sector has special entries
        memset(sector, 0, bytes_per_sector);
        uint32_t* fat = (uint32_t*)sector;
        fat[0] = 0x0FFFFFF8; //media type + EOC
        fat[1] = 0x0FFFFFFF; //reserved
        fat[2] = 0x0FFFFFFF; //root directory (EOC)
        
        if (write_sector(fd, fat_sector, sector, bytes_per_sector) != 0) {
            printf("Failed to write FAT table %u\n", fat_num);
            free(sector);
            return -1;
        }
        
        //zero rest of FAT
        memset(sector, 0, bytes_per_sector);
        for (uint32_t i = 1; i < fat_size_sectors; i++) {
            if (write_sector(fd, fat_sector + i, sector, bytes_per_sector) != 0) {
                printf("Failed to write FAT table %u sector %u\n", fat_num, i);
                free(sector);
                return -1;
            }
        }
    }
    
    //zero root directory cluster
    printf("Initializing root directory...\n");
    uint32_t root_sector = data_begin + ((bpb->root_cluster - 2) * cluster_sectors);
    memset(sector, 0, bytes_per_sector);
    for (uint32_t i = 0; i < cluster_sectors; i++) {
        if (write_sector(fd, root_sector + i, sector, bytes_per_sector) != 0) {
            printf("Failed to write root directory\n");
            free(sector);
            return -1;
        }
    }
    
    free(sector);
    printf("Format complete!\n");
    return 0;
}

int main(int argc, char** argv) {
    const char* device = NULL;
    uint32_t size_mb = 0;
    const char* label = "FROSTBYTE";
    uint32_t cluster_sectors = 0;
    
    //parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            size_mb = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            label = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cluster_sectors = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            device = argv[i];
        } else {
            printf("Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }
    
    if (!device) {
        printf("Error: No device specified\n");
        usage(argv[0]);
        return 1;
    }
    
    //open device
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        printf("Failed to open device: %s\n", device);
        return 1;
    }
    
    //try to auto-detect size if not specified
    if (size_mb == 0) {
        blkdev_info_t info;
        if (ioctl(fd, IOCTL_BLK_GET_INFO, &info) == 0 && info.sector_count > 0) {
            size_mb = (info.sector_count * 512) / (1024 * 1024);
            printf("Auto-detected size: %u MB\n", size_mb);
        } else {
            printf("Error: Could not detect device size. Please specify with -s\n");
            close(fd);
            usage(argv[0]);
            return 1;
        }
    }
    
    if (size_mb < 33) {
        printf("Error: FAT32 requires at least 33 MB\n");
        close(fd);
        return 1;
    }
    
    printf("WARNING: This will DESTROY all data on %s!\n", device);
    printf("Press ENTER to continue, Ctrl+C to cancel...\n");
    char dummy;
    read(0, &dummy, 1);
    
    int result = format_fat32(fd, size_mb, label, cluster_sectors);
    
    close(fd);
    
    if (result == 0) {
        printf("Successfully formatted %s as FAT32\n", device);
        printf("Volume label: %s\n", label);
    } else {
        printf("Format failed\n");
    }
    
    return result;
}
