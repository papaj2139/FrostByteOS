#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/block.h>

//mkfat16 format a device with FAT16 filesystem
//usage: mkfat16 <device_or_path> [total_sectors]
//notes:
//- if total_sectors is omitted defaults to 65536 (32 MB)
//- the utility expects to run on a partition device
//- it initializes boot sector two FATs and an empty root directory
//- geometry fields are informational boot code is not provided

#define SECTOR_SIZE 512

static void puts1(const char* s) {
    fputs(1, s);
}

static void puthex32(unsigned v) {
    dprintf(1, "0x%08X\n", v);
}

static int isdigit_c(char c){
    return c>='0' && c<='9';
}

static int ishex_c(char c){
    return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
}

static unsigned hexval(char c){
    if(c>='0'&&c<='9')return (unsigned)(c-'0');
    if(c>='a'&&c<='f')return 10u+(unsigned)(c-'a');
    if(c>='A'&&c<='F')return 10u+(unsigned)(c-'A');
    return 0;
}

static unsigned parse_u32(const char* s){
    if (!s||!*s) return 0;
    //skip spaces
    while (*s==' ') s++;
    unsigned base=10;
    if (s[0]=='0' && (s[1]=='x'||s[1]=='X')){
        base=16;
        s+=2;
    }
    unsigned v=0;
    if (base==10){
        while (isdigit_c(*s)){
            v = v*10u + (unsigned)(*s - '0');
            s++;
        }
    }
    else {
        while (ishex_c(*s)){
            v = (v<<4) | hexval(*s);
            s++;
        }
    }
    return v;
}

//FAT16 boot sector
typedef struct __attribute__((packed)) {
    unsigned char  jmp_boot[3];
    unsigned char  oem_name[8];
    unsigned short bytes_per_sector;
    unsigned char  sectors_per_cluster;
    unsigned short reserved_sectors;
    unsigned char  num_fats;
    unsigned short root_entries;
    unsigned short total_sectors_16;
    unsigned char  media_type;
    unsigned short sectors_per_fat;
    unsigned short sectors_per_track;
    unsigned short num_heads;
    unsigned int   hidden_sectors;
    unsigned int   total_sectors_32;
    unsigned char  drive_number;
    unsigned char  reserved1;
    unsigned char  boot_signature;     //0x29
    unsigned int   volume_id;
    unsigned char  volume_label[11];
    unsigned char  file_system_type[8]; //"FAT16   "
    unsigned char  boot_code[448];
    unsigned short boot_signature_end; //0xAA55
} bs16_t;

static unsigned ceil_div(unsigned a, unsigned b){
    return (a + b - 1) / b;
}

//compute sectors-per-FAT for given total sectors and sectors-per-cluster
static void compute_fat_layout(unsigned total_sectors,
                               unsigned spc,
                               unsigned* out_spf,
                               unsigned* out_root_secs,
                               unsigned* out_data_clusters){
    const unsigned reserved = 1;
    const unsigned num_fats = 2;
    const unsigned root_entries = 512; //512 entries so 32 sectors
    unsigned root_secs = ceil_div(root_entries * 32u, SECTOR_SIZE);
    unsigned spf = 1;
    for (int it=0; it<16; ++it){
        unsigned data_secs = 0;
        if (total_sectors > (reserved + num_fats*spf + root_secs))
            data_secs = total_sectors - (reserved + num_fats*spf + root_secs);
        unsigned clusters = (spc ? (data_secs / spc) : 0);
        unsigned needed_fat_bytes = (clusters + 2) * 2; //+2 reserved
        unsigned spf_new = ceil_div(needed_fat_bytes, SECTOR_SIZE);
        if (spf_new == spf){
            if (out_spf) *out_spf = spf;
            if (out_root_secs) *out_root_secs = root_secs;
            if (out_data_clusters) *out_data_clusters = clusters;
            return;
        }
        spf = spf_new ? spf_new : 1;
    }
    if (out_spf) *out_spf = spf;
    if (out_root_secs) *out_root_secs = root_secs;
    if (out_data_clusters) *out_data_clusters = 0;
}

