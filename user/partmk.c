#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/block.h>

#define MAX_PARTITIONS 4

typedef struct {
    uint8_t bootable;
    uint8_t type;
    uint32_t start_lba;
    uint32_t sectors;
} partition_t;

typedef struct {
    const char* name;
    uint8_t type;
} partition_type_t;

static const partition_type_t partition_types[] = {
    {"Empty", 0x00},
    {"FAT16", 0x06},
    {"FAT16 LBA", 0x0E},
    {"FAT32 LBA", 0x0C},
    {"Linux", 0x83},
    {"Linux Swap", 0x82},
    {"Extended", 0x05},
    {"NTFS", 0x07},
};
static const int partition_types_count = sizeof(partition_types) / sizeof(partition_types[0]);

static const char* get_partition_type_name(uint8_t type) {
    for (int i = 0; i < partition_types_count; i++) {
        if (partition_types[i].type == type) {
            return partition_types[i].name;
        }
    }
    return "Unknown";
}

static void clear_screen(void) {
    printf("\033[2J\033[H");
}

static void print_header(const char* device, uint32_t total_sectors) {
    printf("+========================================================================+\n");
    printf("|              FrostByte Partition Manager                               |\n");
    printf("+========================================================================+\n");
    printf("| Device: %s\n", device);
    printf("| Size: %u MB (%u sectors)\n", 
           (total_sectors * 512) / (1024 * 1024), total_sectors);
    printf("+========================================================================+\n\n");
}

static void print_partition_table(partition_t* parts, uint32_t total_sectors) {
    printf("+------+----------+--------------+--------------+--------------+---------+\n");
    printf("| Part | Bootable |     Type     |  Start LBA   |   Sectors    | Size MB |\n");
    printf("+------+----------+--------------+--------------+--------------+---------+\n");
    
    for (int i = 0; i < MAX_PARTITIONS; i++) {
        if (parts[i].type == 0x00) {
            printf("|  %d   |    No    |    Empty     |      -       |      -       |    -    |\n", i + 1);
        } else {
            const char* type_name = get_partition_type_name(parts[i].type);
            printf("|  %d   |   %s    | ", i + 1, parts[i].bootable ? "Yes" : "No ");
            printf("%s", type_name);
            //pad type name to 12 chars
            int len = strlen(type_name);
            for (int j = len; j < 12; j++) printf(" ");
            printf(" | %10u   | %10u   | %5u   |\n",
                   parts[i].start_lba,
                   parts[i].sectors,
                   (parts[i].sectors * 512) / (1024 * 1024));
        }
    }
    
    printf("+------+----------+--------------+--------------+--------------+---------+\n\n");
    
    //calculate free space
    uint32_t used = 0;
    for (int i = 0; i < MAX_PARTITIONS; i++) {
        if (parts[i].type != 0x00) {
            uint32_t end = parts[i].start_lba + parts[i].sectors;
            if (end > used) used = end;
        }
    }
    uint32_t free = (used < total_sectors) ? (total_sectors - used) : 0;
    printf("Free space: %u sectors (%u MB)\n\n", free, (free * 512) / (1024 * 1024));
}

static void print_menu(void) {
    printf("Commands:\n");
    printf("  [N]ew partition    [D]elete partition   [T]ype change   [B]ootable\n");
    printf("  [W]rite & quit     [Q]uit without save  [H]elp\n\n");
    printf("Choice: ");
}

static void show_help(void) {
    clear_screen();
    printf("+========================================================================+\n");
    printf("|                      Partition Manager Help                          |\n");
    printf("+========================================================================+\n");
    printf("|                                                                      |\n");
    printf("| N - New Partition                                                    |\n");
    printf("|     Create a new partition in free space. You'll be prompted for:    |\n");
    printf("|     - Partition number (1-4)                                         |\n");
    printf("|     - Type (FAT16, FAT32, Linux, etc.)                               |\n");
    printf("|     - Size in MB                                                     |\n");
    printf("|                                                                      |\n");
    printf("| D - Delete Partition                                                 |\n");
    printf("|     Remove an existing partition. This only updates the partition    |\n");
    printf("|     table - data is not erased.                                      |\n");
    printf("|                                                                      |\n");
    printf("| T - Change Type                                                      |\n");
    printf("|     Change the filesystem type of an existing partition.             |\n");
    printf("|                                                                      |\n");
    printf("| B - Toggle Bootable                                                  |\n");
    printf("|     Mark/unmark a partition as bootable.                             |\n");
    printf("|                                                                      |\n");
    printf("| W - Write & Quit                                                     |\n");
    printf("|     Write changes to disk and exit. This updates the MBR.            |\n");
    printf("|     Remember to rescan partitions after: echo 1 > /proc/rescan       |\n");
    printf("|                                                                      |\n");
    printf("| Q - Quit Without Save                                                |\n");
    printf("|     Exit without writing changes to disk.                            |\n");
    printf("|                                                                      |\n");
    printf("| Note: First partition typically starts at LBA 2048 for alignment.    |\n");
    printf("|                                                                      |\n");
    printf("+========================================================================+\n");
    printf("\nPress ENTER to continue...");
    char dummy;
    read(0, &dummy, 1);
}

