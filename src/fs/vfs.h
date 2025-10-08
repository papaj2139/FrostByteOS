#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include "../device_manager.h"
#include "../process.h"

//file types
#define VFS_FILE_TYPE_FILE      0x01
#define VFS_FILE_TYPE_DIRECTORY 0x02
#define VFS_FILE_TYPE_DEVICE    0x04
#define VFS_FILE_TYPE_SYMLINK   0x08

//VFS node flags
#define VFS_FLAG_READ    0x01
#define VFS_FLAG_WRITE   0x02
#define VFS_FLAG_EXECUTE 0x04

//maximum path length
#define VFS_MAX_PATH 256

//forward declarations
typedef struct vfs_node vfs_node_t;
typedef struct vfs_operations vfs_operations_t;
typedef struct vfs_mount vfs_mount_t;

//VFS operations structure
struct vfs_operations {
    int (*open)(vfs_node_t* node, uint32_t flags);
    int (*close)(vfs_node_t* node);
    int (*read)(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer);
    int (*write)(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer);
    int (*create)(vfs_node_t* parent, const char* name, uint32_t flags);
    int (*unlink)(vfs_node_t* node);
    int (*mkdir)(vfs_node_t* parent, const char* name, uint32_t flags);
    int (*rmdir)(vfs_node_t* node);
    int (*readdir)(vfs_node_t* node, uint32_t index, vfs_node_t** out);
    int (*finddir)(vfs_node_t* node, const char* name, vfs_node_t** out);
    int (*get_size)(vfs_node_t* node);
    int (*ioctl)(vfs_node_t* node, uint32_t request, void* arg);
    int (*readlink)(vfs_node_t* node, char* buf, uint32_t bufsize);
    int (*symlink)(vfs_node_t* parent, const char* name, const char* target);
    int (*link)(vfs_node_t* parent, const char* name, vfs_node_t* src); //hard link
    int (*poll_can_read)(vfs_node_t* node);
    int (*poll_can_write)(vfs_node_t* node);
};

//permission mode bits (subset of POSIX)
#ifndef S_IRUSR
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020    
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#endif

//VFS node structure
struct vfs_node {
    char name[64];              //node name
    uint32_t type;              //file type
    uint32_t flags;             //legacy VFS flags
    uint32_t size;              //file size in bytes
    uint32_t inode;             //inode number
    vfs_operations_t* ops;      //operations for this node
    void* device;               //device-specific data
    void* private_data;         //filesystem-specific data
    uint32_t ref_count;         //reference count
    vfs_mount_t* mount;         //mount point this node belongs to
    vfs_node_t* parent;         //parent directory
    uint32_t uid;               //owner user id
    uint32_t gid;               //owner group id
    uint32_t mode;              //permission bits (S_IRUSR..)
};

//VFS mount structure
struct vfs_mount {
    char mount_point[VFS_MAX_PATH];  //mount point path
    vfs_node_t* root;                //root node of mounted filesystem
    device_t* mount_device;          //physical device this filesystem is on (NULL for virtual FS)
    void* private_data;              //filesystem-specific mount data
    char fs_name[32];                //filesystem type name
    vfs_mount_t* next;               //next mount in the list
};

//file descriptor structure
typedef struct {
    vfs_node_t* node;           //VFS node
    uint32_t offset;            //current position in file
    uint32_t flags;             //access flags
    uint32_t ref_count;         //reference count
    uint32_t append;            //append mode flag (O_APPEND)
} vfs_file_t;

//function declarations
int vfs_init(void);
int vfs_mount(const char* device, const char* mount_point, const char* fs_type);
int vfs_unmount(const char* mount_point);
vfs_node_t* vfs_open(const char* path, uint32_t flags);
int vfs_close(vfs_node_t* node);
int vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer);
int vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer);
int vfs_create(const char* path, uint32_t flags);
int vfs_unlink(const char* path);
int vfs_mkdir(const char* path, uint32_t flags);
int vfs_rmdir(const char* path);
int vfs_symlink(const char* target, const char* linkpath);
int vfs_readlink(const char* path, char* buf, uint32_t bufsize);
int vfs_link(const char* oldpath, const char* newpath);
int vfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t** out);
int vfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out);
int vfs_get_size(vfs_node_t* node);
vfs_node_t* vfs_resolve_path(const char* path);
//resolve without following the final symlink
vfs_node_t* vfs_resolve_path_nofollow(const char* path);
int vfs_register_fs(const char* name, vfs_operations_t* ops);
vfs_node_t* vfs_create_node(const char* name, uint32_t type, uint32_t flags);
void vfs_destroy_node(vfs_node_t* node);

//metadata overlay API for filesystems without native POSIX metadata
//set any of mode/uid/gid for a given absolute path pass has_* to indicate which to set
int vfs_set_metadata_override(const char* abspath, int has_mode, uint32_t mode,
                              int has_uid, uint32_t uid,
                              int has_gid, uint32_t gid);
//apply overlay (if present) to a node resolved from 'abspath'
void vfs_apply_metadata_override(vfs_node_t* node, const char* abspath);

//set the root filesystem handlers directly (used by initramfs)
int vfs_set_root_ops(vfs_operations_t* ops, void* private_data);

//enumerate current mounts (read-only) returns head of internal list
const vfs_mount_t* vfs_get_mounts(void);

//enumerate registered filesystem type names (read-only snapshot)
//fills up to 'max' entries of 32-byte strings in 'names' and returns 0 on success
int vfs_list_fs_types(char names[][32], uint32_t max, uint32_t* out_count);

//path manipulation functions
char* vfs_get_parent_path(const char* path);
char* vfs_get_basename(const char* path);
int vfs_path_compare(const char* path1, const char* path2);
//normalize 'path' against base (if path is relative) resolving '.' and '..' into out
int vfs_normalize_path(const char* base, const char* path, char* out, size_t outsz);

//root node
extern vfs_node_t* vfs_root;

#endif
