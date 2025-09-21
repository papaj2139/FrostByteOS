#include "vfs.h"
#include "fs.h"
#include "../drivers/serial.h"
#include "../debug.h"
#include <string.h>
#include <stdbool.h>
#include "../libc/stdlib.h"
#include "../mm/heap.h"

//root VFS node
vfs_node_t* vfs_root = NULL;

//registered filesystems
typedef struct vfs_fs_type {
    char name[32];
    vfs_operations_t* ops;
    struct vfs_fs_type* next;
} vfs_fs_type_t;

static vfs_fs_type_t* registered_fs_types = NULL;
static vfs_mount_t* mount_list = NULL;

static vfs_node_t* vfs_resolve_path_internal(const char* path, int depth);

//VFS debug function
static void vfs_debug(const char* msg) {
    (void)msg;
#if LOG_VFS
    serial_write_string("[VFS] ");
    serial_write_string(msg);
    serial_write_string("\n");
#endif
}

static void vfs_debug_path(const char* prefix, const char* path) {
    (void)prefix; (void)path;
#if LOG_VFS
    serial_write_string("[VFS] ");
    serial_write_string(prefix);
    serial_write_string(": ");
    serial_write_string(path);
    serial_write_string("\n");
#endif
}

//initialize VFS
int vfs_init(void) {
    vfs_debug("Initializing VFS");

    //create root node
    vfs_root = vfs_create_node("/", VFS_FILE_TYPE_DIRECTORY, VFS_FLAG_READ);
    if (!vfs_root) {
        vfs_debug("Failed to create root node");
        return -1;
    }

    //set root parent to itself
    vfs_root->parent = vfs_root;

    vfs_debug("VFS initialized successfully");
    return 0;
}

//register a filesystem type
int vfs_register_fs(const char* name, vfs_operations_t* ops) {
    if (!name || !ops) {
        return -1;
    }

    //check if filesystem type already registered
    vfs_fs_type_t* current = registered_fs_types;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return -1; //already registered
        }
        current = current->next;
    }

    //allocate new filesystem type
    vfs_fs_type_t* fs_type = (vfs_fs_type_t*)kmalloc(sizeof(vfs_fs_type_t));
    if (!fs_type) {
        return -1;
    }

    //initialize filesystem type
    memset(fs_type, 0, sizeof(vfs_fs_type_t));
    strncpy(fs_type->name, name, sizeof(fs_type->name) - 1);
    fs_type->name[sizeof(fs_type->name) - 1] = '\0';
    fs_type->ops = ops;
    fs_type->next = registered_fs_types;
    registered_fs_types = fs_type;

    return 0;
}

