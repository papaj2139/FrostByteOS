
#include "fd.h"
#include "fs/vfs.h"
#include <stddef.h>

static vfs_file_t open_files[MAX_OPEN_FILES];

void fd_init(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        open_files[i].node = NULL;
    }
}

int32_t fd_alloc(vfs_node_t* node, uint32_t flags) {
    for (int32_t i = 3; i < MAX_OPEN_FILES; i++) { //start from 3 to reserve 0 1 and 2 for stdin stdout and stderr
        if (open_files[i].node == NULL) {
            open_files[i].node = node;
            open_files[i].offset = 0;
            open_files[i].flags = flags;
            open_files[i].ref_count = 1;
            return i;
        }
    }
    return -1; //no free file descriptors
}

vfs_file_t* fd_get(int32_t fd) {
    if (fd < 3 || fd >= MAX_OPEN_FILES || open_files[fd].node == NULL) {
        return NULL;
    }
    return &open_files[fd];
}

void fd_close(int32_t fd) {
    if (fd >= 3 && fd < MAX_OPEN_FILES && open_files[fd].node != NULL) {
        open_files[fd].ref_count--;
        if (open_files[fd].ref_count == 0) {
            // Delegate to VFS close so refcounts and FS-specific semantics are handled centrally
            vfs_close(open_files[fd].node);
            open_files[fd].node = NULL;
        }
    }
}
