#ifndef LIBC_SYS_BLOCK_H
#define LIBC_SYS_BLOCK_H

#define IOCTL_BLK_GET_INFO 0x424C4B01u /* 'BLK'|1 */

typedef struct {
    unsigned sector_size;  //bytes per sector (typically 512)
    unsigned sector_count; //total number of sectors
} blkdev_info_t;

#endif