//find a registered filesystem type by name
static vfs_fs_type_t* vfs_find_fs_type(const char* name) {
    vfs_fs_type_t* current = registered_fs_types;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

//create a new VFS node
vfs_node_t* vfs_create_node(const char* name, uint32_t type, uint32_t flags) {
    if (!name) {
        return NULL;
    }

    //allocate node
    vfs_node_t* node = (vfs_node_t*)kmalloc(sizeof(vfs_node_t));
    if (!node) {
        return NULL;
    }

    //initialize node
    memset(node, 0, sizeof(vfs_node_t));
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    node->type = type;
    node->flags = flags;
    node->ref_count = 1;

    return node;
}

//destroy a VFS node
void vfs_destroy_node(vfs_node_t* node) {
    if (!node) {
        return;
    }

    node->ref_count--;
    if (node->ref_count > 0) {
        return;
    }

    //call filesyste specific cleanup if needed
    if (node->ops && node->ops->close) {
        node->ops->close(node);
    }

    kfree(node);
}

//allow setting root node ops and private data directly (e.x for initramfs)
int vfs_set_root_ops(vfs_operations_t* ops, void* private_data) {
    if (!vfs_root || !ops) return -1;
    vfs_root->ops = ops;
    vfs_root->private_data = private_data;
    vfs_root->type = VFS_FILE_TYPE_DIRECTORY;
    vfs_root->flags = VFS_FLAG_READ;
    return 0;
}

//get parent path from a full path
char* vfs_get_parent_path(const char* path) {
    if (!path) return NULL;

    //find the last slash
    const char* last_slash = strrchr(path, '/');
    if (!last_slash) return NULL;

    //if its the root directory
    if (last_slash == path) {
        char* result = (char*)kmalloc(2);
        if (result) {
            result[0] = '/';
            result[1] = '\0';
        }
        return result;
    }

    //calculate length and allocate
    size_t len = last_slash - path;
    char* result = (char*)kmalloc(len + 1);
    if (result) {
        memcpy(result, path, len);
        result[len] = '\0';
    }
    return result;
}

//get basename from a full path
char* vfs_get_basename(const char* path) {
    if (!path) return NULL;

    //find the last slash
    const char* last_slash = strrchr(path, '/');
    if (!last_slash) {
        //no / return copy of entire path
        char* result = (char*)kmalloc(strlen(path) + 1);
        if (result) {
            strcpy(result, path);
        }
        return result;
    }

    //return everything after the last slash
    const char* basename = last_slash + 1;
    char* result = (char*)kmalloc(strlen(basename) + 1);
    if (result) {
        strcpy(result, basename);
    }
    return result;
}

//compare two paths
int vfs_path_compare(const char* path1, const char* path2) {
    if (!path1 && !path2) return 1;
    if (!path1 || !path2) return 0;
    return strcmp(path1, path2) == 0;
}

//find a mount point that matches the given path
static vfs_mount_t* vfs_find_mount(const char* path) {
    if (!path) return NULL;

    //choose the longest mount point that is a prefix of 'path'
    vfs_mount_t* best = NULL;
    size_t best_len = 0;
    for (vfs_mount_t* cur = mount_list; cur; cur = cur->next) {
        const char* mp = cur->mount_point;
        size_t ml = strlen(mp);
        if (ml == 0) continue;
        //'/' matches everything but has length 1 prefer longer matches
        if (strncmp(path, mp, ml) == 0) {
            //ensure boundary: either exact match or next char in path is '/'
            if (path[ml] == '\0' || path[ml] == '/') {
                if (ml > best_len) { 
                    best = cur; 
                    best_len = ml; 
                }
            }
        }
    }
    return best;
}

//mount a filesystem
int vfs_mount(const char* device, const char* mount_point, const char* fs_type) {
    vfs_debug_path("Mounting filesystem", mount_point);

    //find filesystem type
    vfs_fs_type_t* fs_type_entry = vfs_find_fs_type(fs_type);
    if (!fs_type_entry) {
        vfs_debug("Unknown filesystem type");
        return -1;
    }

    //find the device optional for virual FS'es
    device_t* dev = NULL;
    if (device && strcmp(device, "none") != 0) {
        dev = device_find_by_name(device);
        if (!dev) {
            vfs_debug("Device not found");
            return -1;
        }
    }

    //allocate mount structure
    vfs_mount_t* mount = (vfs_mount_t*)kmalloc(sizeof(vfs_mount_t));
    if (!mount) {
        vfs_debug("Failed to allocate mount structure");
        return -1;
    }

    //initialize mount structure
    memset(mount, 0, sizeof(vfs_mount_t));
    strncpy(mount->mount_point, mount_point, sizeof(mount->mount_point) - 1);
    mount->mount_point[sizeof(mount->mount_point) - 1] = '\0';

    //initialize filesystem for physical filesystems only (require device)
    bool use_physical_fs = (dev != NULL);
    filesystem_t* fs = NULL;
    if (use_physical_fs) {
        //create a filesystem structure
        fs = (filesystem_t*)kmalloc(sizeof(filesystem_t));
        if (!fs) {
            kfree(mount);
            vfs_debug("Failed to allocate filesystem structure");
            return -1;
        }
        if (fs_init(fs, dev) != 0) {
            kfree(fs);
            kfree(mount);
            vfs_debug("Failed to initialize filesystem");
            return -1;
        }
    }

    //create root node for this filesystem
    //virtual filesystems (no backing device) are read-only by default
    uint32_t root_flags = use_physical_fs ? (VFS_FLAG_READ | VFS_FLAG_WRITE) : VFS_FLAG_READ;
    mount->root = vfs_create_node(mount_point, VFS_FILE_TYPE_DIRECTORY, root_flags);
    if (!mount->root) {
        if (fs) kfree(fs);
        kfree(mount);
        vfs_debug("Failed to create root node");
        return -1;
    }

    //set up the node with filesystem operations
    mount->root->ops = fs_type_entry->ops;
    mount->root->device = dev;
    mount->root->private_data = use_physical_fs ? (void*)fs : NULL;
    mount->root->mount = mount;
    //store device for physical FS NULL for virtual
    mount->mount_device = use_physical_fs ? dev : NULL;
    //keep a copy at the mount level so we can free it on unmount even if
    //root->private_data is later replaced by FS-specific directory data
    mount->private_data = use_physical_fs ? (void*)fs : NULL;
    //store fs type name
    memset(mount->fs_name, 0, sizeof(mount->fs_name));
    strncpy(mount->fs_name, fs_type, sizeof(mount->fs_name) - 1);

    //add to mount list
    mount->next = mount_list;   
    mount_list = mount;

    vfs_debug("Filesystem mounted successfully");
    return 0;
}

//unmount a filesystem
int vfs_unmount(const char* mount_point) {
    if (!mount_point) return -1;

    vfs_mount_t* current = mount_list;
    vfs_mount_t* prev = NULL;

    while (current) {
        if (vfs_path_compare(current->mount_point, mount_point)) {
            //remove from list
            if (prev) {
                prev->next = current->next;
            } else {
                mount_list = current->next;
            }

            //clean up
            void* saved_fs = current->private_data;
            void* saved_root_priv = current->root ? current->root->private_data : NULL;
            if (current->root) {
                vfs_destroy_node(current->root);
            }
            if (saved_fs && saved_fs != saved_root_priv) {
                kfree(saved_fs);
            }
            kfree(current);

            return 0;
        }
        prev = current;
        current = current->next;
    }

    return -1;
}

//resolve a path to a VFS node (public API)
vfs_node_t* vfs_resolve_path(const char* path) {
    return vfs_resolve_path_internal(path, 0);
}

//internal resolver with symlink following
static vfs_node_t* vfs_resolve_path_internal(const char* path, int depth) {
    if (!path) {
        return NULL;
    }

    vfs_debug_path("Resolving path", path);

    //only handle absolute paths atleast rn
    if (path[0] != '/') {
        return NULL;
    }

    //find the best mount for this path (longest prefix)
    vfs_mount_t* m = vfs_find_mount(path);
    vfs_node_t* current_node = NULL;
    const char* p = NULL;
    if (!m) {
        //fallback to generic root node (e.x via vfs_set_root_ops)
        if (!vfs_root) return NULL;
        current_node = vfs_root;
        current_node->ref_count++;
        p = path + 1; //skip leading '/'
    } else {
        //start from mounted filesystem root
        current_node = m->root;
        current_node->ref_count++;
        size_t ml = strlen(m->mount_point);
        if (ml <= 1) {
            p = path + 1; //root mount
        } else {
            p = path + ml;
            while (*p == '/') p++; //skip any extra separators
        }
    }

    //if path equals the mount point exactly return its root node
    if (*p == '\0') {
        return current_node;
    }

    //traverse the path component by component
    while (*p) {
        //find the next component
        const char* end = p;
        while (*end && *end != '/') end++;

        //extract component name
        size_t len = end - p;
        if (len == 0) break;

        char component[64];
        if (len >= sizeof(component)) len = sizeof(component) - 1;
        memcpy(component, p, len);
        component[len] = '\0';

        //open the directory so it can search it
        if (current_node->ops && current_node->ops->open) {
            current_node->ops->open(current_node, VFS_FLAG_READ);
        }

        //look for this component in the current directory
        vfs_node_t* child = NULL;
        if (current_node->ops && current_node->ops->finddir) {
            if (current_node->ops->finddir(current_node, component, &child) == 0) {
                //found it release the parent and continue with the child
                vfs_close(current_node); //vfs_close just decrements ref_count
                //handle symlink
                if (child && child->type == VFS_FILE_TYPE_SYMLINK) {
                    if (depth > 8) {
                        vfs_close(child);
                        vfs_debug("Symlink recursion limit reached");
                        return NULL;
                    }
                    if (child->ops && child->ops->readlink) {
                        char target[512];
                        int rl = child->ops->readlink(child, target, sizeof(target));
                        if (rl < 0) {
                            vfs_close(child);
                            return NULL;
                        }
                        target[sizeof(target)-1] = '\0';
                        //compose new path: target [+ '/' + rest]
                        const char* rest = end;
                        while (*rest == '/') rest++;
                        char newpath[1024];
                        newpath[0] = '\0';
                        if (target[0] == '/') {
                            strncpy(newpath, target, sizeof(newpath) - 1);
                            newpath[sizeof(newpath)-1] = '\0';
                        } else {
                            //prefix up to component start (path .. p)
                            size_t prefix_len = (size_t)(p - path);
                            if (prefix_len >= sizeof(newpath)) prefix_len = sizeof(newpath) - 1;
                            memcpy(newpath, path, prefix_len);
                            newpath[prefix_len] = '\0';
                            //ensure trailing slash
                            size_t nl = strlen(newpath);
                            if (nl == 0 || newpath[nl-1] != '/') {
                                if (nl + 1 < sizeof(newpath)) { newpath[nl++] = '/'; newpath[nl] = '\0'; }
                            }
                            strncat(newpath, target, sizeof(newpath) - strlen(newpath) - 1);
                        }
                        if (*rest) {
                            size_t nl = strlen(newpath);
                            if (nl > 0 && newpath[nl-1] != '/') strncat(newpath, "/", sizeof(newpath) - nl - 1);
                            strncat(newpath, rest, sizeof(newpath) - strlen(newpath) - 1);
                        }
                        vfs_close(child);
                        return vfs_resolve_path_internal(newpath, depth + 1);
                    }
                }
                current_node = child;
            } else {
                //not found
                vfs_close(current_node);
                vfs_debug_path("Component not found", component);
                return NULL;
            }
        } else {
            //no finddir operation can't traverse
            vfs_close(current_node);
            vfs_debug("No finddir operation on current node");
            return NULL;
        }

        //move to the next component
        p = end;
        if (*p == '/') p++;
    }

    return current_node;
}

int vfs_symlink(const char* target, const char* linkpath) {
    if (!target || !linkpath) return -1;
    //get parent directory
    char* parent_path = vfs_get_parent_path(linkpath);
    if (!parent_path) return -1;
    char* linkname = vfs_get_basename(linkpath);
    if (!linkname) { 
        kfree(parent_path); 
        return -1; 
    }
    vfs_node_t* parent = vfs_open(parent_path, VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (!parent) { 
        kfree(parent_path); 
        kfree(linkname); 
        return -1; 
    }
    int r = -1;
    if (parent->ops && parent->ops->symlink) {
        r = parent->ops->symlink(parent, linkname, target);
    }
    vfs_close(parent);
    kfree(parent_path);
    kfree(linkname);
    return r;
}

int vfs_readlink(const char* path, char* buf, uint32_t bufsize) {
    if (!path || !buf || bufsize == 0) return -1;
    vfs_node_t* node = vfs_resolve_path(path);
    if (!node) return -1;
    int r = -1;
    if (node->type == VFS_FILE_TYPE_SYMLINK && node->ops && node->ops->readlink) {
        r = node->ops->readlink(node, buf, bufsize);
    }
    vfs_close(node);
    return r;
}

//open a file or directory
vfs_node_t* vfs_open(const char* path, uint32_t flags) {
    vfs_debug_path("Opening", path);

    //resolve path to node
    vfs_node_t* node = vfs_resolve_path(path);
    if (!node) {
        vfs_debug("Failed to resolve path");
        return NULL;
    }

    //check permissions
    if ((flags & VFS_FLAG_READ) && !(node->flags & VFS_FLAG_READ)) {
        vfs_destroy_node(node);
        vfs_debug("Read permission denied");
        return NULL;
    }

    if ((flags & VFS_FLAG_WRITE) && !(node->flags & VFS_FLAG_WRITE)) {
        vfs_destroy_node(node);
        vfs_debug("Write permission denied");
        return NULL;
    }

    //call filesystem specific open if available
    if (node->ops && node->ops->open) {
        if (node->ops->open(node, flags) != 0) {
            vfs_destroy_node(node);
            vfs_debug("Filesystem-specific open failed");
            return NULL;
        }
    }

    return node;
}

//close a file or directory
int vfs_close(vfs_node_t* node) {
    if (!node) {
        return -1;
    }

    vfs_debug_path("Closing", node->name);
    vfs_destroy_node(node);
    return 0;
}

//read from a file
int vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer) {
    if (!node || !buffer) {
        return -1;
    }

    vfs_debug("Reading from file");

    //check if its a directory
    if (node->type == VFS_FILE_TYPE_DIRECTORY) {
        vfs_debug("Cannot read from directory");
        return -1;
    }

    //check permissions
    if (!(node->flags & VFS_FLAG_READ)) {
        vfs_debug("Read permission denied");
        return -1;
    }

    //call filesystem-specific read
    if (node->ops && node->ops->read) {
        return node->ops->read(node, offset, size, buffer);
    }

    return -1;
}

//write to a file
int vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer) {
    if (!node || !buffer) {
        return -1;
    }

    vfs_debug("Writing to file");

    //check if its a directory
    if (node->type == VFS_FILE_TYPE_DIRECTORY) {
        vfs_debug("Cannot write to directory");
        return -1;
    }

    //check permissions
    if (!(node->flags & VFS_FLAG_WRITE)) {
        vfs_debug("Write permission denied");
        return -1;
    }

    //call filesystemspecific write
    if (node->ops && node->ops->write) {
        return node->ops->write(node, offset, size, buffer);
    }

    return -1;
}

