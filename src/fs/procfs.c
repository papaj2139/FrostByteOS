#include "procfs.h"
#include "vfs.h"
#include "../mm/heap.h"
#include "../libc/string.h"
#include "../mm/pmm.h"
#include "../device_manager.h"
#include "../process.h"
#include "../gui/vga.h"
#include "../drivers/vga_dev.h"
#include "../drivers/timer.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PROCFS_NODE_ROOT = 0,
    PROCFS_NODE_MOUNTS,
    PROCFS_NODE_MEMINFO,
    PROCFS_NODE_DEVICES,
    PROCFS_NODE_KERNEL_CMDLINE,
    PROCFS_NODE_VGA_CTRL,
    PROCFS_NODE_UPTIME,
    PROCFS_NODE_TTY,
    PROCFS_NODE_DIR_SELF,
    PROCFS_NODE_DIR_PID,
    PROCFS_NODE_FILE_STATUS,
    PROCFS_NODE_FILE_CMDLINE,
    PROCFS_NODE_RESCAN,
} procfs_node_kind_t;

typedef struct {
    procfs_node_kind_t kind;
    uint32_t pid; //for self/pid dirs and files
} procfs_priv_t;

static int procfs_open(vfs_node_t* node, uint32_t flags) { 
    (void)node; (void)flags; 
    return 0; 
}

//trim to token lowercase
static void procfs_copy_trim_lower(char* dst, uint32_t dstsz, const char* src, uint32_t srclen) {
    if (!dst || dstsz == 0) return;
    //skip leading spaces
    uint32_t i = 0; while (i < srclen && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r')) i++;
    uint32_t j = 0;
    while (i < srclen && j + 1 < dstsz) {
        char c = src[i++];
        if (c == '\n' || c == '\r') break;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        dst[j++] = c;
    }
    //trim trailing spaces
    while (j > 0 && (dst[j-1] == ' ' || dst[j-1] == '\t')) j--;
    dst[j] = '\0';
}

static int procfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer) {
    (void)offset;
    if (!node || !buffer || size == 0) return -1;
    procfs_priv_t* p = (procfs_priv_t*)node->private_data;
    if (!p) return -1;
    if (p->kind == PROCFS_NODE_VGA_CTRL) {
        char cmd[16];
        procfs_copy_trim_lower(cmd, sizeof(cmd), buffer, size);
        if (cmd[0] == '\0') return 0;
        if (strcmp(cmd, "13h") == 0) {
            vga_set_mode(VGA_MODE_13H);
        } else if (strcmp(cmd, "12h") == 0) {
            vga_set_mode(VGA_MODE_12H);
        } else if (strcmp(cmd, "text") == 0 || strcmp(cmd, "03h") == 0) {
            vga_set_mode(VGA_MODE_TEXT);
        } else {
            return -1;
        }
        //ensure /dev/vga0 exists and inform device of mode change
        if (!device_find_by_name("vga0")) {
            (void)vga_device_register();
        }
        vga_device_on_mode_changed();
        return (int)size;
    }
    if (p->kind == PROCFS_NODE_TTY) {
        char cmd[32];
        procfs_copy_trim_lower(cmd, sizeof(cmd), buffer, size);
        if (cmd[0] == '\0') return 0;
        //allow 'tty0' or 'serial0'
        device_t* dev = device_find_by_name(cmd);
        if (!dev) return -1;
        process_t* cur = process_get_current();
        if (!cur) return -1;
        cur->tty = dev;
        return (int)size;
    }
    if (p->kind == PROCFS_NODE_RESCAN) {
        //ATA rescan
        extern void ata_rescan_partitions(void);
        ata_rescan_partitions();
        return (int)size;
    }
    return -1;
}

static int procfs_close(vfs_node_t* node) {
    if (node && node->private_data) { 
        kfree(node->private_data); 
        node->private_data = NULL; 
    }
    return 0;
}

