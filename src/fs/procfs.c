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
#include "../kernel/kshutdown.h"
#include "../kernel/kreboot.h"
#include "../interrupts/irq.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../drivers/sb16.h"

typedef enum {
    PROCFS_NODE_ROOT = 0,
    PROCFS_NODE_MOUNTS,
    PROCFS_NODE_MEMINFO,
    PROCFS_NODE_DEVICES,
    PROCFS_NODE_KERNEL_CMDLINE,
    PROCFS_NODE_VGA_CTRL,
    PROCFS_NODE_UPTIME,
    PROCFS_NODE_TTY,
    PROCFS_NODE_POWER,
    PROCFS_NODE_DIR_SELF,
    PROCFS_NODE_DIR_PID,
    PROCFS_NODE_FILE_STATUS,
    PROCFS_NODE_FILE_CMDLINE,
    PROCFS_NODE_RESCAN,
    PROCFS_NODE_CPUINFO,
    PROCFS_NODE_VERSION,
    PROCFS_NODE_FILESYSTEMS,
    PROCFS_NODE_INTERRUPTS,
    PROCFS_NODE_PARTITIONS,
    PROCFS_NODE_SB16,
} procfs_node_kind_t;

typedef struct {
    procfs_node_kind_t kind;
    uint32_t pid; //for self/pid dirs and files
} procfs_priv_t;

//root directory static entries table (order defines readdir order)
typedef struct {
    const char* name;
    procfs_node_kind_t kind;
    uint32_t type; //VFS_FILE_TYPE_*
} procfs_root_entry_t;

static const procfs_root_entry_t g_procfs_root_entries[] = {
    { "mounts",  PROCFS_NODE_MOUNTS,          VFS_FILE_TYPE_FILE },
    { "meminfo", PROCFS_NODE_MEMINFO,         VFS_FILE_TYPE_FILE },
    { "devices", PROCFS_NODE_DEVICES,         VFS_FILE_TYPE_FILE },
    { "filesystems", PROCFS_NODE_FILESYSTEMS, VFS_FILE_TYPE_FILE },
    { "cpuinfo", PROCFS_NODE_CPUINFO,         VFS_FILE_TYPE_FILE },
    { "version", PROCFS_NODE_VERSION,         VFS_FILE_TYPE_FILE },
    { "interrupts", PROCFS_NODE_INTERRUPTS,   VFS_FILE_TYPE_FILE },
    { "cmdline", PROCFS_NODE_KERNEL_CMDLINE,  VFS_FILE_TYPE_FILE },
    { "vga",     PROCFS_NODE_VGA_CTRL,        VFS_FILE_TYPE_FILE },
    { "uptime",  PROCFS_NODE_UPTIME,          VFS_FILE_TYPE_FILE },
    { "tty",     PROCFS_NODE_TTY,             VFS_FILE_TYPE_FILE },
    { "power",   PROCFS_NODE_POWER,           VFS_FILE_TYPE_FILE },
    { "rescan",  PROCFS_NODE_RESCAN,          VFS_FILE_TYPE_FILE },
    { "partitions", PROCFS_NODE_PARTITIONS,   VFS_FILE_TYPE_FILE },
    { "sb16",    PROCFS_NODE_SB16,            VFS_FILE_TYPE_FILE },
    { "self",    PROCFS_NODE_DIR_SELF,        VFS_FILE_TYPE_DIRECTORY },
};

static const uint32_t g_procfs_root_count = (uint32_t)(sizeof(g_procfs_root_entries) / sizeof(g_procfs_root_entries[0]));

static int procfs_open(vfs_node_t* node, uint32_t flags) {
    (void)node; (void)flags;
    return 0;
}

