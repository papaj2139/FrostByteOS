#include "initramfs.h"
#include "vfs.h"
#include "../mm/heap.h"
#include "../drivers/serial.h"
#include <string.h>

typedef struct initramfs_node {
    char name[64];
    uint32_t type; //VFS_FILE_TYPE_FILE or VFS_FILE_TYPE_DIRECTORY
    uint32_t size;
    uint8_t* data; //only for files
    struct initramfs_node* parent;
    struct initramfs_node* children; //singly-linked list of first child
    struct initramfs_node* next;     //next sibling
} initramfs_node_t;

static initramfs_node_t* g_ramfs_root = NULL;

static void irfs_debug(const char* m) {
    serial_write_string("[initramfs] ");
    serial_write_string(m);
    serial_write_string("\n");
}

//FS operations forward declarations
static int irfs_open(vfs_node_t* node, uint32_t flags);
static int irfs_close(vfs_node_t* node);
static int irfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer);
static int irfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer);
static int irfs_create(vfs_node_t* parent, const char* name, uint32_t flags);
static int irfs_unlink(vfs_node_t* node);
static int irfs_mkdir(vfs_node_t* parent, const char* name, uint32_t flags);
static int irfs_rmdir(vfs_node_t* node);
static int irfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t** out);
static int irfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out);
static int irfs_get_size(vfs_node_t* node);
static int irfs_ioctl(vfs_node_t* node, uint32_t request, void* arg);

static vfs_operations_t g_irfs_ops = {
    .open = irfs_open,
    .close = irfs_close,
    .read = irfs_read,
    .write = irfs_write,
    .create = irfs_create,
    .unlink = irfs_unlink,
    .mkdir = irfs_mkdir,
    .rmdir = irfs_rmdir,
    .readdir = irfs_readdir,
    .finddir = irfs_finddir,
    .get_size = irfs_get_size,
    .ioctl = irfs_ioctl
};

static initramfs_node_t* irfs_create_node(const char* name, uint32_t type) {
    initramfs_node_t* n = (initramfs_node_t*)kmalloc(sizeof(initramfs_node_t));
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    if (name) {
        strncpy(n->name, name, sizeof(n->name) - 1);
    }
    n->type = type;
    n->size = 0;
    n->data = NULL;
    n->parent = NULL;
    n->children = NULL;
    n->next = NULL;
    return n;
}

static initramfs_node_t* irfs_find_child(initramfs_node_t* dir, const char* name) {
    if (!dir || dir->type != VFS_FILE_TYPE_DIRECTORY) return NULL;
    for (initramfs_node_t* c = dir->children; c; c = c->next) {
        if (strcmp(c->name, name) == 0) return c;
    }
    return NULL;
}

static initramfs_node_t* irfs_ensure_dir_path(const char* path) {
    //path starts with '/' ensure all directory components are created
    if (!path || path[0] != '/') return NULL;
    initramfs_node_t* cur = g_ramfs_root;
    const char* p = path + 1;
    while (*p) {
        //find next component
        const char* end = p;
        while (*end && *end != '/') end++;
        size_t len = end - p;
        if (len == 0) { if (*end == '\0') break; p = end + 1; continue; }
        char name[64];
        if (len >= sizeof(name)) len = sizeof(name) - 1;
        memcpy(name, p, len);
        name[len] = '\0';

        //ensure child directory exists (including the last component)
        initramfs_node_t* child = irfs_find_child(cur, name);
        if (!child) {
            child = irfs_create_node(name, VFS_FILE_TYPE_DIRECTORY);
            if (!child) return NULL;
            child->parent = cur;
            //insert at head
            child->next = cur->children;
            cur->children = child;
        }
        if (child->type != VFS_FILE_TYPE_DIRECTORY) return NULL;
        cur = child;
        if (*end == '\0') break;
        p = end + 1;
    }
    return cur;
}

static int irfs_add_file_at(initramfs_node_t* parent, const char* filename, const uint8_t* data, uint32_t size) {
    if (!parent || parent->type != VFS_FILE_TYPE_DIRECTORY) return -1;
    initramfs_node_t* n = irfs_find_child(parent, filename);
    if (!n) {
        n = irfs_create_node(filename, VFS_FILE_TYPE_FILE);
        if (!n) return -1;
        n->parent = parent;
        n->next = parent->children;
        parent->children = n;
    }
    //copy data
    if (n->data) kfree(n->data);
    n->data = NULL;
    n->size = 0;
    if (size) {
        n->data = (uint8_t*)kmalloc(size);
        if (!n->data) return -1;
        memcpy(n->data, data, size);
        n->size = size;
    }
    return 0;
}

void initramfs_init(void) {
    g_ramfs_root = irfs_create_node("/", VFS_FILE_TYPE_DIRECTORY);
    if (!g_ramfs_root) {
        irfs_debug("Failed to allocate root");
        return;
    }
    irfs_debug("Initialized");
}

