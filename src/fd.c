#include "fd.h"
#include "fs/vfs.h"
#include "process.h"
#include <stddef.h>
#include <string.h>

static vfs_file_t open_files[MAX_OPEN_FILES];

//allocate an open-file slot (global) return index or -1
static int32_t of_alloc(vfs_node_t* node, uint32_t flags) {
    for (int32_t i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i].node == NULL) {
            open_files[i].node = node;
            open_files[i].offset = 0;
            open_files[i].flags = flags;
            open_files[i].ref_count = 1;
            return i;
        }
    }
    return -1;
}

//get open-file by index
static inline vfs_file_t* of_get(int32_t idx) {
    if (idx < 0 || idx >= MAX_OPEN_FILES) return NULL;
    if (open_files[idx].node == NULL) return NULL;
    return &open_files[idx];
}

//drop a reference to an open-file by index
static void of_drop(int32_t idx) {
    if (idx < 0 || idx >= MAX_OPEN_FILES) return;
    if (open_files[idx].node == NULL) return;
    if (open_files[idx].ref_count > 0) {
        open_files[idx].ref_count--;
        if (open_files[idx].ref_count == 0) {
            vfs_close(open_files[idx].node);
            open_files[idx].node = NULL;
        }
    }
}

//find lowest free fd slot in a process
static int find_free_fd_slot(process_t* p) {
    if (!p) return -1;
    for (int i = 0; i < (int)(sizeof(p->fd_table)/sizeof(p->fd_table[0])); i++) {
        if (p->fd_table[i] < 0) return i;
    }
    return -1;
}

void fd_init(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        open_files[i].node = NULL;
        open_files[i].offset = 0;
        open_files[i].flags = 0;
        open_files[i].ref_count = 0;
    }
}

//allocate a CURRENT-process-local fd and bind it to a freshly created open-file object
int32_t fd_alloc(vfs_node_t* node, uint32_t flags) {
    process_t* cur = process_get_current();
    if (!cur || !node) return -1;
    int of_idx = of_alloc(node, flags);
    if (of_idx < 0) {
        //no global slot close node
        vfs_close(node);
        return -1;
    }
    int fd = find_free_fd_slot(cur);
    if (fd < 0) {
        //no per-process fd available drop open-file
        of_drop(of_idx);
        return -1;
    }
    cur->fd_table[fd] = of_idx;
    return fd;
}

//lookup CURRENT process fd -> open-file
vfs_file_t* fd_get(int32_t fd) {
    process_t* cur = process_get_current();
    if (!cur) return NULL;
    if (fd < 0 || fd >= (int)(sizeof(cur->fd_table)/sizeof(cur->fd_table[0]))) return NULL;
    int of_idx = cur->fd_table[fd];
    return of_get(of_idx);
}

//close CURRENT process fd
void fd_close(int32_t fd) {
    process_t* cur = process_get_current();
    if (!cur) return;
    if (fd < 0 || fd >= (int)(sizeof(cur->fd_table)/sizeof(cur->fd_table[0]))) return;
    int of_idx = cur->fd_table[fd];
    if (of_idx >= 0) {
        cur->fd_table[fd] = -1;
        of_drop(of_idx);
    }
}

void fd_init_process_stdio(process_t* proc) {
    if (!proc) return;
    //ensure table initialized
    for (int i = 0; i < (int)(sizeof(proc->fd_table)/sizeof(proc->fd_table[0])); i++) {
        if (proc->fd_table[i] != -1 && proc->fd_table[i] < MAX_OPEN_FILES) {
            //leave existing entries as-is (caller may have set up)
        } else {
            proc->fd_table[i] = -1;
        }
    }
    //try to bind 0/1/2 to /dev/tty0 if available
    vfs_node_t* tty = vfs_open("/dev/tty0", VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (!tty) {
        //not available yet leave as unbound (syscalls will fallback for stdio)
        return;
    }
    int of_idx = of_alloc(tty, VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (of_idx < 0) {
        vfs_close(tty);
        return;
    }
    //share the same open-file for 0/1/2 bump refcount accordingly
    open_files[of_idx].ref_count = 3;
    if (0 < (int)(sizeof(proc->fd_table)/sizeof(proc->fd_table[0]))) proc->fd_table[0] = of_idx;
    if (1 < (int)(sizeof(proc->fd_table)/sizeof(proc->fd_table[0]))) proc->fd_table[1] = of_idx;
    if (2 < (int)(sizeof(proc->fd_table)/sizeof(proc->fd_table[0]))) proc->fd_table[2] = of_idx;
}

void fd_copy_on_fork(process_t* parent, process_t* child) {
    if (!parent || !child) return;
    int n = (int)(sizeof(parent->fd_table)/sizeof(parent->fd_table[0]));
    int m = (int)(sizeof(child->fd_table)/sizeof(child->fd_table[0]));
    int lim = (n < m) ? n : m;
    for (int i = 0; i < lim; i++) {
        int of_idx = parent->fd_table[i];
        child->fd_table[i] = of_idx;
        if (of_idx >= 0 && of_idx < MAX_OPEN_FILES && open_files[of_idx].node) {
            open_files[of_idx].ref_count++;
        }
    }
}

void fd_close_all_for(process_t* proc) {
    if (!proc) return;
    int n = (int)(sizeof(proc->fd_table)/sizeof(proc->fd_table[0]));
    for (int i = 0; i < n; i++) {
        int of_idx = proc->fd_table[i];
        if (of_idx >= 0) {
            proc->fd_table[i] = -1;
            of_drop(of_idx);
        }
    }
}
