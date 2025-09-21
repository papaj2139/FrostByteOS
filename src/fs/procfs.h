#ifndef PROCFS_H
#define PROCFS_H

#include "vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern vfs_operations_t procfs_ops;

//set the kernel command line string exposed at /proc/cmdline
void procfs_set_cmdline(const char* cmdline);

#ifdef __cplusplus
}
#endif

#endif
