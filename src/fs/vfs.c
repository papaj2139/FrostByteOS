#include "vfs.h"
#include "fs.h"
#include "../drivers/serial.h"
#include "../debug.h"
#include <string.h>
#include <stdbool.h>
#include "../libc/stdlib.h"
#include "../mm/heap.h"
#include "tmpfs.h"
#include "fat16_vfs.h"
#include "fat32_vfs.h"

//toggle to disable strict permission enforcement
#ifndef VFS_ENFORCE_PERMS
#define VFS_ENFORCE_PERMS 0
#endif

//VFS metadata overlay
typedef struct vfs_meta_override {
    char path[VFS_MAX_PATH];
    int has_mode; uint32_t mode;
    int has_uid;  uint32_t uid;
    int has_gid;  uint32_t gid;
    struct vfs_meta_override* next;
} vfs_meta_override_t;

static vfs_meta_override_t* g_meta_overrides = NULL;

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

static vfs_node_t* vfs_resolve_path_internal2(const char* path, int depth, bool nofollow_last);
static vfs_node_t* vfs_resolve_path_internal(const char* path, int depth) {
    return vfs_resolve_path_internal2(path, depth, false);
}

//VFS debug function
static void vfs_debug(const char* msg) {
    (void)msg;
    #if LOG_VFS
        serial_write_string("[VFS] ");
        serial_write_string(msg);
        serial_write_string("\n");
    #endif
}

static vfs_meta_override_t* vfs_find_override(const char* abspath) {
    for (vfs_meta_override_t* it = g_meta_overrides; it; it = it->next) {
        if (strncmp(it->path, abspath, VFS_MAX_PATH) == 0) return it;
    }
    return NULL;
}

int vfs_set_metadata_override(const char* abspath, int has_mode, uint32_t mode,
                              int has_uid, uint32_t uid,
                              int has_gid, uint32_t gid)
{
    if (!abspath || abspath[0] != '/') return -1;
    vfs_meta_override_t* o = vfs_find_override(abspath);
    if (!o) {
        o = (vfs_meta_override_t*)kmalloc(sizeof(*o));
        if (!o) return -1;
        memset(o, 0, sizeof(*o));
        strncpy(o->path, abspath, sizeof(o->path) - 1);
        o->path[sizeof(o->path) - 1] = '\0';
        o->next = g_meta_overrides;
        g_meta_overrides = o;
    }
    if (has_mode) {
        o->has_mode = 1;
        o->mode = mode & 07777;
    }
    if (has_uid)  {
        o->has_uid  = 1;
        o->uid  = uid;
    }
    if (has_gid)  {
        o->has_gid  = 1;
        o->gid  = gid;
    }
    return 0;
}

void vfs_apply_metadata_override(vfs_node_t* node, const char* abspath) {
    if (!node || !abspath) return;
    vfs_meta_override_t* o = vfs_find_override(abspath);
    if (!o) return;
    if (o->has_mode) node->mode = o->mode;
    if (o->has_uid)  node->uid  = o->uid;
    if (o->has_gid)  node->gid  = o->gid;
}

//enumerate registered filesystem type names (read-only snapshot)
int vfs_list_fs_types(char names[][32], uint32_t max, uint32_t* out_count) {
    if (!names || max == 0) {
        if (out_count) *out_count = 0;
        return -1;
    }
    uint32_t cnt = 0;
    for (vfs_fs_type_t* cur = registered_fs_types; cur && cnt < max; cur = cur->next) {
        strncpy(names[cnt], cur->name, 31);
        names[cnt][31] = '\0';
        cnt++;
    }
    if (out_count) *out_count = cnt;
    return 0;
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
    //default ownership and perms (root:root)
    node->uid = 0;
    node->gid = 0;
    //dirs default 0755 files 0644 devices 0666
    if (type == VFS_FILE_TYPE_DIRECTORY) node->mode = S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
    else if (type == VFS_FILE_TYPE_DEVICE) node->mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH;
    else node->mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH;

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

//normalize 'path' against base (if relative) resolving '.' and '..' components
//the result is always an absolute path beginning with '/' returns 0 on success -1 on overflow/invalid
static int vfs_push_seg(char segs[][64], int* segc, const char* s, size_t len) {
    if (!s || !segc) return -1;
    if (len == 0) return 0;
    if (*segc >= (int)(32)) return -1;
    if (len >= 64) len = 63;
    memcpy(segs[*segc], s, len);
    segs[*segc][len] = '\0';
    (*segc)++;
    return 0;
}

static void vfs_parse_into(char segs[][64], int* segc, const char* p) {
    if (!p) return;
    while (*p == '/') p++;
    while (*p) {
        const char* start = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - start);
        if (len == 0) { while (*p == '/') p++; continue; }
        if (len == 1 && start[0] == '.') {
            //skip
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (*segc > 0) (*segc)--; //pop
        } else {
            vfs_push_seg(segs, segc, start, len);
        }
        while (*p == '/') p++;
    }
}

