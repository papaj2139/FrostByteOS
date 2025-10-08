#ifndef TMPFS_H
#define TMPFS_H

#include "vfs.h"

//initialize tmpfs
int tmpfs_init(void);

//get tmpfs root node for mounting
vfs_node_t* tmpfs_get_root(void);

#endif