static vfs_node_t* procfs_make_node(const char* name, uint32_t type, procfs_node_kind_t kind, uint32_t pid, vfs_node_t* parent) {
    uint32_t flags = VFS_FLAG_READ;
    if (kind == PROCFS_NODE_VGA_CTRL || kind == PROCFS_NODE_RESCAN) flags = VFS_FLAG_WRITE; //control is write-only
    if (kind == PROCFS_NODE_TTY) flags = VFS_FLAG_READ | VFS_FLAG_WRITE; //read current write to switch
    vfs_node_t* n = vfs_create_node(name, type, flags);
    if (!n) return NULL;
    n->ops = &procfs_ops;
    n->mount = parent ? parent->mount : NULL;
    n->parent = parent;
    procfs_priv_t* p = (procfs_priv_t*)kmalloc(sizeof(procfs_priv_t));
    if (!p) { vfs_destroy_node(n); return NULL; }
    p->kind = kind; p->pid = pid;
    n->private_data = p;
    return n;
}

//kernel cmdline storage (exposed at /proc/cmdline)
static char g_kernel_cmdline[512] = {0};
void procfs_set_cmdline(const char* cmdline) {
    if (!cmdline) { 
        g_kernel_cmdline[0] = '\0'; 
        return; 
    }
    size_t n = strlen(cmdline);
    if (n >= sizeof(g_kernel_cmdline)) n = sizeof(g_kernel_cmdline) - 1;
    memcpy(g_kernel_cmdline, cmdline, n);
    g_kernel_cmdline[n] = '\0';
}

static int procfs_readdir_root(uint32_t index, vfs_node_t* node, vfs_node_t** out) {
    //static entries first
    if (index == 0) { 
        *out = procfs_make_node("mounts", VFS_FILE_TYPE_FILE, PROCFS_NODE_MOUNTS, 0, node); 
        return *out ? 0 : -1; 
    }
    if (index == 1) { 
        *out = procfs_make_node("meminfo", VFS_FILE_TYPE_FILE, PROCFS_NODE_MEMINFO, 0, node); 
        return *out ? 0 : -1; 
    }
    if (index == 2) { 
        *out = procfs_make_node("devices", VFS_FILE_TYPE_FILE, PROCFS_NODE_DEVICES, 0, node); 
        return *out ? 0 : -1; 
    }
    if (index == 3) { 
        *out = procfs_make_node("cmdline", VFS_FILE_TYPE_FILE, PROCFS_NODE_KERNEL_CMDLINE, 0, node); 
        return *out ? 0 : -1; 
    }
    if (index == 4) { 
        *out = procfs_make_node("vga", VFS_FILE_TYPE_FILE, PROCFS_NODE_VGA_CTRL, 0, node); 
        return *out ? 0 : -1; 
    }
    if (index == 5) { 
        *out = procfs_make_node("uptime", VFS_FILE_TYPE_FILE, PROCFS_NODE_UPTIME, 0, node); 
        return *out ? 0 : -1; 
    }
    if (index == 6) { 
        *out = procfs_make_node("tty", VFS_FILE_TYPE_FILE, PROCFS_NODE_TTY, 0, node); 
        return *out ? 0 : -1; 
    }
    if (index == 7) { 
        *out = procfs_make_node("rescan", VFS_FILE_TYPE_FILE, PROCFS_NODE_RESCAN, 0, node); 
        return *out ? 0 : -1; 
    }
    if (index == 8) { 
        *out = procfs_make_node("self", VFS_FILE_TYPE_DIRECTORY, PROCFS_NODE_DIR_SELF, 0, node); 
        return *out ? 0 : -1; 
    }
    //process directories start at index 9
    uint32_t which = index - 9;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t* pr = &process_table[i];
        if (pr->state != PROC_UNUSED) {
            if (seen == which) {
                char namebuf[16];
                ksnprintf(namebuf, sizeof(namebuf), "%u", pr->pid);
                *out = procfs_make_node(namebuf, VFS_FILE_TYPE_DIRECTORY, PROCFS_NODE_DIR_PID, pr->pid, node);
                return *out ? 0 : -1;
            }
            seen++;
        }
    }
    return -1;
}