int main(int argc, char** argv, char** envp){
    (void)envp;
    (void)argc;
    //argc recompute
    int ac=0; if (argv){
        while (argv[ac]) ac++;
    }
    if (ac < 2){
        puts1("Usage: mkfat16 <device_or_path> [total_sectors]\n");
        return 1;
    }
    const char* target = argv[1];
    char devpath[128];
    if (target[0] == '/') {
        //already a path
        strncpy(devpath, target, sizeof(devpath)-1);
        devpath[sizeof(devpath)-1] = '\0';
    } else {
        //assume /dev/<name>
        int p=0; const char* s="/dev/";
        while (*s && p < (int)sizeof(devpath)-1) devpath[p++]=*s++;
        const char* t=target;
        while (*t && p < (int)sizeof(devpath)-1) devpath[p++]=*t++;
        devpath[p]='\0';
    }
    unsigned total_sectors = (ac >= 3) ? parse_u32(argv[2]) : 65536u; //default 32 MB
    if (total_sectors < 2048u){
        puts1("mkfat16: total_sectors too small (min 2048)\n");
        return 1;
    }

    //if user didn't provide size try to query device info via ioctl
    int fd_probe = open(devpath, 2);
    if (fd_probe >= 0) {
        blkdev_info_t info;
        if (ioctl(fd_probe, IOCTL_BLK_GET_INFO, &info) == 0 && info.sector_count > 0) {
            total_sectors = info.sector_count;
        }
        close(fd_probe);
    }

    //choose sectors-per-cluster to keep cluster count in FAT16 limits
    //try from 1 up to 64 sectors/cluster
    unsigned spc_candidates[] = {1,2,4,8,16,32,64};
    unsigned chosen_spc=0, spf=0, root_secs=0, clusters=0;
    for (unsigned i=0;i<sizeof(spc_candidates)/sizeof(spc_candidates[0]);++i){
        unsigned tmp_spf=0, tmp_root=0, tmp_clusters=0;
        compute_fat_layout(total_sectors, spc_candidates[i], &tmp_spf, &tmp_root, &tmp_clusters);
        if (tmp_clusters >= 4085 && tmp_clusters < 65525){
            chosen_spc = spc_candidates[i]; spf = tmp_spf; root_secs = tmp_root; clusters = tmp_clusters; break;
        }
    }
    if (chosen_spc == 0){
        puts1("mkfat16: unable to find suitable sectors/cluster for FAT16\n");
        return 1;
    }

    //open device for read/write
    int fd = open(devpath, 2); //O_RDWR
    if (fd < 0){
        puts1("mkfat16: open failed\n");
        return 1;
    }

    //build boot sector
    unsigned char sector[SECTOR_SIZE];
    memset(sector, 0, sizeof(sector));
    bs16_t* bs = (bs16_t*)sector;
    bs->jmp_boot[0] = 0xEB; bs->jmp_boot[1] = 0x3C; bs->jmp_boot[2] = 0x90;
    memcpy(bs->oem_name, "FROSTBYT", 8);
    bs->bytes_per_sector = (unsigned short)SECTOR_SIZE;
    bs->sectors_per_cluster = (unsigned char)chosen_spc;
    bs->reserved_sectors = 1;
    bs->num_fats = 2;
    bs->root_entries = 512;
    if (total_sectors < 65536u){
        bs->total_sectors_16 = (unsigned short)total_sectors;
        bs->total_sectors_32 = 0;
    }
    else {
        bs->total_sectors_16 = 0;
        bs->total_sectors_32 = total_sectors;
    }
    bs->media_type = 0xF8; //fixed disk
    bs->sectors_per_fat = (unsigned short)spf;
    bs->sectors_per_track = 63;
    bs->num_heads = 255;
    bs->hidden_sectors = 0;
    bs->drive_number = 0x80;
    bs->reserved1 = 0;
    bs->boot_signature = 0x29;
    bs->volume_id = 0x12345678u;
    memcpy(bs->volume_label, "FROSTBYTE  ", 11); //pad to 11
    memcpy(bs->file_system_type, "FAT16   ", 8);
    bs->boot_signature_end = 0xAA55;

    if (write(fd, sector, SECTOR_SIZE) != SECTOR_SIZE){
        puts1("mkfat16: write boot sector failed\n");
        close(fd);
        return 1;
    }

    //write FAT #0 and FAT #1
    unsigned fat_bytes = spf * SECTOR_SIZE;
    (void)fat_bytes;
    //first sector of each FAT set reserved entries
    unsigned char fat_sec[SECTOR_SIZE]; memset(fat_sec, 0, sizeof(fat_sec));
    //FAT[0] = 0xFFF8 FAT[1] = 0xFFFF (little-endian)
    fat_sec[0] = 0xF8;
    fat_sec[1] = 0xFF;
    fat_sec[2] = 0xFF;
    fat_sec[3] = 0xFF;

    //FAT #0
    if (write(fd, fat_sec, SECTOR_SIZE) != SECTOR_SIZE){
        puts1("mkfat16: write FAT0[0] failed\n");
        close(fd);
        return 1;
    }
    memset(fat_sec, 0, sizeof(fat_sec));
    for (unsigned i=1; i<spf; ++i){
        if (write(fd, fat_sec, SECTOR_SIZE) != SECTOR_SIZE){
            puts1("mkfat16: write FAT0 body failed\n");
            close(fd);
            return 1;
        }
    }
    //FAT #1
    memset(fat_sec, 0, sizeof(fat_sec));
    //first sector again with reserved entries
    fat_sec[0] = 0xF8; fat_sec[1] = 0xFF; fat_sec[2] = 0xFF; fat_sec[3] = 0xFF;
    if (write(fd, fat_sec, SECTOR_SIZE) != SECTOR_SIZE){
        puts1("mkfat16: write FAT1[0] failed\n");
        close(fd);
        return 1;
    }
    memset(fat_sec, 0, sizeof(fat_sec));
    for (unsigned i=1; i<spf; ++i){
        if (write(fd, fat_sec, SECTOR_SIZE) != SECTOR_SIZE){
            puts1("mkfat16: write FAT1 body failed\n");
            close(fd);
            return 1;
        }
    }

    //root directory area
    for (unsigned i=0; i<root_secs; ++i){
        if (write(fd, fat_sec, SECTOR_SIZE) != SECTOR_SIZE){
            puts1("mkfat16: write root dir failed\n");
            close(fd);
            return 1;
        }
    }

    puts1("mkfat16: formatted FAT16 on "); puts1(devpath); puts1("\n");
    puts1("  total_sectors: "); {
        char buf[12];
        unsigned v = total_sectors;
        int pos=0;
        char tmp[12];
        int tp=0;
        if (v==0) tmp[tp++]='0';
        while(v){
            tmp[tp++]=(char)('0'+(v%10)); v/=10;
        }
        while (tp>0 && pos<11) buf[pos++]=tmp[--tp];
        buf[pos]='\n';
        write(1, buf, pos+1);
    }
    puts1("  sectors_per_cluster: "); puthex32(chosen_spc);
    puts1("  sectors_per_fat: "); puthex32(spf);
    puts1("  root_dir_sectors: "); puthex32(root_secs);
    puts1("  data_clusters: "); puthex32(clusters);

    close(fd);
    return 0;
}
