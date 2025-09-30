#include "fd.h"
#include "fs/vfs.h"
#include "process.h"
#include "mm/heap.h"
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
//reserve 0,1,2 for stdio regardless of whether they are bound yet
static int find_free_fd_slot(process_t* p) {
    if (!p) return -1;
    int n = (int)(sizeof(p->fd_table)/sizeof(p->fd_table[0]));
    for (int i = 3; i < n; i++) {
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

int32_t fd_dup(int32_t oldfd) {
    process_t* cur = process_get_current();
    if (!cur) return -1;
    int n = (int)(sizeof(cur->fd_table)/sizeof(cur->fd_table[0]));
    if (oldfd < 0 || oldfd >= n) return -1;
    int of_idx = cur->fd_table[oldfd];
    if (of_idx < 0) return -1; //oldfd not open
    //find a new fd slot
    int newfd = find_free_fd_slot(cur);
    if (newfd < 0) return -1;
    //point newfd to the same open-file and increment ref count
    cur->fd_table[newfd] = of_idx;
    if (of_idx >= 0 && of_idx < MAX_OPEN_FILES && open_files[of_idx].node) {
        open_files[of_idx].ref_count++;
    }
    return newfd;
}

int32_t fd_dup2(int32_t oldfd, int32_t newfd) {
    process_t* cur = process_get_current();
    if (!cur) return -1;
    int n = (int)(sizeof(cur->fd_table)/sizeof(cur->fd_table[0]));
    if (oldfd < 0 || oldfd >= n) return -1;
    if (newfd < 0 || newfd >= n) return -1;
    int of_idx = cur->fd_table[oldfd];
    if (of_idx < 0) return -1; //oldfd not open
    //if oldfd == newfd return newfd without doing anything (POSIX behavior)
    if (oldfd == newfd) return newfd;
    //close newfd if it's currently open
    if (cur->fd_table[newfd] >= 0) {
        fd_close(newfd);
    }
    //point newfd to the same open-file and increment ref count
    cur->fd_table[newfd] = of_idx;
    if (of_idx >= 0 && of_idx < MAX_OPEN_FILES && open_files[of_idx].node) {
        open_files[of_idx].ref_count++;
    }
    return newfd;
}

//pipe implementation - simple ring buffer
#define PIPE_BUF_SIZE 4096

typedef struct {
    char buffer[PIPE_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count; //number of bytes in pipe
    int write_end_open; //1 if write end is still open
    int read_end_open;  //1 if read end is still open
} pipe_t;

//allocate a pipe buffer
static pipe_t* pipe_alloc(void) {
    pipe_t* p = (pipe_t*)kmalloc(sizeof(pipe_t));
    if (!p) return NULL;
    memset(p->buffer, 0, PIPE_BUF_SIZE);
    p->read_pos = 0;
    p->write_pos = 0;
    p->count = 0;
    p->write_end_open = 1;
    p->read_end_open = 1;
    return p;
}

//pipe read operation
static int pipe_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer) {
    (void)offset; //pipes don't use offset
    if (!node || !node->private_data || !buffer) return -1;
    pipe_t* pipe = (pipe_t*)node->private_data;
    if (size == 0) return 0;
    //if pipe is empty and write end is closed return EOF
    if (pipe->count == 0 && !pipe->write_end_open) return 0;
    //if pipe is empty but write end is open block (for now return 0 blocking would require scheduler support)
    if (pipe->count == 0) return 0;
    //read available bytes
    uint32_t to_read = size < pipe->count ? size : pipe->count;
    for (uint32_t i = 0; i < to_read; i++) {
        buffer[i] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUF_SIZE;
        pipe->count--;
    }
    return (int)to_read;
}

//pipe write operation
static int pipe_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer) {
    (void)offset;
    if (!node || !node->private_data || !buffer) return -1;
    pipe_t* pipe = (pipe_t*)node->private_data;
    if (size == 0) return 0;
    //if read end is closed return error (EPIPE)
    if (!pipe->read_end_open) return -1;
    //write as much as we can fit
    uint32_t space = PIPE_BUF_SIZE - pipe->count;
    uint32_t to_write = size < space ? size : space;
    for (uint32_t i = 0; i < to_write; i++) {
        pipe->buffer[pipe->write_pos] = buffer[i];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUF_SIZE;
        pipe->count++;
    }
    return (int)to_write;
}

//pipe close operation
static int pipe_close(vfs_node_t* node) {
    if (!node || !node->private_data) return -1;
    pipe_t* pipe = (pipe_t*)node->private_data;
    //determine if this is read or write end based on flags
    if (node->flags & VFS_FLAG_READ) {
        pipe->read_end_open = 0;
    }
    if (node->flags & VFS_FLAG_WRITE) {
        pipe->write_end_open = 0;
    }
    //if both ends are closed, free the pipe
    if (!pipe->read_end_open && !pipe->write_end_open) {
        kfree(pipe);
        node->private_data = NULL;
    }
    return 0;
}

static vfs_operations_t pipe_ops = {
    .open = NULL,
    .close = pipe_close,
    .read = pipe_read,
    .write = pipe_write,
    .create = NULL,
    .unlink = NULL,
    .mkdir = NULL,
    .rmdir = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .get_size = NULL,
    .ioctl = NULL,
    .readlink = NULL,
    .symlink = NULL,
    .link = NULL,
};

int32_t fd_pipe(int32_t pipefd[2]) {
    process_t* cur = process_get_current();
    if (!cur || !pipefd) return -1;
    //allocate pipe buffer
    pipe_t* pipe = pipe_alloc();
    if (!pipe) return -1;
    //create two VFS nodes for read and write ends
    vfs_node_t* read_node = vfs_create_node("pipe_r", VFS_FILE_TYPE_DEVICE, VFS_FLAG_READ);
    vfs_node_t* write_node = vfs_create_node("pipe_w", VFS_FILE_TYPE_DEVICE, VFS_FLAG_WRITE);
    if (!read_node || !write_node) {
        if (read_node) vfs_destroy_node(read_node);
        if (write_node) vfs_destroy_node(write_node);
        kfree(pipe);
        return -1;
    }
    //set up operations and share the same pipe buffer
    read_node->ops = &pipe_ops;
    read_node->private_data = pipe;
    write_node->ops = &pipe_ops;
    write_node->private_data = pipe;
    //allocate file descriptors
    int read_fd = fd_alloc(read_node, VFS_FLAG_READ);
    if (read_fd < 0) {
        vfs_destroy_node(read_node);
        vfs_destroy_node(write_node);
        kfree(pipe);
        return -1;
    }
    int write_fd = fd_alloc(write_node, VFS_FLAG_WRITE);
    if (write_fd < 0) {
        fd_close(read_fd);
        vfs_destroy_node(write_node);
        kfree(pipe);
        return -1;
    }
    pipefd[0] = read_fd;
    pipefd[1] = write_fd;
    return 0;
}