static int read_mbr(int fd, partition_t* parts) {
    uint8_t mbr[512];
    if (lseek(fd, 0, SEEK_SET) < 0) return -1;
    if (read(fd, mbr, 512) != 512) return -1;
    
    //check signature
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        //no valid MBR initialize empty
        for (int i = 0; i < MAX_PARTITIONS; i++) {
            parts[i].bootable = 0;
            parts[i].type = 0x00;
            parts[i].start_lba = 0;
            parts[i].sectors = 0;
        }
        return 0;
    }
    
    //parse partition table
    for (int i = 0; i < MAX_PARTITIONS; i++) {
        uint8_t* e = &mbr[446 + i * 16];
        parts[i].bootable = (e[0] == 0x80) ? 1 : 0;
        parts[i].type = e[4];
        parts[i].start_lba = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                             ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        parts[i].sectors = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                           ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
    }
    
    return 0;
}

static int write_mbr(int fd, partition_t* parts) {
    uint8_t mbr[512];
    memset(mbr, 0, sizeof(mbr));
    
    //write partition table
    for (int i = 0; i < MAX_PARTITIONS; i++) {
        uint8_t* e = &mbr[446 + i * 16];
        e[0] = parts[i].bootable ? 0x80 : 0x00;
        //CHS start (set to 0xFF for LBA)
        e[1] = 0xFF;
        e[2] = 0xFF;
        e[3] = 0xFF;
        e[4] = parts[i].type;
        //CHS end (set to 0xFF for LBA)
        e[5] = 0xFF;
        e[6] = 0xFF;
        e[7] = 0xFF;
        //LBA start
        e[8] = (uint8_t)(parts[i].start_lba & 0xFF);
        e[9] = (uint8_t)((parts[i].start_lba >> 8) & 0xFF);
        e[10] = (uint8_t)((parts[i].start_lba >> 16) & 0xFF);
        e[11] = (uint8_t)((parts[i].start_lba >> 24) & 0xFF);
        //sector count
        e[12] = (uint8_t)(parts[i].sectors & 0xFF);
        e[13] = (uint8_t)((parts[i].sectors >> 8) & 0xFF);
        e[14] = (uint8_t)((parts[i].sectors >> 16) & 0xFF);
        e[15] = (uint8_t)((parts[i].sectors >> 24) & 0xFF);
    }
    
    //write signature
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    
    if (lseek(fd, 0, SEEK_SET) < 0) return -1;
    if (write(fd, mbr, 512) != 512) return -1;
    
    return 0;
}

static uint32_t find_free_start(partition_t* parts, uint32_t min_lba) {
    uint32_t start = min_lba;
    int changed;
    
    do {
        changed = 0;
        for (int i = 0; i < MAX_PARTITIONS; i++) {
            if (parts[i].type != 0x00) {
                uint32_t p_start = parts[i].start_lba;
                uint32_t p_end = p_start + parts[i].sectors;
                if (start >= p_start && start < p_end) {
                    start = p_end;
                    changed = 1;
                }
            }
        }
    } while (changed);
    
    return start;
}

