#ifndef INITRAMFS_H
#define INITRAMFS_H

#include <stdint.h>
#include "vfs.h"

//initialize internal structures
void initramfs_init(void);

//add an in-memory file by absolute path like /bin/sh
int initramfs_add_file(const char* path, const uint8_t* data, uint32_t size);

//install initramfs as the root filesystem (no VFS mount required)
//after this VFS path resolution will traverse initramfs via vfs_root->ops
void initramfs_install_as_root(void);

//populate with built-in files embedded in the kernel
void initramfs_populate_builtin(void);

#endif