static int procfs_readdir_pid_dir(uint32_t index, vfs_node_t* node, vfs_node_t** out) {
    procfs_priv_t* p = (procfs_priv_t*)node->private_data;
    if (!p) return -1;
    if (index == 0) { 
        *out = procfs_make_node("status", VFS_FILE_TYPE_FILE, PROCFS_NODE_FILE_STATUS, p->pid, node); 
        return *out ? 0 : -1; 
    }
    if (index == 1) { 
        *out = procfs_make_node("cmdline", VFS_FILE_TYPE_FILE, PROCFS_NODE_FILE_CMDLINE, p->pid, node); 
        return *out ? 0 : -1; 
    }
    return -1;
}

static int procfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t** out) {
    if (!node || !out) return -1;
    procfs_priv_t* p = (procfs_priv_t*)node->private_data;
    if (!p) {
        //mount root gets no private_data treat as ROOT
        return procfs_readdir_root(index, node, out);
    }
    switch (p->kind) {
        case PROCFS_NODE_ROOT: return procfs_readdir_root(index, node, out);
        case PROCFS_NODE_DIR_SELF: {
            process_t* cur = process_get_current();
            uint32_t pid = cur ? cur->pid : 0;
            if (index == 0) { 
                *out = procfs_make_node("status", VFS_FILE_TYPE_FILE, PROCFS_NODE_FILE_STATUS, pid, node); 
                return *out ? 0 : -1; 
            }
            if (index == 1) { 
                *out = procfs_make_node("cmdline", VFS_FILE_TYPE_FILE, PROCFS_NODE_FILE_CMDLINE, pid, node); 
                return *out ? 0 : -1; 
            }
            return -1;
        }
        case PROCFS_NODE_DIR_PID: return procfs_readdir_pid_dir(index, node, out);
        default: return -1;
    }
}

