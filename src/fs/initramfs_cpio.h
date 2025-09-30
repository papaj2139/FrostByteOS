#ifndef INITRAMFS_CPIO_H
#define INITRAMFS_CPIO_H

#include <stdint.h>

//load a newc cpio archive from [start,end) into the existing initramfs tree
//returns 0 on success -1 on error
int initramfs_load_cpio(const uint8_t* start, const uint8_t* end);

#endif