static void create_partition(partition_t* parts, uint32_t total_sectors) {
    printf("\n--- Create New Partition ---\n");
    
    //find empty slot
    int slot = -1;
    for (int i = 0; i < MAX_PARTITIONS; i++) {
        if (parts[i].type == 0x00) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        printf("Error: All partition slots are in use!\n");
        printf("Press ENTER to continue...");
        char dummy;
        read(0, &dummy, 1);
        return;
    }
    
    printf("Partition slot: %d\n\n", slot + 1);
    
    //show partition types
    printf("Available partition types:\n");
    for (int i = 0; i < partition_types_count; i++) {
        if (partition_types[i].type != 0x00) {
            printf("  %d. %s (0x%02X)\n", i, partition_types[i].name, partition_types[i].type);
        }
    }
    printf("\nType number: ");
    
    char input[64];
    if (!fgets(0, input, sizeof(input))) return;
    int type_idx = atoi(input);
    if (type_idx < 0 || type_idx >= partition_types_count || partition_types[type_idx].type == 0x00) {
        printf("Invalid type!\n");
        printf("Press ENTER to continue...");
        char dummy;
        read(0, &dummy, 1);
        return;
    }
    
    //find free space
    uint32_t start = find_free_start(parts, 2048);
    if (start >= total_sectors) {
        printf("Error: No free space available!\n");
        printf("Press ENTER to continue...");
        char dummy;
        read(0, &dummy, 1);
        return;
    }
    
    uint32_t max_size = total_sectors - start;
    printf("\nSize in MB (max %u MB): ", (max_size * 512) / (1024 * 1024));
    
    if (!fgets(0, input, sizeof(input))) return;
    uint32_t size_mb = atoi(input);
    if (size_mb == 0) {
        printf("Invalid size!\n");
        printf("Press ENTER to continue...");
        char dummy;
        read(0, &dummy, 1);
        return;
    }
    
    uint32_t sectors = (size_mb * 1024 * 1024) / 512;
    if (sectors > max_size) sectors = max_size;
    
    //create partition
    parts[slot].bootable = 0;
    parts[slot].type = partition_types[type_idx].type;
    parts[slot].start_lba = start;
    parts[slot].sectors = sectors;
    
    printf("\nPartition %d created successfully!\n", slot + 1);
    printf("Start: LBA %u, Size: %u sectors (%u MB)\n", start, sectors, (sectors * 512) / (1024 * 1024));
    printf("Press ENTER to continue...");
    char dummy;
    read(0, &dummy, 1);
}

static void delete_partition(partition_t* parts) {
    printf("\n--- Delete Partition ---\n");
    printf("Partition number (1-4): ");
    
    char input[64];
    if (!fgets(0, input, sizeof(input))) return;
    int num = atoi(input);
    if (num < 1 || num > MAX_PARTITIONS) {
        printf("Invalid partition number!\n");
        printf("Press ENTER to continue...");
        char dummy;
        read(0, &dummy, 1);
        return;
    }
    
    int idx = num - 1;
    if (parts[idx].type == 0x00) {
        printf("Partition %d is already empty!\n", num);
        printf("Press ENTER to continue...");
        char dummy;
        read(0, &dummy, 1);
        return;
    }
    
    printf("Delete partition %d (%s, %u MB)? [y/N]: ", 
           num, get_partition_type_name(parts[idx].type),
           (parts[idx].sectors * 512) / (1024 * 1024));
    
    if (!fgets(0, input, sizeof(input))) return;
    if (input[0] == 'y' || input[0] == 'Y') {
        parts[idx].bootable = 0;
        parts[idx].type = 0x00;
        parts[idx].start_lba = 0;
        parts[idx].sectors = 0;
        printf("Partition %d deleted.\n", num);
    } else {
        printf("Cancelled.\n");
    }
    
    printf("Press ENTER to continue...");
    char dummy;
    read(0, &dummy, 1);
}

static void change_type(partition_t* parts) {
    printf("\n--- Change Partition Type ---\n");
    printf("Partition number (1-4): ");
    
    char input[64];
    if (!fgets(0, input, sizeof(input))) return;
    int num = atoi(input);
    if (num < 1 || num > MAX_PARTITIONS) {
        printf("Invalid partition number!\n");
        printf("Press ENTER to continue...");
        char dummy;
        read(0, &dummy, 1);
        return;
    }
    
    int idx = num - 1;
    if (parts[idx].type == 0x00) {
        printf("Partition %d is empty!\n", num);
        printf("Press ENTER to continue...");
        char dummy;
        read(0, &dummy, 1);
        return;
    }
    
    printf("Current type: %s (0x%02X)\n\n", get_partition_type_name(parts[idx].type), parts[idx].type);
    
    printf("Available partition types:\n");
    for (int i = 0; i < partition_types_count; i++) {
        if (partition_types[i].type != 0x00) {
            printf("  %d. %s (0x%02X)\n", i, partition_types[i].name, partition_types[i].type);
        }
    }
    printf("\nNew type number: ");
    
    if (!fgets(0, input, sizeof(input))) return;
    int type_idx = atoi(input);
    if (type_idx < 0 || type_idx >= partition_types_count || partition_types[type_idx].type == 0x00) {
        printf("Invalid type!\n");
        printf("Press ENTER to continue...");
        char dummy;
        read(0, &dummy, 1);
        return;
    }
    
    parts[idx].type = partition_types[type_idx].type;
    printf("Type changed to %s.\n", partition_types[type_idx].name);
    printf("Press ENTER to continue...");
    char dummy;
    read(0, &dummy, 1);
}