static int procfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out) {
    if (!node || !name || !out) return -1;
    procfs_priv_t* p = (procfs_priv_t*)node->private_data;
    if (!p) {
        //treat as root
        if (strcmp(name, "mounts") == 0) { 
            *out = procfs_make_node("mounts", VFS_FILE_TYPE_FILE, PROCFS_NODE_MOUNTS, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "meminfo") == 0) { 
            *out = procfs_make_node("meminfo", VFS_FILE_TYPE_FILE, PROCFS_NODE_MEMINFO, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "devices") == 0) { 
            *out = procfs_make_node("devices", VFS_FILE_TYPE_FILE, PROCFS_NODE_DEVICES, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "cmdline") == 0) { 
            *out = procfs_make_node("cmdline", VFS_FILE_TYPE_FILE, PROCFS_NODE_KERNEL_CMDLINE, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "vga") == 0) { 
            *out = procfs_make_node("vga", VFS_FILE_TYPE_FILE, PROCFS_NODE_VGA_CTRL, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "uptime") == 0) { 
            *out = procfs_make_node("uptime", VFS_FILE_TYPE_FILE, PROCFS_NODE_UPTIME, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "tty") == 0) { 
            *out = procfs_make_node("tty", VFS_FILE_TYPE_FILE, PROCFS_NODE_TTY, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "rescan") == 0) { 
            *out = procfs_make_node("rescan", VFS_FILE_TYPE_FILE, PROCFS_NODE_RESCAN, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "self") == 0) { 
            *out = procfs_make_node("self", VFS_FILE_TYPE_DIRECTORY, PROCFS_NODE_DIR_SELF, 0, node); 
            return *out ? 0 : -1; 
        }
        uint32_t pid = 0; bool all_digits = true; for (const char* s = name; *s; s++) { if (*s < '0' || *s > '9') { all_digits = false; break; } pid = pid * 10 + (uint32_t)(*s - '0'); }
        if (all_digits) {
            process_t* pr = process_get_by_pid(pid);
            if (pr && pr->state != PROC_UNUSED) {
                *out = procfs_make_node(name, VFS_FILE_TYPE_DIRECTORY, PROCFS_NODE_DIR_PID, pid, node);
                return *out ? 0 : -1;
            }
        }
        return -1;
    }
    if (p->kind == PROCFS_NODE_ROOT) {
        if (strcmp(name, "mounts") == 0) { 
            *out = procfs_make_node("mounts", VFS_FILE_TYPE_FILE, PROCFS_NODE_MOUNTS, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "meminfo") == 0) { 
            *out = procfs_make_node("meminfo", VFS_FILE_TYPE_FILE, PROCFS_NODE_MEMINFO, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "devices") == 0) { 
            *out = procfs_make_node("devices", VFS_FILE_TYPE_FILE, PROCFS_NODE_DEVICES, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "cmdline") == 0) { 
            *out = procfs_make_node("cmdline", VFS_FILE_TYPE_FILE, PROCFS_NODE_KERNEL_CMDLINE, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "vga") == 0) { 
            *out = procfs_make_node("vga", VFS_FILE_TYPE_FILE, PROCFS_NODE_VGA_CTRL, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "uptime") == 0) { 
            *out = procfs_make_node("uptime", VFS_FILE_TYPE_FILE, PROCFS_NODE_UPTIME, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "tty") == 0) { 
            *out = procfs_make_node("tty", VFS_FILE_TYPE_FILE, PROCFS_NODE_TTY, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "rescan") == 0) { 
            *out = procfs_make_node("rescan", VFS_FILE_TYPE_FILE, PROCFS_NODE_RESCAN, 0, node); 
            return *out ? 0 : -1; 
        }
        if (strcmp(name, "self") == 0) { 
            *out = procfs_make_node("self", VFS_FILE_TYPE_DIRECTORY, PROCFS_NODE_DIR_SELF, 0, node); 
            return *out ? 0 : -1; 
        }
        //numeric pid?
        uint32_t pid = 0; bool all_digits = true; for (const char* s = name; *s; s++) { if (*s < '0' || *s > '9') { all_digits = false; break; } pid = pid * 10 + (uint32_t)(*s - '0'); }
        if (all_digits) {
            process_t* pr = process_get_by_pid(pid);
            if (pr && pr->state != PROC_UNUSED) {
                *out = procfs_make_node(name, VFS_FILE_TYPE_DIRECTORY, PROCFS_NODE_DIR_PID, pid, node);
                return *out ? 0 : -1;
            }
        }
        return -1;
    } else if (p->kind == PROCFS_NODE_DIR_SELF || p->kind == PROCFS_NODE_DIR_PID) {
        uint32_t pid = (p->kind == PROCFS_NODE_DIR_SELF) ? (process_get_current() ? process_get_current()->pid : 0) : p->pid;
        if (strcmp(name, "status") == 0) { 
            *out = procfs_make_node("status", VFS_FILE_TYPE_FILE, PROCFS_NODE_FILE_STATUS, pid, node); 
            return *out ? 0 : -1;
        }
        if (strcmp(name, "cmdline") == 0) { 
            *out = procfs_make_node("cmdline", VFS_FILE_TYPE_FILE, PROCFS_NODE_FILE_CMDLINE, pid, node);
            return *out ? 0 : -1; 
        }
        return -1;
    }
    return -1;
}

static int procfs_get_size(vfs_node_t* node) { 
    (void)node; 
    return 0; 
}

static const char* proc_state_str(proc_state_t s) {
    switch (s) {
        case PROC_UNUSED: return "UNUSED";
        case PROC_EMBRYO: return "EMBRYO";
        case PROC_RUNNABLE: return "RUNNABLE";
        case PROC_RUNNING: return "RUNNING";
        case PROC_SLEEPING: return "SLEEPING";
        case PROC_ZOMBIE: return "ZOMBIE";
        default: return "?";
    }
}