int vfs_normalize_path(const char* base, const char* path, char* out, size_t outsz) {
    if (!path || !out || outsz == 0) return -1;
    char segs[32][64];
    int segc = 0;
    if (path[0] == '/') {
        vfs_parse_into(segs, &segc, path);
    } else {
        const char* b = (base && base[0]) ? base : "/";
        if (b[0] == '/') vfs_parse_into(segs, &segc, b);
        vfs_parse_into(segs, &segc, path);
    }
    size_t pos = 0;
    if (pos < outsz) out[pos++] = '/'; else return -1;
    if (segc == 0) { if (pos < outsz) { out[pos] = '\0'; return 0; } else return -1; }
    for (int i = 0; i < segc; i++) {
        size_t sl = strlen(segs[i]);
        if (pos + sl >= outsz) return -1;
        memcpy(&out[pos], segs[i], sl); pos += sl;
        if (i != segc - 1) {
            if (pos + 1 >= outsz) return -1;
            out[pos++] = '/';
        }
    }
    if (pos >= outsz) return -1;
    out[pos] = '\0';
    return 0;
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
            //ensure boundary either exact match or next char in path is '/'
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
    int is_tmpfs = (strcmp(fs_type, "tmpfs") == 0);
    if (!fs_type_entry && !is_tmpfs) {
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
    //tmpfs, fat16, and fat32 get their own root nodes
    int is_fat16 = (strcmp(fs_type, "fat16") == 0);
    int is_fat32 = (strcmp(fs_type, "fat32") == 0);
    if (is_tmpfs) {
        mount->root = tmpfs_get_root();
        if (!mount->root) {
            kfree(mount);
            vfs_debug("Failed to get tmpfs root");
            return -1;
        }
        mount->root->mount = mount;
    } else if (is_fat16 && fs) {
        //FAT16 needs special root node setup
        void* fat16_mount_data = &fs->fs_data.fat16;
        mount->root = fat16_get_root(fat16_mount_data);
        if (!mount->root) {
            if (fs) kfree(fs);
            kfree(mount);
            vfs_debug("Failed to get FAT16 root");
            return -1;
        }
        mount->root->mount = mount;
        mount->root->device = dev;
    } else if (is_fat32 && fs) {
        //FAT32 needs special root node setup
        void* fat32_mount_data = fs->fs_data.fat32_mount;
        mount->root = fat32_get_root(fat32_mount_data);
        if (!mount->root) {
            if (fs) kfree(fs);
            kfree(mount);
            vfs_debug("Failed to get FAT32 root");
            return -1;
        }
        mount->root->mount = mount;
        mount->root->device = dev;
    } else {
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
    }
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

//resolve a path to a VFS node
vfs_node_t* vfs_resolve_path(const char* path) {
    vfs_node_t* n = vfs_resolve_path_internal2(path, 0, false);
    if (n) vfs_apply_metadata_override(n, path);
    return n;
}

vfs_node_t* vfs_resolve_path_nofollow(const char* path) {
    return vfs_resolve_path_internal2(path, 0, true);
}

//internal resolver with symlink following
static vfs_node_t* vfs_resolve_path_internal2(const char* path, int depth, bool nofollow_last) {
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

        #if VFS_ENFORCE_PERMS
                //check execute/search permission on the current directory
                process_t* curp = process_get_current();
                //compute which bits apply
                uint32_t mode = current_node->mode;
                uint32_t mask = (curp && curp->euid == current_node->uid) ? (S_IXUSR)
                            : (curp && curp->egid == current_node->gid) ? (S_IXGRP)
                            : (S_IXOTH);
                if ((mask & S_IXUSR) && !(mode & S_IXUSR)) {
                    vfs_close(current_node);
                    return NULL;
                }
                if ((mask & S_IXGRP) && !(mode & S_IXGRP)) {
                    vfs_close(current_node);
                    return NULL;
                }
                if ((mask & S_IXOTH) && !(mode & S_IXOTH)) {
                    vfs_close(current_node);
                    return NULL;
                }
        #endif
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
                bool is_last = (*end == '\0');
                if (child && child->type == VFS_FILE_TYPE_SYMLINK && !(nofollow_last && is_last)) {
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
                        //normalize composed path to fold any '.'/'..' before recursing
                        char norm[1024];
                        if (vfs_normalize_path("/", newpath, norm, sizeof(norm)) != 0) {
                            vfs_close(child);
                            return NULL;
                        }
                        vfs_close(child);
                        return vfs_resolve_path_internal(norm, depth + 1);
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
    //apply overlay again to be certain (ensures permission checks use overridden bits)
    if (node) vfs_apply_metadata_override(node, path);
    if (!node) {
        vfs_debug("Failed to resolve path");
        return NULL;
    }

    #if VFS_ENFORCE_PERMS
        //check permissions based on uid/gid/mode
        process_t* curp = process_get_current();
        uint32_t mode = node->mode;
        //select class
        int cls = 2; // other
        if (curp && curp->euid == node->uid) cls = 0;
            else if (curp && curp->egid == node->gid) cls = 1;
        uint32_t rbit = (cls==0?S_IRUSR:(cls==1?S_IRGRP:S_IROTH));
        uint32_t wbit = (cls==0?S_IWUSR:(cls==1?S_IWGRP:S_IWOTH));
        uint32_t xbit = (cls==0?S_IXUSR:(cls==1?S_IXGRP:S_IXOTH));
        if ((flags & VFS_FLAG_READ) && !(mode & rbit)) {
            vfs_destroy_node(node);
            return NULL;
        }
        if ((flags & VFS_FLAG_WRITE) && !(mode & wbit)) {
            vfs_destroy_node(node);
            return NULL;
        }
        if ((flags & VFS_FLAG_EXECUTE) && !(mode & xbit)) {
            vfs_destroy_node(node);
            return NULL;
        }
    #endif

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

    #if VFS_ENFORCE_PERMS
        //check permissions
        //allow read only if read bit set
        process_t* curp = process_get_current();
        int cls = 2; if (curp && curp->euid == node->uid) cls = 0; else if (curp && curp->egid == node->gid) cls = 1;
        uint32_t rbit = (cls==0?S_IRUSR:(cls==1?S_IRGRP:S_IROTH));
        if (!(node->mode & rbit)) {
            vfs_debug("Read permission denied");
            return -1;
        }
    #endif

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

    #if VFS_ENFORCE_PERMS
        //check permissions
        process_t* curp2 = process_get_current();
        int cls2 = 2; if (curp2 && curp2->euid == node->uid) cls2 = 0; else if (curp2 && curp2->egid == node->gid) cls2 = 1;
        uint32_t wbit2 = (cls2==0?S_IWUSR:(cls2==1?S_IWGRP:S_IWOTH));
        if (!(node->mode & wbit2)) {
            vfs_debug("Write permission denied");
            return -1;
        }
    #endif

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
        //ensure the filesystem unlink op has a valid parent context
        //some FS implementations access node->parent to derive directory state
        node->parent = parent;
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

int vfs_link(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) return -1;
    //resolve source node
    vfs_node_t* src = vfs_resolve_path(oldpath);
    if (!src) return -1;
    //get parent directory for newpath
    char* parent_path = vfs_get_parent_path(newpath);
    if (!parent_path) { vfs_close(src); return -1; }
    char* basename = vfs_get_basename(newpath);
    if (!basename) { kfree(parent_path); vfs_close(src); return -1; }

    //open parent directory with write permission
    vfs_node_t* parent = vfs_open(parent_path, VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (!parent) {
        kfree(parent_path); kfree(basename); vfs_close(src); return -1;
    }

    int r = -1;
    if (parent->ops && parent->ops->link) {
        r = parent->ops->link(parent, basename, src);
    }

    vfs_close(parent);
    vfs_close(src);
    kfree(parent_path);
    kfree(basename);
    return r;
}
