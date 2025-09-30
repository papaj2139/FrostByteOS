#include "tmpfs.h"
#include "vfs.h"
#include "../mm/heap.h"
#include "../libc/string.h"
#include <stddef.h>

#define TMPFS_MAX_ENTRIES 256
#define TMPFS_MAX_NAME 64

typedef struct tmpfs_entry {
    char name[TMPFS_MAX_NAME];
    uint32_t type;  //VFS_FILE_TYPE_*
    void* data;     //file content or subdirectory entries
    uint32_t size;  //file size
    uint32_t capacity; //allocated capacity
    struct tmpfs_entry* parent;
    struct tmpfs_entry* entries; //for directories
    uint32_t entry_count;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
} tmpfs_entry_t;

static tmpfs_entry_t* g_tmpfs_root = NULL;

//find entry in directory by name
static tmpfs_entry_t* tmpfs_find_entry(tmpfs_entry_t* dir, const char* name) {
    if (!dir || dir->type != VFS_FILE_TYPE_DIRECTORY || !name) return NULL;
    for (uint32_t i = 0; i < dir->entry_count; i++) {
        if (strcmp(dir->entries[i].name, name) == 0) {
            return &dir->entries[i];
        }
    }
    return NULL;
}

//create new entry in directory
static tmpfs_entry_t* tmpfs_create_entry(tmpfs_entry_t* dir, const char* name, uint32_t type) {
    if (!dir || dir->type != VFS_FILE_TYPE_DIRECTORY || !name) return NULL;
    if (dir->entry_count >= TMPFS_MAX_ENTRIES) return NULL;
    
    //check if already exists
    if (tmpfs_find_entry(dir, name)) return NULL;
    
    //allocate entries array if needed
    if (!dir->entries) {
        dir->entries = (tmpfs_entry_t*)kmalloc(sizeof(tmpfs_entry_t) * TMPFS_MAX_ENTRIES);
        if (!dir->entries) return NULL;
        memset(dir->entries, 0, sizeof(tmpfs_entry_t) * TMPFS_MAX_ENTRIES);
    }
    
    tmpfs_entry_t* entry = &dir->entries[dir->entry_count];
    strncpy(entry->name, name, TMPFS_MAX_NAME - 1);
    entry->name[TMPFS_MAX_NAME - 1] = '\0';
    entry->type = type;
    entry->data = NULL;
    entry->size = 0;
    entry->capacity = 0;
    entry->parent = dir;
    entry->entries = NULL;
    entry->entry_count = 0;
    entry->mode = (type == VFS_FILE_TYPE_DIRECTORY) ? 0755 : 0644;
    entry->uid = 0;
    entry->gid = 0;
    dir->entry_count++;
    
    return entry;
}

//VFS operations
static int tmpfs_open(vfs_node_t* node, uint32_t flags) {
    (void)node;
    (void)flags;
    return 0; //always succeeds for tmpfs
}

static int tmpfs_close(vfs_node_t* node) {
    (void)node;
    return 0;
}

static int tmpfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer) {
    if (!node || !node->private_data || !buffer) return -1;
    tmpfs_entry_t* entry = (tmpfs_entry_t*)node->private_data;
    if (entry->type != VFS_FILE_TYPE_FILE) return -1;
    if (offset >= entry->size) return 0; //EOF
    uint32_t available = entry->size - offset;
    uint32_t to_read = (size < available) ? size : available;
    if (entry->data) {
        memcpy(buffer, (char*)entry->data + offset, to_read);
    }
    return (int)to_read;
}

static int tmpfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer) {
    if (!node || !node->private_data || !buffer || size == 0) return -1;
    tmpfs_entry_t* entry = (tmpfs_entry_t*)node->private_data;
    if (entry->type != VFS_FILE_TYPE_FILE) return -1;
    
    uint32_t needed = offset + size;
    if (needed > entry->capacity) {
        //grow buffer
        uint32_t new_cap = needed * 2;
        if (new_cap < 64) new_cap = 64;
        void* new_data = kmalloc(new_cap);
        if (!new_data) return -1;
        if (entry->data) {
            memcpy(new_data, entry->data, entry->size);
            kfree(entry->data);
        }
        entry->data = new_data;
        entry->capacity = new_cap;
    }
    
    memcpy((char*)entry->data + offset, buffer, size);
    if (offset + size > entry->size) {
        entry->size = offset + size;
    }
    node->size = entry->size;
    return (int)size;
}

static int tmpfs_create(vfs_node_t* parent, const char* name, uint32_t flags) {
    (void)flags;
    if (!parent || !parent->private_data || !name) return -1;
    tmpfs_entry_t* dir = (tmpfs_entry_t*)parent->private_data;
    tmpfs_entry_t* entry = tmpfs_create_entry(dir, name, VFS_FILE_TYPE_FILE);
    return entry ? 0 : -1;
}