static void toggle_bootable(partition_t* parts) {
    printf("\n--- Toggle Bootable Flag ---\n");
    printf("Partition number (1-4): ");
    
    char input[64];
    if (!fgets(0, input, sizeof(input))) return;
    int num = atoi(input);
    if (num < 1 || num > MAX_PARTITIONS) {
        printf("Invalid partition number!\n");
        printf("Press ENTER to continue...");
        char dummy;
        read(0, &dummy, 1);
        return;
    }
    
    int idx = num - 1;
    if (parts[idx].type == 0x00) {
        printf("Partition %d is empty!\n", num);
        printf("Press ENTER to continue...");
        char dummy;
        read(0, &dummy, 1);
        return;
    }
    
    //clear bootable from all other partitions
    for (int i = 0; i < MAX_PARTITIONS; i++) {
        parts[i].bootable = 0;
    }
    
    parts[idx].bootable = 1;
    printf("Partition %d is now marked as bootable.\n", num);
    printf("Press ENTER to continue...");
    char dummy;
    read(0, &dummy, 1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <device>\n", argv[0]);
        printf("Example: partmk /dev/sata0\n");
        printf("         partmk sata0\n");
        return 1;
    }
    
    const char* target = argv[1];
    char devpath[128];
    
    if (target[0] == '/') {
        strncpy(devpath, target, sizeof(devpath) - 1);
        devpath[sizeof(devpath) - 1] = '\0';
    } else {
        snprintf(devpath, sizeof(devpath), "/dev/%s", target);
    }
    
    //open device
    int fd = open(devpath, O_RDWR);
    if (fd < 0) {
        printf("Error: Cannot open device %s\n", devpath);
        return 1;
    }
    
    //get device size
    blkdev_info_t info;
    uint32_t total_sectors = 0;
    if (ioctl(fd, IOCTL_BLK_GET_INFO, &info) == 0) {
        total_sectors = info.sector_count;
    } else {
        printf("Warning: Could not detect device size. Using default 64 MB.\n");
        total_sectors = 131072; //64 MB
    }
    
    //read existing partition table
    partition_t parts[MAX_PARTITIONS];
    if (read_mbr(fd, parts) != 0) {
        printf("Error: Failed to read MBR\n");
        close(fd);
        return 1;
    }
    
    int quit = 0;
    int modified = 0;
    
    while (!quit) {
        clear_screen();
        print_header(devpath, total_sectors);
        print_partition_table(parts, total_sectors);
        print_menu();
        
        char input[64];
        if (!fgets(0, input, sizeof(input))) break;
        
        char cmd = input[0];
        if (cmd >= 'a' && cmd <= 'z') cmd = cmd - 'a' + 'A';
        
        switch (cmd) {
            case 'N':
                create_partition(parts, total_sectors);
                modified = 1;
                break;
            case 'D':
                delete_partition(parts);
                modified = 1;
                break;
            case 'T':
                change_type(parts);
                modified = 1;
                break;
            case 'B':
                toggle_bootable(parts);
                modified = 1;
                break;
            case 'W':
                if (write_mbr(fd, parts) == 0) {
                    printf("\nPartition table written successfully!\n");
                    printf("Remember to rescan: echo 1 > /proc/rescan or reboot\n");
                } else {
                    printf("\nError: Failed to write partition table!\n");
                }
                printf("Press ENTER to exit...");
                char dummy;
                read(0, &dummy, 1);
                quit = 1;
                break;
            case 'Q':
                if (modified) {
                    printf("\nChanges will be lost. Quit anyway? [y/N]: ");
                    if (!fgets(0, input, sizeof(input))) break;
                    if (input[0] == 'y' || input[0] == 'Y') {
                        quit = 1;
                    }
                } else {
                    quit = 1;
                }
                break;
            case 'H':
                show_help();
                break;
            default:
                break;
        }
    }
    
    close(fd);
    return 0;
}
