
#ifndef FD_H
#define FD_H

#include "fs/vfs.h"
#include <stdint.h>

#define MAX_OPEN_FILES 256

// Initialize global open-file table
void fd_init(void);

// Allocate a descriptor for the CURRENT process for the given node/flags.
// Returns the process-local fd number on success, or -1 on failure.
int32_t fd_alloc(vfs_node_t* node, uint32_t flags);

// Lookup a process-local fd for the CURRENT process and return the open-file object.
// Returns NULL if fd is invalid or closed for the current process.
vfs_file_t* fd_get(int32_t fd);

// Close a process-local fd for the CURRENT process.
void fd_close(int32_t fd);

// Set up stdin/stdout/stderr for a process (tries to bind to /dev/tty0 if available).
// If /dev/tty0 is not yet mounted, leaves 0/1/2 unbound (syscalls will still fallback to TTY).
void fd_init_process_stdio(struct process* proc);

// Duplicate parent's descriptors into child, increasing refcounts appropriately.
void fd_copy_on_fork(struct process* parent, struct process* child);

// Close all descriptors owned by the given process (used on process destruction).
void fd_close_all_for(struct process* proc);

#endif
