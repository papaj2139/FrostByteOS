
#ifndef FD_H
#define FD_H

#include "fs/vfs.h"
#include <stdint.h>

#define MAX_OPEN_FILES 256

void fd_init(void);
int32_t fd_alloc(vfs_node_t* node, uint32_t flags);
vfs_file_t* fd_get(int32_t fd);
void fd_close(int32_t fd);

#endif
