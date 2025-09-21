#include "devfs.h"
#include "vfs.h"
#include "../device_manager.h"
#include "../mm/heap.h"
#include "../libc/string.h"
#include "../kernel/klog.h"
#include "../drivers/timer.h"
#include <stddef.h>

typedef enum {
    DEVFS_NODE_ROOT = 0,
    DEVFS_NODE_NULL,
    DEVFS_NODE_ZERO,
    DEVFS_NODE_KMSG,
    DEVFS_NODE_RANDOM,
    DEVFS_NODE_DEVICE, //generic device backed by device_manager
} devfs_node_kind_t;

typedef struct {
    devfs_node_kind_t kind;
    device_t* dev; //for TTY or other devices
} devfs_priv_t;

//declarations for function pointers used in devfs_ops
static int devfs_open(vfs_node_t* node, uint32_t flags);
static int devfs_close(vfs_node_t* node);
static int devfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer);
static int devfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer);
static int devfs_create(vfs_node_t* parent, const char* name, uint32_t flags);
static int devfs_unlink(vfs_node_t* node);
static int devfs_mkdir(vfs_node_t* parent, const char* name, uint32_t flags);
static int devfs_rmdir(vfs_node_t* node);
static int devfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t** out);
static int devfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out);
static int devfs_get_size(vfs_node_t* node);
static int devfs_ioctl(vfs_node_t* node, uint32_t request, void* arg);

vfs_operations_t devfs_ops = {
    .open = devfs_open,
    .close = devfs_close,
    .read = devfs_read,
    .write = devfs_write,
    .create = devfs_create,
    .unlink = devfs_unlink,
    .mkdir = devfs_mkdir,
    .rmdir = devfs_rmdir,
    .readdir = devfs_readdir,
    .finddir = devfs_finddir,
    .get_size = devfs_get_size,
    .ioctl = devfs_ioctl,
};


static int devfs_open(vfs_node_t* node, uint32_t flags) {
    (void)node; (void)flags; return 0;
}

static int devfs_close(vfs_node_t* node) {
    if (node && node->private_data) {
        kfree(node->private_data);
        node->private_data = NULL;
    }
    return 0;
}

static int devfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer) {
    if (!node || !buffer) return -1;
    devfs_priv_t* p = (devfs_priv_t*)node->private_data;
    if (!p) return -1;
    switch (p->kind) {
        case DEVFS_NODE_NULL:
            return 0; //EOF
        case DEVFS_NODE_ZERO:
            memset(buffer, 0, size);
            return (int)size;
        case DEVFS_NODE_KMSG: {
            //read kernel log from ring buffer
            return (int)klog_copy(offset, buffer, size);
        }
        case DEVFS_NODE_RANDOM: {
            //LCG PRNG
            static uint32_t rng_state = 0;
            if (rng_state == 0) {
                //seed with ticks
                rng_state = (uint32_t)timer_get_ticks() ^ 0xA5A5A5A5u;
                if (rng_state == 0) rng_state = 0x12345678u;
            }
            uint32_t s = rng_state;
            for (uint32_t i = 0; i < size; i++) {
                //xorshift32 variant
                s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                buffer[i] = (char)(s & 0xFF);
            }
            rng_state = s;
            return (int)size;
        }
        case DEVFS_NODE_DEVICE:
            if (!p->dev) return -1;
            return device_read(p->dev, offset, buffer, size);
        default:
            return -1;
    }
}

static int devfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer) {
    if (!node || !buffer) return -1;
    devfs_priv_t* p = (devfs_priv_t*)node->private_data;
    if (!p) return -1;
    switch (p->kind) {
        case DEVFS_NODE_NULL:
        case DEVFS_NODE_ZERO:
            (void)offset; return (int)size; //discard
        case DEVFS_NODE_KMSG:
            //append to kernel log
            klog_write(buffer, size);
            return (int)size;
        case DEVFS_NODE_RANDOM:
            //allow seeding by writing 4 bytes else ignore
            (void)offset; (void)buffer; return (int)size;
        case DEVFS_NODE_DEVICE:
            if (!p->dev) return -1;
            return device_write(p->dev, offset, buffer, size);
        default:
            return -1;
    }
}