//cpuid helpe returns eax, ebx, ecx, edx for the given leaf/subleaf
static void cpuid_ex(uint32_t leaf, uint32_t subleaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    uint32_t _a=0,_b=0,_c=0,_d=0;
    __asm__ volatile ("cpuid" : "=a"(_a), "=b"(_b), "=c"(_c), "=d"(_d) : "a"(leaf), "c"(subleaf));
    if (a) *a=_a; if (b) *b=_b; if (c) *c=_c; if (d) *d=_d;
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
    if (p->kind == PROCFS_NODE_POWER) {
        char cmd[32];
        procfs_copy_trim_lower(cmd, sizeof(cmd), buffer, size);
        if (cmd[0] == '\0') return 0;
        if (strcmp(cmd, "poweroff") == 0 || strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "off") == 0) {
            kshutdown(); //does not return
        } else if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "reset") == 0) {
            kreboot();   //does not return
        } else if (strcmp(cmd, "halt") == 0) {
            __asm__ volatile ("cli");
            for(;;) {
                __asm__ volatile ("hlt");
            }
        } else {
            return -1;
        }
        return (int)size; //not reached for poweroff/reboot/halt
    }
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
    if (p->kind == PROCFS_NODE_SB16) {
        //accept commands: "rate <hz>" "speaker on" "speaker off"
        //note: it parses up to the first whitespace-delimited token
        char line[64];
        if (size >= sizeof(line)) size = sizeof(line) - 1;
        memcpy(line, buffer, size);
        line[size] = '\0';
        //lowercase copy and trim
        for (uint32_t i = 0; i < size; i++) {
            if (line[i] >= 'A' && line[i] <= 'Z') line[i] = (char)(line[i] - 'A' + 'a');
        }
        //tokenize
        char* s = line;
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
        if (*s == '\0') return 0;
        //find first token
        char* t0 = s;
        while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') s++;
        char c0 = *s; *s = '\0';
        s++;
        if (strcmp(t0, "rate") == 0) {
            //parse integer
            while (*s == ' ' || *s == '\t') s++;
            int val = 0;
            while (*s >= '0' && *s <= '9') {
                val = val * 10 + (*s - '0');
                s++;
            }
            if (val <= 0) return -1;
            return sb16_set_rate((uint16_t)val) == 0 ? (int)size : -1;
        } else if (strcmp(t0, "speaker") == 0) {
            while (*s == ' ' || *s == '\t') s++;
            char* t1 = s;
            while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') s++;
            char save = *s; *s = '\0';
            if (strcmp(t1, "on") == 0) {
                sb16_speaker_on();
                return (int)size;
            }
            if (strcmp(t1, "off") == 0) {
                sb16_speaker_off();
                return (int)size;
            }
            (void)save;
            return -1;
        } else {
            return -1;
        }
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
    if (kind == PROCFS_NODE_POWER) flags = VFS_FLAG_READ | VFS_FLAG_WRITE; //read capabilities write to control
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
    //enumerate static entries first using the table
    if (index < g_procfs_root_count) {
        const procfs_root_entry_t* e = &g_procfs_root_entries[index];
        *out = procfs_make_node(e->name, e->type, e->kind, 0, node);
        return *out ? 0 : -1;
    }
    //then enumerate numeric PID directories
    uint32_t which = index - g_procfs_root_count;
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

//match a root entry name via the static table
static int procfs_finddir_root_helper(vfs_node_t* node, const char* name, vfs_node_t** out) {
    for (uint32_t i = 0; i < g_procfs_root_count; i++) {
        const procfs_root_entry_t* e = &g_procfs_root_entries[i];
        if (strcmp(name, e->name) == 0) {
            *out = procfs_make_node(e->name, e->type, e->kind, 0, node);
            return *out ? 0 : -1;
        }
    }
    return -1;
}

static int procfs_finddir(vfs_node_t* node, const char* name, vfs_node_t** out) {
    if (!node || !name || !out) return -1;
    procfs_priv_t* p = (procfs_priv_t*)node->private_data;
    if (!p) {
        //treat as root using table
        if (procfs_finddir_root_helper(node, name, out) == 0) return 0;
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
        //treat as root using table
        if (procfs_finddir_root_helper(node, name, out) == 0) return 0;
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
        case PROCFS_NODE_FILESYSTEMS: {
            char names[16][32]; uint32_t cnt = 0;
            if (vfs_list_fs_types(names, 16, &cnt) == 0) {
                for (uint32_t i = 0; i < cnt; i++) {
                    len += ksnprintf(tmp + len, sizeof(tmp) - len, "%s\n", names[i]);
                    if (len >= sizeof(tmp)) break;
                }
            }
            break;
        }
        case PROCFS_NODE_PARTITIONS: {
            //list block devices and partitions (ATA) with sizes similar to /proc/partitions
            //header: major minor blocks name
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "major minor blocks name\n");
            uint32_t idx = 0;
            uint32_t minor = 0;
            for (;;) {
                device_t* dev = NULL;
                if (device_enumerate(idx++, &dev) != 0 || !dev) break;
                if (dev->type != DEVICE_TYPE_STORAGE) continue;
                //query ATA device info via helper
                extern int ata_query_device_info(device_t* dev, uint64_t* start_lba, uint64_t* sectors, int* is_part);
                uint64_t start = 0, secs = 0; int is_part = 0;
                if (ata_query_device_info && ata_query_device_info(dev, &start, &secs, &is_part) == 0) {
                    //compute 1K blocks
                    uint64_t blocks = secs / 2ull;
                    //fake major=8 minor increments for display purposes
                    uint32_t major = 8u;
                    len += ksnprintf(tmp + len, sizeof(tmp) - len, "%4u %5u %7u %s\n", major, minor++, (uint32_t)blocks, dev->name);
                    if (len >= sizeof(tmp)) break;
                }
            }
            break;
        }
        case PROCFS_NODE_CPUINFO: {
            //basic vendor and brand
            uint32_t a=0,b=0,c=0,d=0;
            cpuid_ex(0, 0, &a, &b, &c, &d);
            char vendor[13];
            ((uint32_t*)vendor)[0] = b; //"GenuineIntel" or "AuthenticAMD"
            ((uint32_t*)vendor)[1] = d;
            ((uint32_t*)vendor)[2] = c;
            vendor[12] = '\0';
            uint32_t max_basic = a;

            cpuid_ex(1, 0, &a, &b, &c, &d);
            uint32_t stepping = a & 0xF;
            uint32_t model = (a >> 4) & 0xF;
            uint32_t family = (a >> 8) & 0xF;
            uint32_t ext_model = (a >> 16) & 0xF;
            uint32_t ext_family = (a >> 20) & 0xFF;
            uint32_t eff_family = (family == 0xF) ? (family + ext_family) : family;
            uint32_t eff_model = (family == 0x6 || family == 0xF) ? ((ext_model << 4) | model) : model;

            char brand[49]; brand[0]='\0';
            cpuid_ex(0x80000000u, 0, &a, &b, &c, &d);
            if (a >= 0x80000004u) {
                uint32_t* bp = (uint32_t*)brand;
                cpuid_ex(0x80000002u, 0, &bp[0], &bp[1], &bp[2], &bp[3]);
                cpuid_ex(0x80000003u, 0, &bp[4], &bp[5], &bp[6], &bp[7]);
                cpuid_ex(0x80000004u, 0, &bp[8], &bp[9], &bp[10], &bp[11]);
                brand[48] = '\0';
            }
            //format
            if (brand[0]) {
                len += ksnprintf(tmp + len, sizeof(tmp) - len, "model name\t: %s\n", brand);
            }
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "vendor_id\t: %s\n", vendor);
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "cpu family\t: %u\n", eff_family);
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "model\t\t: %u\n", eff_model);
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "stepping\t: %u\n", stepping);
            break;
        }
        case PROCFS_NODE_VERSION: {
            len += ksnprintf(tmp + len, sizeof(tmp) - len, "FrostByteOS version 0.0.5 (%s %s)\n", __DATE__, __TIME__);
            break;
        }
        case PROCFS_NODE_INTERRUPTS: {
            uint32_t counts[16];
            irq_get_all_counts(counts);
            for (int i = 0; i < 16; i++) {
                len += ksnprintf(tmp + len, sizeof(tmp) - len, "irq%02d: %u\n", i, counts[i]);
                if (len >= sizeof(tmp)) break;
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
            uint32_t ppid = pr ? pr->ppid : 0;
            const char* ttyn = (pr && pr->tty) ? pr->tty->name : "(none)";
            const char* cwd = (pr && pr->cwd[0]) ? pr->cwd : "/";
            uint32_t prio = pr ? pr->priority : 0;
            const char* ik = (pr && pr->in_kernel) ? "yes" : "no";
            uint32_t ueip = pr ? pr->context.eip : 0;
            uint32_t uesp = pr ? pr->context.esp : 0;
            len += ksnprintf(tmp + len, sizeof(tmp) - len,
                             "Name:\t%s\nPid:\t%u\nPPid:\t%u\nState:\t%s\nTTY:\t%s\nCwd:\t%s\nPriority:\t%u\nInKernel:\t%s\nUserEIP:\t0x%x\nUserESP:\t0x%x\n",
                             name, pid, ppid, st, ttyn, cwd, prio, ik, ueip, uesp);
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
        case PROCFS_NODE_POWER: {
            len += ksnprintf(tmp + len, sizeof(tmp) - len,
                             "capabilities: poweroff reboot halt\nstate: on\n");
            break;
        }
        case PROCFS_NODE_SB16: {
            uint16_t rate = sb16_get_rate();
            int spk = sb16_is_speaker_on();
            uint8_t irq = sb16_get_irq();
            uint8_t dma = sb16_get_dma8();
            len += ksnprintf(tmp + len, sizeof(tmp) - len,
                             "rate: %u\nspeaker: %s\nirq: %u\ndma8: %u\n",
                             (unsigned)rate, spk ? "on" : "off", (unsigned)irq, (unsigned)dma);
            break;
        }
        default: return -1;
    }

    //ensure we never present a zero-length file to userland unless offset beyond content
    if (len == 0) {
        len += ksnprintf(tmp + len, sizeof(tmp) - len, "(empty)\n");
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
