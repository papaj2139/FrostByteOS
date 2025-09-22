#include <unistd.h>
#include <string.h>

//MBR partitioner writes a single primary partition entry starting at LBA 2048
//usage: partmk <disk_or_path> <total_sectors> [start_lba]

static void puts1(const char* s) { 
    write(1, s, strlen(s)); 
}

static void putu(unsigned v) {
    char tmp[16]; int tp=0; if (v==0) tmp[tp++]='0';
    while (v) { 
        tmp[tp++] = (char)('0' + (v % 10)); 
        v/=10; 
    }
    char out[18]; 
    int p=0; 
    while (tp>0) out[p++]=tmp[--tp]; 
    out[p++]='\n'; 
    write(1,out,p);
}

static unsigned parse_u32(const char* s){ 
    if(!s) return 0; 
    unsigned v=0; 
    while(*s==' ') s++; 
    while(*s>='0'&&*s<='9'){ 
        v = v*10u + (unsigned)(*s - '0'); 
        s++; 
    } 
    return v; 
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    //recompute argc
    int ac=0; 
    if (argv){ 
        while (argv[ac]) ac++; 
    }
    if (ac < 3) {
        puts1("Usage: partmk <disk_or_path> <total_sectors> [start_lba]\n");
        return 1;
    }
    const char* target = argv[1];
    unsigned total_sectors = parse_u32(argv[2]);
    unsigned start_lba = (ac >= 4) ? parse_u32(argv[3]) : 2048u;
    if (total_sectors == 0 || start_lba >= total_sectors) {
        puts1("partmk: invalid total_sectors/start_lba\n");
        return 1;
    }
    //build path
    char devpath[128];
    if (target[0] == '/') { 
        strncpy(devpath, target, sizeof(devpath)-1); 
        devpath[sizeof(devpath)-1]='\0'; 
    }
    else { 
        int p=0; 
        const char* a="/dev/"; 
        while (*a && p<(int)sizeof(devpath)-1) devpath[p++]=*a++; 
        const char* t=target; 
        while (*t && p<(int)sizeof(devpath)-1) devpath[p++]=*t++; 
        devpath[p]='\0'; 
    }

    //create a MBR with one partition
    unsigned char mbr[512];
    for (int i=0;i<512;i++) mbr[i]=0;
    //partition entry 0 at offset 446
    unsigned char* e = &mbr[446];
    e[0] = 0x00;                 //boot indicator (0x80 if bootable)
    e[1] = 0x00; e[2] = 0x02; e[3] = 0x00; //CHS start (ignored)
    e[4] = 0x0E;                 //type: FAT16 LBA
    e[5] = 0xFF; e[6] = 0xFF; e[7] = 0xFF; //CHS end (ignored)
    unsigned part_sectors = total_sectors - start_lba;
    //LBA start
    e[8]  = (unsigned char)(start_lba & 0xFF);
    e[9]  = (unsigned char)((start_lba >> 8) & 0xFF);
    e[10] = (unsigned char)((start_lba >> 16) & 0xFF);
    e[11] = (unsigned char)((start_lba >> 24) & 0xFF);
    //number of sectors
    e[12] = (unsigned char)(part_sectors & 0xFF);
    e[13] = (unsigned char)((part_sectors >> 8) & 0xFF);
    e[14] = (unsigned char)((part_sectors >> 16) & 0xFF);
    e[15] = (unsigned char)((part_sectors >> 24) & 0xFF);
    //signature
    mbr[510] = 0x55; mbr[511] = 0xAA;

    int fd = open(devpath, 2);
    if (fd < 0) { 
        puts1("partmk: open failed\n"); 
        return 1; 
    }
    if (write(fd, mbr, 512) != 512) { 
        puts1("partmk: write failed\n"); 
        close(fd); 
        return 1; 
    }
    close(fd);

    puts1("partmk: wrote MBR on "); 
    puts1(devpath); 
    puts1("\n");
    puts1("  total_sectors: "); 
    putu(total_sectors);
    puts1("  start_lba: "); 
    putu(start_lba);
    puts1("  part_sectors: "); 
    putu(part_sectors);
    puts1("NOTE: reboot to rescan partitions.\n");
    return 0;
}