static int devfs_create(vfs_node_t* parent, const char* name, uint32_t flags) { 
    (void)parent; (void)name; (void)flags; 
    return -1; 
}

static int devfs_unlink(vfs_node_t* node) { 
    (void)node; 
    return -1; 
}

static int devfs_mkdir(vfs_node_t* parent, const char* name, uint32_t flags) { 
    (void)parent; (void)name; (void)flags; 
    return -1; 
}

static int devfs_rmdir(vfs_node_t* node) { 
    (void)node; 
    return -1; 
}

static int devfs_get_size(vfs_node_t* node) { 
    (void)node; 
    return 0; 
}

static int devfs_ioctl(vfs_node_t* node, uint32_t request, void* arg) {
    devfs_priv_t* p = (devfs_priv_t*)node->private_data;
    if (p && p->kind == DEVFS_NODE_DEVICE && p->dev) {
        return device_ioctl(p->dev, request, arg);
    }
    return -1;
}

static vfs_node_t* devfs_make_node(const char* name, uint32_t type, devfs_node_kind_t kind, device_t* dev, vfs_node_t* parent) {
    vfs_node_t* n = vfs_create_node(name, type, VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (!n) return NULL;
    n->ops = &devfs_ops;
    n->mount = parent ? parent->mount : NULL;
    n->parent = parent;
    devfs_priv_t* p = (devfs_priv_t*)kmalloc(sizeof(devfs_priv_t));
    if (!p) { 
        vfs_destroy_node(n); 
        return NULL; 
    }
    p->kind = kind; p->dev = dev;
    n->private_data = p;
    return n;
}

static int devfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t** out) {
    if (!node || !out) return -1;
    //built-ins first
    if (index == 0) { 
        *out = devfs_make_node("null", VFS_FILE_TYPE_DEVICE, DEVFS_NODE_NULL, NULL, node); 
        return *out ? 0 : -1; 
    }
    if (index == 1) { 
        *out = devfs_make_node("zero", VFS_FILE_TYPE_DEVICE, DEVFS_NODE_ZERO, NULL, node); 
        return *out ? 0 : -1; 
    }
    if (index == 2) { 
        *out = devfs_make_node("kmsg", VFS_FILE_TYPE_DEVICE, DEVFS_NODE_KMSG, NULL, node); 
        return *out ? 0 : -1; 
    }
    if (index == 3) { 
        *out = devfs_make_node("random", VFS_FILE_TYPE_DEVICE, DEVFS_NODE_RANDOM, NULL, node);
        return *out ? 0 : -1;
    }
    //enumerate devices from device manager
    uint32_t di = index - 4;
    device_t* dev = NULL;
    if (device_enumerate(di, &dev) != 0 || !dev) return -1;
    *out = devfs_make_node(dev->name, VFS_FILE_TYPE_DEVICE, DEVFS_NODE_DEVICE, dev, node);
    return *out ? 0 : -1;
}

static int devfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out) {
    if (!node || !name || !out) return -1;
    if (strcmp(name, "null") == 0) { 
        *out = devfs_make_node("null", VFS_FILE_TYPE_DEVICE, DEVFS_NODE_NULL, NULL, node); 
        return *out ? 0 : -1; 
    }
    if (strcmp(name, "zero") == 0) { 
        *out = devfs_make_node("zero", VFS_FILE_TYPE_DEVICE, DEVFS_NODE_ZERO, NULL, node); 
        return *out ? 0 : -1;
    }
    if (strcmp(name, "kmsg") == 0) { 
        *out = devfs_make_node("kmsg", VFS_FILE_TYPE_DEVICE, DEVFS_NODE_KMSG, NULL, node); 
        return *out ? 0 : -1; 
    }
    if (strcmp(name, "random") == 0) { 
        *out = devfs_make_node("random", VFS_FILE_TYPE_DEVICE, DEVFS_NODE_RANDOM, NULL, node); 
        return *out ? 0 : -1; 
    }
    //lookup device by name
    device_t* dev = device_find_by_name(name);
    if (dev) {
        *out = devfs_make_node(dev->name, VFS_FILE_TYPE_DEVICE, DEVFS_NODE_DEVICE, dev, node);
        return *out ? 0 : -1;
    }
    return -1;
}