//create a file
int vfs_create(const char* path, uint32_t flags) {
    if (!path) {
        return -1;
    }

    vfs_debug_path("Creating file", path);

    //get parent directory path
    char* parent_path = vfs_get_parent_path(path);
    if (!parent_path) {
        return -1;
    }

    //get filename
    char* filename = vfs_get_basename(path);
    if (!filename) {
        kfree(parent_path);
        return -1;
    }

    //open parent directory
    vfs_node_t* parent = vfs_open(parent_path, VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (!parent) {
        kfree(parent_path);
        kfree(filename);
        return -1;
    }

    //check if parent is a directory
    if (parent->type != VFS_FILE_TYPE_DIRECTORY) {
        vfs_close(parent);
        kfree(parent_path);
        kfree(filename);
        return -1;
    }

    //call filesystem-specific create
    int result = -1;
    if (parent->ops && parent->ops->create) {
        result = parent->ops->create(parent, filename, flags);
    }

    //clean up
    vfs_close(parent);
    kfree(parent_path);
    kfree(filename);

    return result;
}

//delete a file
int vfs_unlink(const char* path) {
    if (!path) {
        return -1;
    }

    vfs_debug_path("Deleting file", path);

    //resolve the node to delete
    vfs_node_t* node = vfs_resolve_path(path);
    if (!node) {
        return -1;
    }

    //get parent directory path
    char* parent_path = vfs_get_parent_path(path);
    if (!parent_path) {
        vfs_close(node);
        return -1;
    }

    //open parent directory
    vfs_node_t* parent = vfs_open(parent_path, VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (!parent) {
        kfree(parent_path);
        vfs_close(node);
        return -1;
    }

    //call filesystem-specific unlink
    int result = -1;
    if (parent->ops && parent->ops->unlink) {
        result = parent->ops->unlink(node);
    }

    //clean up
    vfs_close(parent);
    vfs_close(node);
    kfree(parent_path);

    return result;
}

//create a directory
int vfs_mkdir(const char* path, uint32_t flags) {
    if (!path) {
        return -1;
    }

    vfs_debug_path("Creating directory", path);

    //get parent directory path
    char* parent_path = vfs_get_parent_path(path);
    if (!parent_path) {
        return -1;
    }

    //get directory name
    char* dirname = vfs_get_basename(path);
    if (!dirname) {
        kfree(parent_path);
        return -1;
    }

    //open parent directory
    vfs_node_t* parent = vfs_open(parent_path, VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (!parent) {
        kfree(parent_path);
        kfree(dirname);
        return -1;
    }

    //check if parent is a directory
    if (parent->type != VFS_FILE_TYPE_DIRECTORY) {
        vfs_close(parent);
        kfree(parent_path);
        kfree(dirname);
        return -1;
    }

    //call filesystem-specific mkdir
    int result = -1;
    if (parent->ops && parent->ops->mkdir) {
        result = parent->ops->mkdir(parent, dirname, flags);
    }

    //clean up
    vfs_close(parent);
    kfree(parent_path);
    kfree(dirname);

    return result;
}

//remove a directory
int vfs_rmdir(const char* path) {
    if (!path) {
        return -1;
    }

    vfs_debug_path("Removing directory", path);

    //resolve the directory to remove
    vfs_node_t* node = vfs_resolve_path(path);
    if (!node) {
        return -1;
    }

    //check if it's actually a directory
    if (node->type != VFS_FILE_TYPE_DIRECTORY) {
        vfs_close(node);
        return -1;
    }

    //get parent directory path
    char* parent_path = vfs_get_parent_path(path);
    if (!parent_path) {
        vfs_close(node);
        return -1;
    }

    //open parent directory
    vfs_node_t* parent = vfs_open(parent_path, VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (!parent) {
        kfree(parent_path);
        vfs_close(node);
        return -1;
    }

    //call filesystem-specific rmdir
    int result = -1;
    if (parent->ops && parent->ops->rmdir) {
        result = parent->ops->rmdir(node);
    }

    //clean up
    vfs_close(parent);
    vfs_close(node);
    kfree(parent_path);

    return result;
}

//read a directory entry
int vfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t** out) {
    if (!node || !out) {
        return -1;
    }

    //check if its a directory
    if (node->type != VFS_FILE_TYPE_DIRECTORY) {
        return -1;
    }

    //call filesystem-specific readdir
    if (node->ops && node->ops->readdir) {
        return node->ops->readdir(node, index, out);
    }

    return -1;
}

//find a directory entry by name
int vfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out) {
    if (!node || !name || !out) {
        return -1;
    }

    //check if its a directory
    if (node->type != VFS_FILE_TYPE_DIRECTORY) {
        return -1;
    }

    //call filesystem-specific finddir
    if (node->ops && node->ops->finddir) {
        return node->ops->finddir(node, name, out);
    }

    return -1;
}

//get file size
int vfs_get_size(vfs_node_t* node) {
    if (!node) {
        return -1;
    }

    //call filesystem-specific get_size
    if (node->ops && node->ops->get_size) {
        return node->ops->get_size(node);
    }

    return node->size;
}

//expose mounts for read-only iteration (like ProcFS)
const vfs_mount_t* vfs_get_mounts(void) {
    return mount_list;
}