static int procfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer) {
    if (!node || !buffer) return -1;
    procfs_priv_t* p = (procfs_priv_t*)node->private_data;
    if (!p) return -1;

    char tmp[1024];
    tmp[0] = '\0';
    size_t len = 0;
    switch (p->kind) {
        case PROCFS_NODE_MOUNTS: {
            const vfs_mount_t* m = vfs_get_mounts();
            while (m) {
                const char* devname = (m->mount_device ? m->mount_device->name : "none");
                size_t used = ksnprintf(tmp + len, sizeof(tmp) - len, "%s %s %s\n", m->mount_point, m->fs_name[0] ? m->fs_name : "(unknown)", devname);
                len += used;
                if (len >= sizeof(tmp)) break;
                m = m->next;
            }
            break;
        }
        case PROCFS_NODE_KERNEL_CMDLINE: {
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "%s\n", g_kernel_cmdline);
            break;
        }
        case PROCFS_NODE_MEMINFO: {
            uint32_t total = pmm_get_total_pages();
            uint32_t freep = pmm_get_free_pages();
            uint32_t used = pmm_get_used_pages();
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "MemTotal: %u pages\n", total);
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "MemFree:  %u pages\n", freep);
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "MemUsed:  %u pages\n", used);
            break;
        }
        case PROCFS_NODE_DEVICES: {
            uint32_t idx = 0;
            for (;;) {
                device_t* d = NULL;
                if (device_enumerate(idx, &d) != 0 || !d) break;
                const char* t = "unknown";
                switch (d->type) {
                    case DEVICE_TYPE_STORAGE: t = "storage"; break;
                    case DEVICE_TYPE_INPUT: t = "input"; break;
                    case DEVICE_TYPE_OUTPUT: t = "output"; break;
                    case DEVICE_TYPE_NETWORK: t = "network"; break;
                    case DEVICE_TYPE_TIMER: t = "timer"; break;
                    default: break;
                }
                len += ksnprintf(tmp + len, sizeof(tmp) - len, "%s %s\n", d->name, t);
                if (len >= sizeof(tmp)) break;
                idx++;
            }
            break;
        }
        case PROCFS_NODE_FILE_STATUS: {
            uint32_t pid = p->pid;
            process_t* pr = process_get_by_pid(pid);
            const char* name = (pr && pr->name[0]) ? pr->name : "(unknown)";
            const char* st = pr ? proc_state_str(pr->state) : "(none)";
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "Name:\t%s\nPid:\t%u\nState:\t%s\n", name, pid, st);
            break;
        }
        case PROCFS_NODE_FILE_CMDLINE: {
            uint32_t pid = p->pid;
            process_t* pr = process_get_by_pid(pid);
            const char* cmd = "";
            if (pr) {
                if (pr->cmdline[0]) cmd = pr->cmdline;
                else if (pr->name[0]) cmd = pr->name;
            }
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "%s\n", cmd);
            break;
        }
        case PROCFS_NODE_UPTIME: {
            uint32_t hz = timer_get_frequency();
            uint32_t t = (uint32_t)timer_get_ticks(); //truncate to 32-bit to avoid 64-bit div helpers
            uint32_t secs = hz ? (t / hz) : t;
            uint32_t rem  = hz ? (t % hz) : 0;
            uint32_t hsec = hz ? ((rem * 100u) / hz) : 0;
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "%u.%02u\n", secs, hsec);
            break;
        }
        case PROCFS_NODE_TTY: {
            process_t* cur = process_get_current();
            const char* name = (cur && cur->tty) ? cur->tty->name : "(none)";
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "%s\n", name);            
            break;
        }
        default: return -1;
    }

    if (offset >= len) return 0;
    uint32_t to_copy = (uint32_t)((len - offset) < size ? (len - offset) : size);
    memcpy(buffer, tmp + offset, to_copy);
    return (int)to_copy;
}

static int procfs_ioctl(vfs_node_t* node, uint32_t request, void* arg) { 
    (void)node; 
    (void)request; 
    (void)arg; 
    return -1; 
}

vfs_operations_t procfs_ops = {
    .open = procfs_open,
    .close = procfs_close,
    .read = procfs_read,
    .write = procfs_write,
    .create = NULL,
    .unlink = NULL,
    .mkdir = NULL,
    .rmdir = NULL,
    .readdir = procfs_readdir,
    .finddir = procfs_finddir,
    .get_size = procfs_get_size,
    .ioctl = procfs_ioctl,
};
