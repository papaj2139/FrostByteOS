#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

static void puts1(const char* s) { 
    fputs(1, s); 
}
static void puts2(const char* a, const char* b) { 
    dprintf(1, "%s%s", a, b); 
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    (void)argc;
    //recompute argc from argv
    int ac = 0; if (argv) { 
        while (argv[ac]) ac++; 
    }
    if (ac < 2) {
        puts1("Usage:\n  mount <device> <mount_point> <fs>\n  mount -u <mount_point>\n");
        return 1;
    }
    if (strcmp(argv[1], "-u") == 0) {
        if (ac < 3) { 
            puts1("mount -u <mount_point>\n"); 
            return 1; 
        }
        if (umount(argv[2]) == 0) { 
            puts2("unmounted ", argv[2]); 
            puts1("\n"); 
            return 0; 
        }
        puts1("umount failed\n");
        return 1;
    }
    if (ac < 4) { 
        puts1("mount <device> <mount_point> <fs>\n"); 
        return 1; 
    }
    const char* dev = argv[1];
    const char* orig_dev = dev;
    //accept full path like /dev/ata0p1 by stripping the /dev/ prefix
    if (dev[0] == '/' && dev[1] == 'd' && dev[2] == 'e' && dev[3] == 'v' && dev[4] == '/') {
        dev += 5;
    }
    
    //do some basic validation before calling mount
    char devpath[128];
    snprintf(devpath, sizeof(devpath), "/dev/%s", dev);
    int fd = open(devpath, O_RDONLY);
    if (fd < 0) {
        printf("mount failed: device '%s' not found\n", orig_dev);
        return 1;
    }
    
    //read boot sector and check signature
    unsigned char boot[512];
    if (read(fd, boot, 512) != 512) {
        close(fd);
        printf("mount failed: cannot read boot sector from '%s'\n", orig_dev);
        return 1;
    }
    close(fd);
    
    if (boot[510] != 0x55 || boot[511] != 0xAA) {
        printf("mount failed: invalid boot signature on '%s'\n", orig_dev);
        printf("  Expected: 0x55 0xAA at bytes 510-511\n");
        printf("  Found: 0x%02X 0x%02X\n", boot[510], boot[511]);
        printf("  Did you format the partition?\n");
        return 1;
    }
    
    //check bytes per sector
    unsigned short bytes_per_sector = boot[11] | (boot[12] << 8);
    if (bytes_per_sector != 512) {
        printf("mount failed: invalid sector size %u (must be 512)\n", bytes_per_sector);
        return 1;
    }
    
    //check root entries for FAT16
    unsigned short root_entries = boot[17] | (boot[18] << 8);
    if (strcmp(argv[3], "fat16") == 0 && root_entries == 0) {
        printf("mount failed: FAT16 requires root_entries > 0\n");
        printf("  This looks like FAT32, not FAT16\n");
        return 1;
    }
    
    //now try the actual mount
    if (mount(dev, argv[2], argv[3]) == 0) {
        puts1("mounted\n");
        return 0;
    }
    
    //if it still failed print generic error
    printf("mount failed: filesystem rejected by kernel\n");
    printf("  Check kernel logs (serial) for more details\n");
    return 1;
}