int initramfs_add_file(const char* path, const uint8_t* data, uint32_t size) {
    if (!g_ramfs_root || !path || path[0] != '/') return -1;
    //split into parent dir path and filename
    const char* last_slash = strrchr(path, '/');
    if (!last_slash) return -1;
    char dirpath[256];
    size_t dirlen = (size_t)(last_slash - path);
    if (dirlen == 0) dirlen = 1; // root
    if (dirlen >= sizeof(dirpath)) dirlen = sizeof(dirpath) - 1;
    memcpy(dirpath, path, dirlen);
    dirpath[dirlen] = '\0';

    const char* fname = last_slash + 1;
    if (!*fname) return -1;

    initramfs_node_t* dir = irfs_ensure_dir_path(dirpath);
    if (!dir) return -1;
    return irfs_add_file_at(dir, fname, data, size);
}

//userspace shell binary gets generated at build time from userapp.asm
//via Makefile rule that produces this header with arrays usershell_bin / usershell_bin_len
#include "usershell_blob.h"
//init program binary generated from init.asm
#include "init_blob.h"
//forktest binary
#include "forktest_blob.h"

void initramfs_install_as_root(void) {
    //define ops lazily below
    extern vfs_operations_t* initramfs_get_ops(void*);
    vfs_operations_t* p = initramfs_get_ops(NULL);
    if (!p) return;
    vfs_set_root_ops(p, g_ramfs_root);
    irfs_debug("Installed as root");
}

vfs_operations_t* initramfs_get_ops(void* unused) { (void)unused; return &g_irfs_ops; }

static initramfs_node_t* irfs_node_from_vnode(vfs_node_t* vnode) {
    return (initramfs_node_t*)vnode->private_data;
}

static vfs_node_t* irfs_make_vnode(initramfs_node_t* n) {
    if (!n) return NULL;
    vfs_node_t* vn = vfs_create_node(n->name, n->type, VFS_FLAG_READ);
    if (!vn) return NULL;
    vn->ops = &g_irfs_ops;
    vn->private_data = n;
    vn->size = n->size;
    vn->parent = NULL;
    return vn;
}

static int irfs_open(vfs_node_t* node, uint32_t flags) {
    (void)node; (void)flags; return 0;
}
static int irfs_close(vfs_node_t* node) { 
    if (node) node->private_data = NULL; 
    return 0; 
}
static int irfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer) {
    (void)node; (void)offset; (void)size; (void)buffer; return -1; // read-only
}
static int irfs_create(vfs_node_t* parent, const char* name, uint32_t flags) {
    (void)parent; (void)name; (void)flags; return -1; // not supported
}
static int irfs_unlink(vfs_node_t* node) { (void)node; return -1; }
static int irfs_mkdir(vfs_node_t* parent, const char* name, uint32_t flags) {
    (void)parent; (void)name; (void)flags; return -1; }
static int irfs_rmdir(vfs_node_t* node) { (void)node; return -1; }

static int irfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer) {
    initramfs_node_t* n = irfs_node_from_vnode(node);
    if (!n || n->type != VFS_FILE_TYPE_FILE) return -1;
    if (offset >= n->size) return 0;
    uint32_t tocopy = n->size - offset;
    if (tocopy > size) tocopy = size;
    memcpy(buffer, n->data + offset, tocopy);
    return (int)tocopy;
}

static int irfs_get_size(vfs_node_t* node) {
    initramfs_node_t* n = irfs_node_from_vnode(node);
    if (!n) return -1;
    return (int)n->size;
}

static int irfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out) {
    if (!node || !name || !out) return -1;
    initramfs_node_t* n = irfs_node_from_vnode(node);
    if (!n || n->type != VFS_FILE_TYPE_DIRECTORY) return -1;
    initramfs_node_t* c = irfs_find_child(n, name);
    if (!c) return -1;
    vfs_node_t* vn = irfs_make_vnode(c);
    if (!vn) return -1;
    *out = vn;
    return 0;
}

static int irfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t** out) {
    if (!node || !out) return -1;
    initramfs_node_t* n = irfs_node_from_vnode(node);
    if (!n || n->type != VFS_FILE_TYPE_DIRECTORY) return -1;
    uint32_t i = 0;
    for (initramfs_node_t* c = n->children; c; c = c->next) {
        if (i == index) {
            vfs_node_t* vn = irfs_make_vnode(c);
            if (!vn) return -1;
            *out = vn;
            return 0;
        }
        i++;
    }
    return -1;
}

static int irfs_ioctl(vfs_node_t* node, uint32_t request, void* arg) {
    (void)node; (void)request; (void)arg; return -1; }

void initramfs_populate_builtin(void) {
    if (!g_ramfs_root) return;
    const char* motd = "Welcome to FrostByte (initramfs)\n";
    initramfs_add_file("/etc/motd", (const uint8_t*)motd, (uint32_t)strlen(motd));
    //add the userspace shell at /bin/sh
    initramfs_add_file("/bin/sh", (const uint8_t*)usershell_bin, (uint32_t)usershell_bin_len);
    //add init that execs /bin/sh
    initramfs_add_file("/bin/init", (const uint8_t*)init_bin, (uint32_t)init_bin_len);
    //add forktest program
    initramfs_add_file("/bin/forktest", (const uint8_t*)forktest_bin, (uint32_t)forktest_bin_len);
}
