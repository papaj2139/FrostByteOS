
#ifndef FD_H
#define FD_H

#include "fs/vfs.h"
#include <stdint.h>

#define MAX_OPEN_FILES 256

//initialize global open-file table
void fd_init(void);

//allocate a descriptor for the CURRENT process for the given node/flags
//returns the process-local fd number on success or -1 on failure
int32_t fd_alloc(vfs_node_t* node, uint32_t flags);

//lookup a process-local fd for the CURRENT process and return the open-file object
//returns NULL if fd is invalid or closed for the current process
vfs_file_t* fd_get(int32_t fd);

//close a process-local fd for the CURRENT process
void fd_close(int32_t fd);

//set up stdin/stdout/stderr for a process (tries to bind to /dev/tty0 if available)
//if /dev/tty0 is not yet mounted leaves 0/1/2 unbound (syscalls will still fallback to TTY.
void fd_init_process_stdio(struct process* proc);

//duplicate parent descriptors into child
void fd_copy_on_fork(struct process* parent, struct process* child);

//close all descriptors owned by the given process
void fd_close_all_for(struct process* proc);

#endif
