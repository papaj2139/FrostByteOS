#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/block.h>

#define SECTOR_SIZE 512

static void usage(const char* prog) {
    printf("Usage: %s [options] <device>\n", prog);
    printf("Format a disk with FAT16 filesystem\n\n");
    printf("Options:\n");
    printf("  -s SIZE       Size in MB (default: auto-detect)\n");
    printf("  -l LABEL      Volume label (default: FROSTBYTE)\n");
    printf("  -c CLUSTER    Cluster size in sectors (default: auto)\n");
    printf("\nExample: mkfat16 -l MYDATA -s 16 /dev/ata0p1\n");
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

//FAT16 Boot Sector structure
typedef struct __attribute__((packed)) {
    uint8_t  jmp_boot[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  file_system_type[8];
} fat16_bpb_t;

static uint32_t ceil_div(uint32_t a, uint32_t b) {
    return (a + b - 1) / b;
}

static int format_fat16(int fd, uint32_t size_mb, const char* label, uint32_t cluster_sectors) {
    uint32_t bytes_per_sector = SECTOR_SIZE;
    uint32_t total_sectors = (size_mb * 1024 * 1024) / bytes_per_sector;
    
    //Auto-select cluster size if not specified
    if (cluster_sectors == 0) {
        if (size_mb <= 16) {
            cluster_sectors = 1;  //512 bytes
        } else if (size_mb <= 128) {
            cluster_sectors = 4;  //2 KB
        } else if (size_mb <= 256) {
            cluster_sectors = 8;  //4 KB
        } else if (size_mb <= 512) {
            cluster_sectors = 16; //8 KB
        } else {
            cluster_sectors = 32; //16 KB
        }
    }
    
    printf("Formatting FAT16:\n");
    printf("  Size: %u MB (%u sectors)\n", size_mb, total_sectors);
    printf("  Cluster size: %u sectors (%u bytes)\n", cluster_sectors, cluster_sectors * bytes_per_sector);
    
    //Calculate FAT16 parameters
    uint16_t reserved_sectors = 1;
    uint8_t num_fats = 2;
    uint16_t root_entries = 512;
    uint32_t root_sectors = ceil_div(root_entries * 32, bytes_per_sector);
    
    //Calculate sectors per FAT
    uint32_t sectors_per_fat = 1;
    uint32_t data_clusters = 0;
    
    for (int it = 0; it < 16; it++) {
        uint32_t data_sectors = 0;
        if (total_sectors > (reserved_sectors + num_fats * sectors_per_fat + root_sectors)) {
            data_sectors = total_sectors - (reserved_sectors + num_fats * sectors_per_fat + root_sectors);
        }
        data_clusters = data_sectors / cluster_sectors;
        uint32_t needed_fat_bytes = (data_clusters + 2) * 2;
        uint32_t spf_new = ceil_div(needed_fat_bytes, bytes_per_sector);
        if (spf_new == sectors_per_fat) {
            break;
        }
        sectors_per_fat = spf_new ? spf_new : 1;
    }
    
    //Validate FAT16 cluster count
    if (data_clusters < 4085 || data_clusters >= 65525) {
        printf("Error: Cluster count %u is outside FAT16 range (4085-65524)\n", data_clusters);
        printf("       Try adjusting the size or cluster size\n");
        return -1;
    }
    
    uint32_t fat_begin = reserved_sectors;
    uint32_t root_begin = reserved_sectors + (num_fats * sectors_per_fat);
    uint32_t data_begin = root_begin + root_sectors;
    
    printf("  FAT begin: sector %u\n", fat_begin);
    printf("  Root begin: sector %u\n", root_begin);
    printf("  Data begin: sector %u\n", data_begin);
    printf("  Total clusters: %u\n", data_clusters);
    
    //Allocate and zero a sector buffer
    char* sector = (char*)malloc(bytes_per_sector);
    if (!sector) {
        printf("Out of memory\n");
        return -1;
    }
    memset(sector, 0, bytes_per_sector);
    
    //Build boot sector
    fat16_bpb_t* bpb = (fat16_bpb_t*)sector;
    
    //Jump instruction
    bpb->jmp_boot[0] = 0xEB;
    bpb->jmp_boot[1] = 0x3C;
    bpb->jmp_boot[2] = 0x90;
    
    //OEM name
    memcpy(bpb->oem_name, "FROSTBYT", 8);
    
    //BPB parameters
    bpb->bytes_per_sector = bytes_per_sector;
    bpb->sectors_per_cluster = cluster_sectors;
    bpb->reserved_sectors = reserved_sectors;
    bpb->num_fats = num_fats;
    bpb->root_entries = root_entries;
    
    if (total_sectors < 65536) {
        bpb->total_sectors_16 = total_sectors;
        bpb->total_sectors_32 = 0;
    } else {
        bpb->total_sectors_16 = 0;
        bpb->total_sectors_32 = total_sectors;
    }
    
    bpb->media_type = 0xF8; //Hard disk
    bpb->sectors_per_fat = sectors_per_fat;
    bpb->sectors_per_track = 63;
    bpb->num_heads = 255;
    bpb->hidden_sectors = 0;
    bpb->drive_number = 0x80;
    bpb->reserved1 = 0;
    bpb->boot_signature = 0x29;
    bpb->volume_id = generate_volume_id();
    
    //Volume label
    memset(bpb->volume_label, ' ', 11);
    if (label) {
        int len = strlen(label);
        if (len > 11) len = 11;
        memcpy(bpb->volume_label, label, len);
        //Pad with spaces
        for (int i = len; i < 11; i++) {
            bpb->volume_label[i] = ' ';
        }
    }
    
    //Filesystem type
    memcpy(bpb->file_system_type, "FAT16   ", 8);
    
    //Boot signature
    sector[510] = 0x55;
    sector[511] = 0xAA;
    
    printf("Writing boot sector...\n");
    if (write_sector(fd, 0, sector, bytes_per_sector) != 0) {
        printf("Failed to write boot sector\n");
        free(sector);
        return -1;
    }
    
    //Initialize FAT tables
    printf("Writing FAT tables...\n");
    for (uint32_t fat_num = 0; fat_num < num_fats; fat_num++) {
        uint32_t fat_sector = fat_begin + (fat_num * sectors_per_fat);
        
        //First FAT sector has special entries
        memset(sector, 0, bytes_per_sector);
        uint16_t* fat = (uint16_t*)sector;
        fat[0] = 0xFFF8; //Media type + EOC
        fat[1] = 0xFFFF; //Reserved
        
        if (write_sector(fd, fat_sector, sector, bytes_per_sector) != 0) {
            printf("Failed to write FAT table %u\n", fat_num);
            free(sector);
            return -1;
        }
        
        //Zero rest of FAT
        memset(sector, 0, bytes_per_sector);
        for (uint32_t i = 1; i < sectors_per_fat; i++) {
            if (write_sector(fd, fat_sector + i, sector, bytes_per_sector) != 0) {
                printf("Failed to write FAT table %u sector %u\n", fat_num, i);
                free(sector);
                return -1;
            }
        }
    }
    
    //Zero root directory
    printf("Initializing root directory...\n");
    memset(sector, 0, bytes_per_sector);
    for (uint32_t i = 0; i < root_sectors; i++) {
        if (write_sector(fd, root_begin + i, sector, bytes_per_sector) != 0) {
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
    
    //Parse arguments
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
    
    //Try to auto-detect size if not specified
    if (size_mb == 0) {
        int fd_probe = open(device, O_RDWR);
        if (fd_probe >= 0) {
            blkdev_info_t info;
            if (ioctl(fd_probe, IOCTL_BLK_GET_INFO, &info) == 0 && info.sector_count > 0) {
                size_mb = (info.sector_count * 512) / (1024 * 1024);
                printf("Auto-detected size: %u MB\n", size_mb);
            }
            close(fd_probe);
        }
    }
    
    if (size_mb == 0) {
        printf("Error: Size must be specified with -s or device must support size detection\n");
        usage(argv[0]);
        return 1;
    }
    
    if (size_mb < 2 || size_mb > 2048) {
        printf("Error: FAT16 size must be between 2 MB and 2048 MB\n");
        return 1;
    }
    
    printf("WARNING: This will DESTROY all data on %s!\n", device);
    printf("Press ENTER to continue, Ctrl+C to cancel...\n");
    char dummy;
    read(0, &dummy, 1);
    
    //Open device
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        printf("Failed to open device: %s\n", device);
        return 1;
    }
    
    int result = format_fat16(fd, size_mb, label, cluster_sectors);
    
    close(fd);
    
    if (result == 0) {
        printf("Successfully formatted %s as FAT16\n", device);
        printf("Volume label: %s\n", label);
    } else {
        printf("Format failed\n");
    }
    
    return result;
}