static int tmpfs_unlink(vfs_node_t* node) {
    if (!node || !node->private_data) return -1;
    tmpfs_entry_t* entry = (tmpfs_entry_t*)node->private_data;
    if (entry->type == VFS_FILE_TYPE_DIRECTORY && entry->entry_count > 0) return -1; //not empty
    
    //free data
    if (entry->data) {
        kfree(entry->data);
        entry->data = NULL;
    }
    if (entry->entries) {
        kfree(entry->entries);
        entry->entries = NULL;
    }
    
    //remove from parent
    tmpfs_entry_t* parent = entry->parent;
    if (parent) {
        for (uint32_t i = 0; i < parent->entry_count; i++) {
            if (&parent->entries[i] == entry) {
                //shift remaining entries
                for (uint32_t j = i; j < parent->entry_count - 1; j++) {
                    memcpy(&parent->entries[j], &parent->entries[j + 1], sizeof(tmpfs_entry_t));
                }
                parent->entry_count--;
                memset(&parent->entries[parent->entry_count], 0, sizeof(tmpfs_entry_t));
                break;
            }
        }
    }
    
    return 0;
}

static int tmpfs_mkdir(vfs_node_t* parent, const char* name, uint32_t flags) {
    (void)flags;
    if (!parent || !parent->private_data || !name) return -1;
    tmpfs_entry_t* dir = (tmpfs_entry_t*)parent->private_data;
    tmpfs_entry_t* entry = tmpfs_create_entry(dir, name, VFS_FILE_TYPE_DIRECTORY);
    return entry ? 0 : -1;
}

static int tmpfs_rmdir(vfs_node_t* node) {
    return tmpfs_unlink(node);
}

static int tmpfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t** out) {
    if (!node || !node->private_data || !out) return -1;
    tmpfs_entry_t* dir = (tmpfs_entry_t*)node->private_data;
    if (dir->type != VFS_FILE_TYPE_DIRECTORY) return -1;
    if (index >= dir->entry_count) return -1;
    
    tmpfs_entry_t* entry = &dir->entries[index];
    vfs_node_t* child = vfs_create_node(entry->name, entry->type, 0);
    if (!child) return -1;
    
    child->size = entry->size;
    child->ops = node->ops;
    child->private_data = entry;
    child->parent = node;
    child->mode = entry->mode;
    child->uid = entry->uid;
    child->gid = entry->gid;
    
    *out = child;
    return 0;
}

static int tmpfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out) {
    if (!node || !node->private_data || !name || !out) return -1;
    tmpfs_entry_t* dir = (tmpfs_entry_t*)node->private_data;
    tmpfs_entry_t* entry = tmpfs_find_entry(dir, name);
    if (!entry) return -1;
    
    vfs_node_t* child = vfs_create_node(entry->name, entry->type, 0);
    if (!child) return -1;
    
    child->size = entry->size;
    child->ops = node->ops;
    child->private_data = entry;
    child->parent = node;
    child->mode = entry->mode;
    child->uid = entry->uid;
    child->gid = entry->gid;
    
    *out = child;
    return 0;
}

static vfs_operations_t tmpfs_ops = {
    .open = tmpfs_open,
    .close = tmpfs_close,
    .read = tmpfs_read,
    .write = tmpfs_write,
    .create = tmpfs_create,
    .unlink = tmpfs_unlink,
    .mkdir = tmpfs_mkdir,
    .rmdir = tmpfs_rmdir,
    .readdir = tmpfs_readdir,
    .finddir = tmpfs_finddir,
    .get_size = NULL,
    .ioctl = NULL,
    .readlink = NULL,
    .symlink = NULL,
    .link = NULL,
};

int tmpfs_init(void) {
    //create root entry
    g_tmpfs_root = (tmpfs_entry_t*)kmalloc(sizeof(tmpfs_entry_t));
    if (!g_tmpfs_root) return -1;
    
    memset(g_tmpfs_root, 0, sizeof(tmpfs_entry_t));
    strncpy(g_tmpfs_root->name, "tmpfs_root", TMPFS_MAX_NAME - 1);
    g_tmpfs_root->type = VFS_FILE_TYPE_DIRECTORY;
    g_tmpfs_root->mode = 0777;
    g_tmpfs_root->uid = 0;
    g_tmpfs_root->gid = 0;
    
    return 0;
}

vfs_node_t* tmpfs_get_root(void) {
    if (!g_tmpfs_root) return NULL;
    
    vfs_node_t* root = vfs_create_node("tmp", VFS_FILE_TYPE_DIRECTORY, 0);
    if (!root) return NULL;
    
    root->ops = &tmpfs_ops;
    root->private_data = g_tmpfs_root;
    root->mode = g_tmpfs_root->mode;
    root->uid = g_tmpfs_root->uid;
    root->gid = g_tmpfs_root->gid;
    
    return root;
}