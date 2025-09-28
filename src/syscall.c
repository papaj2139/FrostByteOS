#include "syscall.h"
#include "fs/vfs.h"
#include "fd.h"
#include "io.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "interrupts/idt.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "drivers/tty.h"
#include "process.h"
#include "drivers/timer.h"
#include "drivers/rtc.h"
#include "device_manager.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "debug.h"
#include "kernel/elf.h"
#include "kernel/cga.h"
#include "kernel/panic.h"
#include "kernel/signal.h"
#include "kernel/uaccess.h"
#include "kernel/dynlink.h"

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_ANON    0x1
#define MAP_FIXED   0x10
#define MMAP_SCAN_START 0x04000000u   //avoid low 8MB identity region
#define MMAP_SCAN_END   0x7F000000u   //keep under 2GiB to avoid sign issues
#define USER_HEAP_BASE 0x03000000u

#ifndef S_IFMT
#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFLNK  0120000
#define S_IFCHR  0020000
#endif

typedef struct {
    uint32_t st_mode;  // type + perms
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_size;
} stat32_t;

static const int mdays_norm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

//timekeeping base captured at first use
static uint64_t g_boot_epoch = 0;     //seconds since epoch at boot
static uint64_t g_boot_ticks = 0;     //timer ticks at the moment of boot capture
static uint32_t g_hz_cached = 0;      //timer frequency (ticks per second)

//32-bit user ABI for timespec/timeval structures
typedef struct {
    uint32_t tv_sec;
    uint32_t tv_nsec;
} timespec32_t;

typedef struct {
    uint32_t tv_sec;
    uint32_t tv_usec;
} timeval32_t;

//external assembly handler
extern void syscall_handler_asm(void);

//divide 64-bit unsigned by 32-bit unsigned return quotient store remainder
static uint64_t udivmod_u64_u32(uint64_t n, uint32_t d, uint32_t* rem)
{
    //simple binary long division
    uint64_t q = 0;
    uint64_t r = 0;
    for (int i = 63; i >= 0; --i) {
        r = (r << 1) | ((n >> i) & 1ull);
        if (r >= d) { r -= d; q |= (1ull << i); }
    }
    if (rem) *rem = (uint32_t)r;
    return q;
}

//compute UNIX epoch seconds from RTC time (naive and UTC assumption)
static int is_leap(unsigned y) {
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

//forward declaration
static int normalize_user_path(const char* in, char* out, size_t outsz);

int32_t sys_chdir(const char* path) {
    if (!path) return -1;
    char abspath[VFS_MAX_PATH];
    if (normalize_user_path(path, abspath, sizeof(abspath)) != 0) return -1;
    vfs_node_t* node = vfs_resolve_path(abspath);
    if (!node) return -1;
    int ok = (node->type == VFS_FILE_TYPE_DIRECTORY);
    vfs_close(node);
    if (!ok) return -1;
    process_t* cur = process_get_current();
    if (!cur) return -1;
    strncpy(cur->cwd, abspath, sizeof(cur->cwd) - 1);
    cur->cwd[sizeof(cur->cwd) - 1] = '\0';
    return 0;
}

int32_t sys_getcwd(char* buf, uint32_t bufsize) {
    if (!buf || bufsize == 0) return -1;
    process_t* cur = process_get_current();
    if (!cur || !cur->cwd[0]) {
        if (bufsize < 2) return -1;
        buf[0] = '/'; buf[1] = '\0';
        return 0;
    }
    size_t len = strlen(cur->cwd);
    if (len + 1 > bufsize) return -1;
    memcpy(buf, cur->cwd, len + 1);
    return 0;
}

static uint64_t rtc_to_epoch_seconds(void) {
    rtc_time_t t;
    if (!rtc_read(&t)) return 0;
    unsigned y = t.year;
    unsigned m = t.month;
    unsigned d = t.day;
    unsigned hh = t.hour;
    unsigned mm = t.minute;
    unsigned ss = t.second;
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    uint64_t days = 0;
    for (unsigned yr = 1970; yr < y; ++yr) days += is_leap(yr) ? 366 : 365;
    for (unsigned i = 1; i < m; ++i) {
        days += mdays_norm[i-1];
        if (i == 2 && is_leap(y)) days += 1;
    }
    days += (d - 1);
    uint64_t secs = days * 86400ull + hh * 3600ull + mm * 60ull + ss;
    return secs;
}

static void ensure_time_base(void) {
    if (g_hz_cached == 0) g_hz_cached = timer_get_frequency();
    if (g_boot_epoch == 0) {
        uint64_t now = rtc_to_epoch_seconds();
        if (now == 0) now = 1735689600ull; //fallback 2025-01-01 UTC
        g_boot_epoch = now;
        g_boot_ticks = timer_get_ticks();
    }
}

//mark entry/exit of syscalls for scheduler to restore correct context
void syscall_mark_enter(void) {
    process_t* cur = process_get_current();
    if (cur) cur->in_kernel = true;
}

//find a free virtual region of 'length' bytes in the current process directory
static uint32_t mmap_find_free_region(page_directory_t dir, uint32_t length, uint32_t hint_start) {
    (void)dir; //vmm_get_physical_addr uses the currently active directory
    if (length == 0) return 0;
    uint32_t start = hint_start ? hint_start : MMAP_SCAN_START;
    if (start < USER_VIRTUAL_START) start = USER_VIRTUAL_START;
    if (start & (PAGE_SIZE - 1)) start = (start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t end_limit = MMAP_SCAN_END;
    if (end_limit > USER_VIRTUAL_END) end_limit = USER_VIRTUAL_END;
    //simple first-fit scan
    for (uint32_t base = start; base + length <= end_limit; base += PAGE_SIZE) {
        bool ok = true;
        for (uint32_t off = 0; off < length; off += PAGE_SIZE) {
            if (vmm_get_physical_addr(base + off) != 0) {
                ok = false;
                break;
            }
        }
        if (ok) return base;
    }
    return 0;
}

void syscall_mark_exit(void) {
    process_t* cur = process_get_current();
    if (cur) cur->in_kernel = false;
}

//normalize a user-provided path against the current process CWD into an absolute path
static int normalize_user_path(const char* in, char* out, size_t outsz) {
    if (!in || !out || outsz == 0) return -1;
    process_t* cur = process_get_current();
    const char* base = (cur && cur->cwd[0]) ? cur->cwd : "/";
    return vfs_normalize_path(base, in, out, outsz);
}

//initialize syscall system
void syscall_init(void) {
    //install syscall handler at interrupt 0x80
    //use DPL=3 trap gate (0xEF) so IF remains enabled inside syscalls
    idt_set_gate(SYSCALL_INT, (uint32_t)syscall_handler_asm, 0x08, 0xEF);
    fd_init();
}

//capture user-mode return frame and GPRs at syscall entry so fork() can clone precisely
void syscall_capture_user_frame(uint32_t eip, uint32_t cs, uint32_t eflags,
                                uint32_t useresp, uint32_t ss, uint32_t ebp,
                                uint32_t eax, uint32_t ebx, uint32_t ecx,
                                uint32_t edx, uint32_t esi, uint32_t edi) {
    process_t* cur = process_get_current();
    if (!cur) return;
    cur->context.eip = eip;
    cur->context.cs = cs;
    cur->context.eflags = eflags;
    cur->context.esp = useresp;
    cur->context.ss = ss;
    cur->context.ebp = ebp;
    //save user GPRs as they were at syscall entry
    cur->context.eax = eax;
    cur->context.ebx = ebx;
    cur->context.ecx = ecx;
    cur->context.edx = edx;
    cur->context.esi = esi;
    cur->context.edi = edi;
}

//clone user space from src->dst directories (user part only)
static int clone_user_space(page_directory_t src, page_directory_t dst) {
    if (!src || !dst) return -1;
    const uint32_t TMP_SRC = 0xE0000000; //high kernel scratch outside kernel heap
    const uint32_t TMP_DST = 0xE0001000;
    for (int i = 0; i < 768; i++) { //user space only
        //skip cloning PDE 0 and 1 (0 to 8MB identity region) these PTs are shared with the kernel
        //so cloning into them would overwrite global identity mappings (e.x VGA 0xB8000)
        //causing text output to disappear
        if (i < 2) continue;
        if (!(src[i] & PAGE_PRESENT)) continue;
        uint32_t pt_src_phys = src[i] & ~0xFFF;
        page_table_t pt_src = (page_table_t)PHYSICAL_TO_VIRTUAL(pt_src_phys);
        for (int j = 0; j < 1024; j++) {
            uint32_t pte = pt_src[j];
            if (!(pte & PAGE_PRESENT)) continue;
            uint32_t src_phys = pte & ~0xFFF;
            uint32_t flags = PAGE_PRESENT | (pte & PAGE_WRITABLE) | PAGE_USER; //ensure USER
            uint32_t vaddr = ((uint32_t)i << 22) | ((uint32_t)j << 12);
            //allocate and copy
            uint32_t dst_phys = pmm_alloc_page();
            if (!dst_phys) return -1;
            if (vmm_map_page(TMP_SRC, src_phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) return -1;
            if (vmm_map_page(TMP_DST, dst_phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                vmm_unmap_page_nofree(TMP_SRC);
                return -1;
            }
            memcpy((void*)TMP_DST, (void*)TMP_SRC, 4096);
            vmm_unmap_page_nofree(TMP_SRC);
            vmm_unmap_page_nofree(TMP_DST);
            //map into dst directory at vaddr
            if (vmm_map_page_in_directory(dst, vaddr, dst_phys, flags) != 0) return -1;
        }
    }
    return 0;
}

//main syscall dispatcher called from assembly
int32_t syscall_dispatch(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg4; //suppress unused parameter warning
    (void)arg5;
    switch (syscall_num) {
        case SYS_EXIT:
            return sys_exit((int32_t)arg1);
        case SYS_WRITE:
            return sys_write((int32_t)arg1, (const char*)arg2, arg3);
        case SYS_READ:
            return sys_read((int32_t)arg1, (char*)arg2, arg3);
        case SYS_OPEN:
            return sys_open((const char*)arg1, (int32_t)arg2);
        case SYS_CLOSE:
            return sys_close((int32_t)arg1);
        case SYS_CREAT:
            return sys_creat((const char*)arg1, (int32_t)arg2);
        case SYS_GETPID:
            return sys_getpid();
        case SYS_SLEEP:
            return sys_sleep(arg1);
        case SYS_FORK:
            return sys_fork();
        case SYS_EXECVE: {
            const char* p = (const char*)arg1;
            char* const* avp = (char* const*)arg2;
            char* const* evp = (char* const*)arg3;
            #if LOG_EXEC
            serial_write_string("[SYS_EXECVE] path ptr=0x");
            serial_printf("%x", (uint32_t)p);
            serial_write_string(" path=\"");
            if (p) serial_write_string(p); else serial_write_string("(null)");
            serial_write_string("\"\n");
            serial_write_string("[SYS_EXECVE] argv ptr=0x");
            serial_printf("%x", (uint32_t)avp);
            serial_write_string(" envp ptr=0x");
            serial_printf("%x", (uint32_t)evp);
            serial_write_string("\n");
            if (avp) {
                serial_write_string("[SYS_EXECVE] argv0 ptr=0x");
                serial_printf("%x", (uint32_t)avp[0]);
                serial_write_string("\n");
                if (avp[0]) {
                    serial_write_string("[SYS_EXECVE] argv0=\"");
                    serial_write_string(avp[0]);
                    serial_write_string("\"\n");
                }
            }
            #endif
            return sys_execve(p, avp, evp);
        }
        case SYS_WAIT:
            return sys_wait((int32_t*)arg1);
        case SYS_WAITPID:
            return sys_waitpid((int32_t)arg1, (int32_t*)arg2, (int32_t)arg3);
        case SYS_YIELD:
            return sys_yield();
        case SYS_IOCTL:
            return sys_ioctl((int32_t)arg1, arg2, (void*)arg3);
        case SYS_BRK:
            return sys_brk(arg1);
        case SYS_SBRK:
            return sys_sbrk((int32_t)arg1);
        case SYS_UNLINK:
            return sys_unlink((const char*)arg1);
        case SYS_MKDIR:
            return sys_mkdir((const char*)arg1, (int32_t)arg2);
        case SYS_RMDIR:
            return sys_rmdir((const char*)arg1);
        case SYS_MOUNT:
            return sys_mount((const char*)arg1, (const char*)arg2, (const char*)arg3);
        case SYS_UMOUNT:
            return sys_umount((const char*)arg1);
        case SYS_READDIR_FD:
            return sys_readdir_fd((int32_t)arg1, arg2, (char*)arg3, arg4, (uint32_t*)arg5);
        case SYS_MMAP:
            return sys_mmap(arg1, arg2, arg3, arg4);
        case SYS_MUNMAP:
            return sys_munmap(arg1, arg2);
        case SYS_TIME:
            return sys_time();
        case SYS_CLOCK_GETTIME:
            return sys_clock_gettime(arg1, (void*)arg2);
        case SYS_GETTIMEOFDAY:
            return sys_gettimeofday((void*)arg1, (void*)arg2);
        case SYS_NANOSLEEP:
            return sys_nanosleep((const void*)arg1, (void*)arg2);
        case SYS_LINK:
            return sys_link((const char*)arg1, (const char*)arg2);
        case SYS_KILL:
            return sys_kill(arg1, arg2);
        case SYS_SYMLINK:
            return sys_symlink((const char*)arg1, (const char*)arg2);
        case SYS_READLINK:
            return sys_readlink((const char*)arg1, (char*)arg2, arg3);
        case SYS_CHDIR:
            return sys_chdir((const char*)arg1);
        case SYS_GETCWD:
            return sys_getcwd((char*)arg1, arg2);
        case SYS_DL_GET_INIT:
            return sys_dl_get_init(arg1);
        case SYS_DL_GET_FINI:
            return sys_dl_get_fini(arg1);
        case SYS_DLOPEN:
            return sys_dlopen((const char*)arg1, arg2);
        case SYS_DLCLOSE:
            return sys_dlclose((int32_t)arg1);
        case SYS_DLSYM:
            return sys_dlsym((int32_t)arg1, (const char*)arg2);
        case SYS_GETUID:
            return sys_getuid();
        case SYS_GETEUID:
            return sys_geteuid();
        case SYS_GETGID:
            return sys_getgid();
        case SYS_GETEGID:
            return sys_getegid();
        case SYS_UMASK:
            return sys_umask((int32_t)arg1);
        case SYS_STAT:
            return sys_stat((const char*)arg1, (void*)arg2);
        case SYS_LSTAT:
            return sys_lstat((const char*)arg1, (void*)arg2);
        case SYS_FSTAT:
            return sys_fstat((int32_t)arg1, (void*)arg2);
        case SYS_CHMOD:
            return sys_chmod((const char*)arg1, (int32_t)arg2);
        case SYS_CHOWN:
            return sys_chown((const char*)arg1, (int32_t)arg2, (int32_t)arg3);
        case SYS_FCHMOD:
            return sys_fchmod((int32_t)arg1, (int32_t)arg2);
        case SYS_FCHOWN:
            return sys_fchown((int32_t)arg1, (int32_t)arg2, (int32_t)arg3);
        default:
            print("Unknown syscall\n", 0x0F);
            return -1; //ENOSYS = Function not implemented
    }
}

//syscall implementations
int32_t sys_exit(int32_t status) {
    #if LOG_PROC
    serial_write_string("[EXIT] sys_exit called with status=\n");
    serial_printf("%d", status);
    serial_write_string("\n");
    #endif
    process_exit(status);
    return 0; //Never reached
}

int32_t sys_write(int32_t fd, const char* buf, uint32_t count) {
    #if LOG_SYSCALL
    serial_write_string("[SYSCALL] Write called - fd: ");
    serial_printf("%d", fd);
    serial_write_string(", count: ");
    serial_printf("%d", count);
    serial_write_string(", buf=0x");
    serial_printf("%x", (uint32_t)buf);
    serial_write_string("\n");
    #endif
    //validate user buffer
    if (!buf || count == 0) return 0;
    if (!user_range_ok(buf, count, 0)) return -1;

    if (fd == 1 || fd == 2) {
        //route stdout/stderr to controlling TTY device
        process_t* curp = process_get_current();
        device_t* dev = (curp) ? curp->tty : NULL;
        int32_t rc;
        if (dev) {
            int wr = device_write(dev, 0, buf, count);
            rc = (wr < 0) ? wr : (int32_t)count;
        } else {
            int written = tty_write(buf, count);
            rc = (written < 0) ? written : (int32_t)count;
        }
        //deliver any pending signals for the current process
        signal_check_current();
        return rc;
    }

    vfs_file_t* file = fd_get(fd);
    if (!file) {
        #if LOG_SYSCALL
        serial_write_string("[SYSCALL] Invalid file descriptor\n");
        #endif
        return -1; //EBADF
    }

    #if LOG_SYSCALL
    serial_write_string("[SYSCALL] Writing to file via VFS\n");
    #endif
    //bounce buffer from user to kernel for filesystem/device writes
    int bytes_written = -1;
    char* kbuf = (char*)kmalloc(count);
    if (kbuf) {
        if (copy_from_user(kbuf, buf, count) == 0) {
            int r = vfs_write(file->node, file->offset, count, kbuf);
            if (r >= 0) {
                file->offset += r;
                bytes_written = r;
            } else {
                bytes_written = r;
            }
        }
        kfree(kbuf);
    }
    #if LOG_SYSCALL
    serial_write_string("[SYSCALL] Write completed, bytes: ");
    serial_printf("%d", bytes_written);
    serial_write_string("\n");
    #endif
    //check signals
    signal_check_current();
    return bytes_written;
}

int32_t sys_read(int32_t fd, char* buf, uint32_t count) {
    if (!buf || count == 0) return 0;
    if (!user_range_ok(buf, count, 1)) return -1;
    if (fd == 0) {
        //read from controlling TTY using the current process TTY mode
        process_t* cur = process_get_current();
        uint32_t mode = (cur) ? cur->tty_mode : (TTY_MODE_CANON | TTY_MODE_ECHO);
        device_t* dev = (cur) ? cur->tty : NULL;
        if (!dev || strcmp(dev->name, "tty0") == 0) {
            //text console keyboard path
            int r = tty_read_mode(buf, count, mode);
            //ff ctrl-c interrupted input r may be 0 don't terminate the shell in that case
            if (r > 0) {
                signal_check_current();
            }
            return r;
        } else {
            //serial or other device: implement a line/raw reader via device manager
            uint32_t pos = 0;
            char ch;
            if (mode & TTY_MODE_CANON) {
                for (;;) {
                    int r;
                    //block for first byte
                    do {
                        r = device_read(dev, 0, &ch, 1);
                    } while (r <= 0);
                    if (ch == '\r') ch = '\n';
                    buf[pos++] = ch;
                    if (mode & TTY_MODE_ECHO) device_write(dev, 0, &ch, 1);
                    if (ch == '\n' || pos >= count) return (int32_t)pos;
                    //drain immediately available without blocking too long
                    while (pos < count) {
                        char t;
                        int rr = device_read(dev, 0, &t, 1);
                        if (rr <= 0) break;
                        if (t == '\r') t = '\n';
                        buf[pos++] = t;
                        if (mode & TTY_MODE_ECHO) device_write(dev, 0, &t, 1);
                        if (t == '\n') return (int32_t)pos;
                    }
                }
            } else {
                //raw mode: block for first byte then return immediately if no more
                int r;
                do {
                    r = device_read(dev, 0, &ch, 1);
                } while (r <= 0);
                if (ch == '\r') ch = '\n';
                buf[pos++] = ch;
                if (mode & TTY_MODE_ECHO) device_write(dev, 0, &ch, 1);
                while (pos < count) {
                    char t;
                    int rr = device_read(dev, 0, &t, 1);
                    if (rr <= 0) break;
                    if (t == '\r') t = '\n';
                    buf[pos++] = t;
                    if (mode & TTY_MODE_ECHO) device_write(dev, 0, &t, 1);
                }
                signal_check_current();
                return (int32_t)pos;
            }
        }
    }

    vfs_file_t* file = fd_get(fd);
    if (!file) {
        return -1; //EBADF
    }

    //bounce buffer in kernel space then copy to user
    int bytes_read = -1;
    char* kbuf = (char*)kmalloc(count);
    if (kbuf) {
        int r = vfs_read(file->node, file->offset, count, kbuf);
        if (r > 0) {
            if (copy_to_user(buf, kbuf, (size_t)r) == 0) {
                file->offset += r;
                bytes_read = r;
            } else {
                bytes_read = -1;
            }
        } else {
            bytes_read = r;
        }
        kfree(kbuf);
    }
    signal_check_current();
    return bytes_read;
}

int32_t sys_open(const char* pathname, int32_t flags) {
    //convert POSIX flags to VFS flags
    uint32_t vfs_flags = 0;
    if (flags == 0) {
        vfs_flags = VFS_FLAG_READ;  //O_RDONLY
    } else if (flags == 1) {
        vfs_flags = VFS_FLAG_WRITE; //O_WRONLY
    } else if (flags == 2) {
        vfs_flags = VFS_FLAG_READ | VFS_FLAG_WRITE; //O_RDWR
    } else {
        vfs_flags = VFS_FLAG_READ; //default to read-only
    }
    char abspath[VFS_MAX_PATH];
    if (normalize_user_path(pathname, abspath, sizeof(abspath)) != 0) return -1;
    vfs_node_t* node = vfs_open(abspath, vfs_flags);
    if (!node) {
        return -1;
    }
    //install into current process descriptor table
    return fd_alloc(node, vfs_flags);
}

int32_t sys_close(int32_t fd) {
    fd_close(fd);
    return 0;
}

int32_t sys_creat(const char* pathname, int32_t mode) {
    char abspath[VFS_MAX_PATH];
    if (normalize_user_path(pathname, abspath, sizeof(abspath)) != 0) return -1;
    process_t* cur = process_get_current();
    if (!cur) return -1;
    //apply umask default file mode 0666 if mode==0
    uint32_t req = (mode == 0) ? 0666u : (uint32_t)mode;
    uint32_t eff_mode = req & ~cur->umask;

    //create the file (filesystem-specific)
    if (vfs_create(abspath, 0) != 0) {
        return -1;
    }

    //persist initial ownership and permissions via overlay so subsequent resolves see them
    vfs_set_metadata_override(abspath, 1, (eff_mode & 07777), 1, cur->euid, 1, cur->egid);

    //open parent directory directly then locate child and open it to avoid resolution race/case issues
    char* parent_path = vfs_get_parent_path(abspath);
    if (!parent_path) {
        return -1;
    }
    char* base = vfs_get_basename(abspath);
    if (!base) { kfree(parent_path); return -1; }

    vfs_node_t* parent = vfs_open(parent_path, VFS_FLAG_READ | VFS_FLAG_WRITE);
    if (!parent) {
        kfree(parent_path);
        kfree(base);
        return -1;
    }

    vfs_node_t* child = NULL;
    int fd = -1;
    if (parent->ops && parent->ops->finddir) {
        if (parent->ops->finddir(parent, base, &child) == 0 && child) {
            //best-effort set attributes on node instance as well
            child->mode = (eff_mode & 07777);
            child->uid = cur->euid;
            child->gid = cur->egid;
            //ensure filesystem-specific open occurs for write
            if (child->ops && child->ops->open) {
                if (child->ops->open(child, VFS_FLAG_WRITE) != 0) {
                    vfs_close(child);
                    child = NULL;
                }
            }
            if (child) {
                fd = fd_alloc(child, 1); //O_WRONLY
                if (fd < 0) {
                    vfs_close(child);
                }
            }
        }
    }

    vfs_close(parent);
    kfree(parent_path);
    kfree(base);
    return fd;
}

int32_t sys_getpid(void) {
    process_t* current = process_get_current();
    return current ? current->pid : 0;
}

int32_t sys_sleep(uint32_t seconds) {
    uint32_t ticks = seconds * 100;
    #if LOG_PROC
    uint64_t now = timer_get_ticks();
    serial_write_string("[SLEEP] seconds=\n");
    serial_printf("%d", (int)seconds);
    serial_write_string(" ticks=\n");
    serial_printf("%d", (int)ticks);
    serial_write_string(" now=\n");
    serial_printf("%d", (int)(uint32_t)now);
    serial_write_string(" wake_at=\n");
    serial_printf("%d", (int)(uint32_t)(now + ticks));
    serial_write_string("\n");
    #endif
    process_sleep(ticks); //convert to ticks
    #if LOG_PROC
    serial_write_string("[SLEEP] woke up\n");
    #endif
    return 0;
}

int32_t sys_fork(void) {
    process_t* parent = process_get_current();
    if (!parent) return -1;
    #if LOG_PROC
    serial_write_string("[FORK] enter\n");
    #endif

    //disable interrupts during fork to avoid reentrancy
    uint32_t eflags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags) :: "memory");
    #if LOG_PROC
    serial_write_string("[FORK] post-cli\n");
    #endif

    #if LOG_PROC
    serial_write_string("[FORK] pre-create\n");
    #endif

    //create child as USER-MODE process
    process_t* child = process_create(parent->name, (void*)parent->context.eip, true);
    if (!child) {
        #if LOG_PROC
        serial_write_string("[FORK] process_create failed\n");
        #endif
        //restore IF if it was set
        if (eflags & 0x200) __asm__ volatile ("sti");
        return -1;
    }
    #if LOG_PROC
    serial_write_string("[FORK] created\n");
    #endif

    //clone user address space now that per-process CR3 is enabled
    if (clone_user_space(parent->page_directory, child->page_directory) != 0) {
        #if LOG_PROC
        serial_write_string("[FORK] clone_user_space failed\n");
        #endif
        process_destroy(child);
        if (eflags & 0x200) __asm__ volatile ("sti");
        return -1;
    }

    //inherit minimal context so child returns to the same user EIP with ESP preserved
    //and EAX=0 in the child per POSIX semantics
    child->context.eip = parent->context.eip;      //return point in user space
    child->context.esp = parent->context.esp;      //user stack pointer at syscall entry
    child->context.ebp = parent->context.ebp;      //preserve proper frame pointer for local vars
    child->context.cs  = 0x1B;                     //user code segment
    child->context.ss  = 0x23;                     //user stack segment
    child->context.ds = child->context.es = child->context.fs = child->context.gs = 0x23;
    child->context.eflags = parent->context.eflags; //preserve IF and flags
    //inherit parent general-purpose registers so callee-saved regs remain valid across fork
    child->context.eax = 0;                        //fork returns 0 in child
    child->context.ebx = parent->context.ebx;
    child->context.ecx = parent->context.ecx;
    child->context.edx = parent->context.edx;
    child->context.esi = parent->context.esi;
    child->context.edi = parent->context.edi;

    //inherit TTY mode and controlling TTY
    child->tty = parent->tty;
    child->tty_mode = parent->tty_mode;

    //inherit file descriptors (per-process) and bump open-file refcounts
    fd_copy_on_fork(parent, child);

    //inherit cmdline for /proc/<pid>/cmdline until execve updates it
    child->cmdline[0] = '\0';
    if (parent->cmdline[0]) {
        strncpy(child->cmdline, parent->cmdline, sizeof(child->cmdline) - 1);
        child->cmdline[sizeof(child->cmdline) - 1] = '\0';
    }

    //ensure parent saved user context reflects fork return value as well
    parent->context.eax = (uint32_t)child->pid;

    //restore IF if it was set
    if (eflags & 0x200) __asm__ volatile ("sti");
    #if LOG_PROC
    serial_write_string("[FORK] return\n");
    #endif

    //return child's PID in parent
    return (int32_t)child->pid;
}

int32_t sys_execve(const char* pathname, char* const argv[], char* const envp[]) {
    if (!pathname) return -1;
    //normalize path against CWD
    char abspath[VFS_MAX_PATH];
    if (normalize_user_path(pathname, abspath, sizeof(abspath)) != 0) return -1;

    int er = elf_execve(abspath, argv, envp);
    if (er == 0) return 0;   //not returned
    //any failure (including not an ELF) equals error
    return -1;
}

static inline uint32_t page_align_up(uint32_t addr) {
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

int32_t sys_brk(uint32_t new_end) {
    process_t* cur = process_get_current();
    if (!cur || !cur->page_directory) return -1;

    //initialize heap base on first use
    if (cur->heap_start == 0 && cur->heap_end == 0) {
        cur->heap_start = USER_HEAP_BASE;
        cur->heap_end = USER_HEAP_BASE;
    }

    uint32_t old_end = cur->heap_end;
    if (new_end == 0) {
        //glibc sometimes calls brk(0) to query break return current end as success (0) is ambiguous
        //we follow linux-like semantics so return 0 on success user queries via sbrk(0)
        return 0;
    }

    //prevent setting break below start
    if (new_end < cur->heap_start) new_end = cur->heap_start;

    uint32_t old_top = page_align_up(old_end);
    uint32_t new_top = page_align_up(new_end);

    //switch to this process directory for unmap operations
    vmm_switch_directory(cur->page_directory);

    if (new_top > old_top) {
        //grow: map new pages
        for (uint32_t va = old_top; va < new_top; va += PAGE_SIZE) {
            uint32_t phys = pmm_alloc_page();
            if (!phys) return -1;
            if (vmm_map_page_in_directory(cur->page_directory, va, phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
                return -1;
            }
            //zero page via temp map
            const uint32_t TMP = 0x00800000;
            if (vmm_map_page(TMP, phys, PAGE_PRESENT | PAGE_WRITABLE) == 0) {
                memset((void*)TMP, 0, PAGE_SIZE);
                vmm_unmap_page_nofree(TMP);
            }
        }
    } else if (new_top < old_top) {
        //shrink: unmap pages beyond new_top (vmm_unmap_page frees the frame)
        for (uint32_t va = new_top; va < old_top; va += PAGE_SIZE) {
            if (vmm_get_physical_addr(va)) {
                vmm_unmap_page(va);
            }
        }
    }

    cur->heap_end = new_end;
    return 0;
}

int32_t sys_sbrk(int32_t increment) {
    process_t* cur = process_get_current();
    if (!cur || !cur->page_directory) return -1;
    if (cur->heap_start == 0 && cur->heap_end == 0) {
        cur->heap_start = USER_HEAP_BASE;
        cur->heap_end = USER_HEAP_BASE;
    }
    uint32_t old_end = cur->heap_end;
    uint32_t new_end = (increment >= 0) ? (cur->heap_end + (uint32_t)increment)
                                        : (cur->heap_end - (uint32_t)(-increment));
    if (sys_brk(new_end) != 0) return -1;
    return (int32_t)old_end;
}

int32_t sys_wait(int32_t* status) {
    process_t* parent = process_get_current();
    if (!parent) return -1;

    for (;;) {
        //scan children for a zombie
        process_t* child = parent->children;
        while (child) {
            if (child->state == PROC_ZOMBIE) {
                int pid = (int)child->pid;
                int ec = child->exit_code;
                if (status) {
                    //store raw exit code
                    *status = ec;
                }
                #if LOG_PROC
                serial_write_string("[WAIT] returning pid=\n");
                serial_printf("%d", pid);
                serial_write_string(" status=\n");
                serial_printf("%d", ec);
                serial_write_string("\n");
                #endif
                process_destroy(child);
                return pid;
            }
            child = child->sibling;
        }
        //no zombies if no children at all return -1 immediately
        if (!parent->children) return -1;
        //sleep until a child exits
        #if LOG_PROC
        serial_write_string("[WAIT] sleeping parent pid=\n");
        serial_printf("%d", (int)parent->pid);
        serial_write_string("\n");
        #endif
        parent->state = PROC_SLEEPING;
        schedule();
    }
}

int32_t sys_yield(void) {
    process_yield();
    return 0;
}

int32_t sys_ioctl(int32_t fd, uint32_t cmd, void* arg) {
    //route standard I/O to per-process TTY settings
    if (fd == 0 || fd == 1 || fd == 2) {
        process_t* cur = process_get_current();
        if (!cur) return -1;
        if (cmd == TTY_IOCTL_SET_MODE) {
            if (!arg) return -1;
            if (!user_range_ok(arg, sizeof(uint32_t), 0)) return -1;
            cur->tty_mode = *(uint32_t*)arg;
            return 0;
        } else if (cmd == TTY_IOCTL_GET_MODE) {
            if (!arg) return -1;
            if (!user_range_ok(arg, sizeof(uint32_t), 1)) return -1;
            *(uint32_t*)arg = cur->tty_mode;
            return 0;
        }
        //unknown TTY ioctl ignore for now
        return -1;
    }

    vfs_file_t* file = fd_get(fd);
    if (!file || !file->node || !file->node->ops || !file->node->ops->ioctl) {
        return -1;
    }
    return file->node->ops->ioctl(file->node, cmd, arg);
}

int32_t sys_unlink(const char* path) {
    if (!path) return -1;
    char abspath[VFS_MAX_PATH];
    if (normalize_user_path(path, abspath, sizeof(abspath)) != 0) return -1;
    return vfs_unlink(abspath);
}

int32_t sys_mkdir(const char* path, int32_t mode) {
    if (!path) return -1;
    char abspath[VFS_MAX_PATH];
    if (normalize_user_path(path, abspath, sizeof(abspath)) != 0) return -1;
    process_t* cur = process_get_current();
    if (!cur) return -1;
    uint32_t req = (mode == 0) ? 0777u : (uint32_t)mode;
    uint32_t eff_mode = req & ~cur->umask;
    int r = vfs_mkdir(abspath, 0);
    if (r != 0) return -1;
    vfs_node_t* n = vfs_resolve_path(abspath);
    if (n) {
        n->mode = (eff_mode & 07777);
        n->uid = cur->euid;
        n->gid = cur->egid;
        vfs_close(n);
    }
    return 0;
}

int32_t sys_rmdir(const char* path) {
    if (!path) return -1;
    char abspath[VFS_MAX_PATH];
    if (normalize_user_path(path, abspath, sizeof(abspath)) != 0) return -1;
    return vfs_rmdir(abspath);
}

int32_t sys_readdir_fd(int32_t fd, uint32_t index, char* name_buf, uint32_t buf_size, uint32_t* out_type) {
    vfs_file_t* file = fd_get(fd);
    if (!file || !file->node) return -1;
    if (name_buf && buf_size > 0 && !user_range_ok(name_buf, buf_size, 1)) return -1;
    if (out_type && !user_range_ok(out_type, sizeof(uint32_t), 1)) return -1;
    vfs_node_t* node = file->node;
    if (node->type != VFS_FILE_TYPE_DIRECTORY) return -1;
    vfs_node_t* child = NULL;
    int r = vfs_readdir(node, index, &child);
    if (r != 0 || !child) return -1;
    //copy name and type out
    if (name_buf && buf_size > 0) {
        size_t nlen = strlen(child->name);
        if (nlen + 1 > buf_size) nlen = buf_size - 1;
        memcpy(name_buf, child->name, nlen);
        name_buf[nlen] = '\0';
    }
    if (out_type) *out_type = child->type;
    vfs_close(child);
    return 0;
}

int32_t sys_mount(const char* device, const char* mount_point, const char* fs_type) {
    if (!device || !mount_point || !fs_type) return -1;
    char mp[VFS_MAX_PATH];
    if (normalize_user_path(mount_point, mp, sizeof(mp)) != 0) return -1;
    int r = vfs_mount(device, mp, fs_type);
    return (r == 0) ? 0 : -1;
}

int32_t sys_umount(const char* mount_point) {
    if (!mount_point) return -1;
    char mp[VFS_MAX_PATH];
    if (normalize_user_path(mount_point, mp, sizeof(mp)) != 0) return -1;
    int r = vfs_unmount(mp);
    return (r == 0) ? 0 : -1;
}

int32_t sys_mmap(uint32_t addr, uint32_t length, uint32_t prot, uint32_t flags) {
    #if LOG_SYSCALL
        serial_write_string("[MMAP] req addr=0x"); serial_printf("%x", addr);
        serial_write_string(" len=0x"); serial_printf("%x", length);
        serial_write_string(" prot=0x"); serial_printf("%x", prot);
        serial_write_string(" flags=0x"); serial_printf("%x", flags);
        serial_write_string("\n");
    #endif
    process_t* cur = process_get_current();
    if (!cur || !cur->page_directory || length == 0) return -1;
    //align length up to page size
    uint32_t len = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    //operate under this process address space
    vmm_switch_directory(cur->page_directory);

    uint32_t start = 0;
    if (flags & MAP_FIXED) {
        //require the specified region to be free
        start = addr & ~(PAGE_SIZE - 1);
        if (start < USER_VIRTUAL_START || start + len > USER_VIRTUAL_END) return -1;
        for (uint32_t off = 0; off < len; off += PAGE_SIZE) {
            if (vmm_get_physical_addr(start + off) != 0) return -1;
        }
    } else {
        uint32_t hint = addr ? (addr & ~(PAGE_SIZE - 1)) : 0;
        start = mmap_find_free_region(cur->page_directory, len, hint);
        if (!start) return -1;
    }

    uint32_t page_flags = PAGE_PRESENT | PAGE_USER | ((prot & PROT_WRITE) ? PAGE_WRITABLE : 0);
    //map and zero each page
    for (uint32_t off = 0; off < len; off += PAGE_SIZE) {
        uint32_t phys = pmm_alloc_page();
        if (!phys) {
            //rollback
            for (uint32_t roff = 0; roff < off; roff += PAGE_SIZE) {
                if (vmm_get_physical_addr(start + roff)) vmm_unmap_page(start + roff);
            }
            return -1;
        }
        if (vmm_map_page_in_directory(cur->page_directory, start + off, phys, page_flags) != 0) {
            //free this phys and rollback
            //no mapping was installed for this page just free phys
            pmm_free_page(phys);
            for (uint32_t roff = 0; roff < off; roff += PAGE_SIZE) {
                if (vmm_get_physical_addr(start + roff)) vmm_unmap_page(start + roff);
            }
            return -1;
        }
        //zero page via temp map
        const uint32_t TMP = 0x00800000;
        if (vmm_map_page(TMP, phys, PAGE_PRESENT | PAGE_WRITABLE) == 0) {
            memset((void*)TMP, 0, PAGE_SIZE);
            vmm_unmap_page_nofree(TMP);
        }
    }
    #if LOG_SYSCALL
        serial_write_string("[MMAP] ok start=0x"); serial_printf("%x", (uint32_t)start);
        serial_write_string(" len=0x"); serial_printf("%x", len);
        serial_write_string("\n");
    #endif
    //return start address as int32
    return (int32_t)start;
}

int32_t sys_munmap(uint32_t addr, uint32_t length) {
    #if LOG_SYSCALL
        serial_write_string("[MUNMAP] addr=0x"); serial_printf("%x", addr);
        serial_write_string(" len=0x"); serial_printf("%x", length);
        serial_write_string("\n");
    #endif
    process_t* cur = process_get_current();
    if (!cur || !cur->page_directory || length == 0) return -1;
    uint32_t start = addr & ~(PAGE_SIZE - 1);
    uint32_t len = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (start < USER_VIRTUAL_START || start + len > USER_VIRTUAL_END) return -1;
    vmm_switch_directory(cur->page_directory);
    for (uint32_t off = 0; off < len; off += PAGE_SIZE) {
        if (vmm_get_physical_addr(start + off)) {
            vmm_unmap_page(start + off);
        }
    }
    return 0;
}

int32_t sys_time(void) {
    ensure_time_base();
    uint64_t ticks = timer_get_ticks() - g_boot_ticks;
    uint32_t hz = (g_hz_cached ? g_hz_cached : timer_get_frequency());
    uint64_t q = 0;
    if (hz) q = udivmod_u64_u32(ticks, hz, NULL);
    uint64_t secs = g_boot_epoch + q;
    return (int32_t)secs;
}

int32_t sys_clock_gettime(uint32_t clock_id, void* ts_out) {
    if (!ts_out) return -1;
    ensure_time_base();
    uint64_t ticks = timer_get_ticks() - g_boot_ticks;
    uint32_t hz = (g_hz_cached ? g_hz_cached : timer_get_frequency());
    uint64_t sec = 0, nsec = 0;
    if (hz == 0) hz = 100; //fallback
    uint32_t rem32 = 0;
    sec = udivmod_u64_u32(ticks, hz, &rem32);
    nsec = udivmod_u64_u32((uint64_t)rem32 * 1000000000u, hz, NULL);
    if (clock_id == 0) { //CLOCK_REALTIME
        sec += g_boot_epoch;
    } else {
        //CLOCK_MONOTONIC or others treated as monotonic
    }
    timespec32_t* ts = (timespec32_t*)ts_out;
    ts->tv_sec = (uint32_t)sec;
    ts->tv_nsec = (uint32_t)nsec;
    return 0;
}

int32_t sys_gettimeofday(void* tv_out, void* tz_ignored) {
    (void)tz_ignored;
    if (!tv_out) return -1;
    ensure_time_base();
    uint64_t ticks = timer_get_ticks() - g_boot_ticks;
    uint32_t hz = (g_hz_cached ? g_hz_cached : timer_get_frequency());
    if (hz == 0) hz = 100;
    uint32_t rem32 = 0;
    uint64_t q = udivmod_u64_u32(ticks, hz, &rem32);
    uint64_t sec = g_boot_epoch + q;
    uint64_t usec = udivmod_u64_u32((uint64_t)rem32 * 1000000u, hz, NULL);
    timeval32_t* tv = (timeval32_t*)tv_out;
    tv->tv_sec = (uint32_t)sec;
    tv->tv_usec = (uint32_t)usec;
    return 0;
}

int32_t sys_nanosleep(const void* req_ts, void* rem_ts) {
    (void)rem_ts; //not supporting remainder yet
    if (!req_ts) return -1;
    const timespec32_t* ts = (const timespec32_t*)req_ts;
    uint64_t nsec = (uint64_t)ts->tv_sec * 1000000000ull + (uint64_t)ts->tv_nsec;
    if (ts->tv_nsec >= 1000000000ull) return -1;
    uint32_t hz = timer_get_frequency();
    if (hz == 0) hz = 100;
    uint32_t ns_rem = 0;
    uint64_t whole = udivmod_u64_u32(nsec, 1000000000u, &ns_rem);
    uint64_t ticks = whole * hz;
    ticks += udivmod_u64_u32((uint64_t)ns_rem * hz, 1000000000u, NULL);
    if (ticks == 0 && nsec > 0) ticks = 1;
    if (ticks > 0xFFFFFFFFu) ticks = 0xFFFFFFFFu;
    process_sleep((uint32_t)ticks);
    return 0;
}

static int read_user_u32(page_directory_t dir, uint32_t va, uint32_t* out) {
    if (!va || !out) return -1;
    page_directory_t saved = vmm_get_kernel_directory();
    vmm_switch_directory(dir);
    uint32_t phys = vmm_get_physical_addr(va & ~0xFFFu) & ~0xFFFu;
    uint32_t off = va & 0xFFFu;
    vmm_switch_directory(saved);
    if (!phys) return -1;
    uint32_t eflags_save; __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_save) :: "memory");
    const uint32_t TMP = 0x00800000u;
    if (vmm_map_page(TMP, phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        if (eflags_save & 0x200) __asm__ volatile ("sti");
        return -1;
    }
    *out = *(uint32_t*)(TMP + off);
    vmm_unmap_page_nofree(TMP);
    if (eflags_save & 0x200) __asm__ volatile ("sti");
    return 0;
}

int32_t sys_dl_get_init(uint32_t index) {
    process_t* cur = process_get_current();
    if (!cur) return 0;
    dynlink_ctx_t* ctx = &cur->dlctx;
    if (ctx->count <= 0) return 0;

    //enumerate in load order: init_array entries, then init function
    for (int i = 0; i < ctx->count; i++) {
        dynobj_t* o = &ctx->objs[i];
        if (!o->ready) continue;
        if (o->init_array && o->init_arraysz) {
            uint32_t entries = o->init_arraysz / 4u;
            if (index < entries) {
                uint32_t fn = 0;
                if (read_user_u32(ctx->dir, o->init_array + index * 4u, &fn) == 0) return (int32_t)fn;
                return 0;
            } else {
                index -= entries;
            }
        }
        if (o->init_addr) {
            if (index == 0) return (int32_t)o->init_addr;
            index--;
        }
    }
    return 0;
}

int32_t sys_dl_get_fini(uint32_t index) {
    process_t* cur = process_get_current();
    if (!cur) return 0;
    dynlink_ctx_t* ctx = &cur->dlctx;
    if (ctx->count <= 0) return 0;

    //enumerate in reverse order: fini_addr first, then fini_array in reverse
    for (int i = ctx->count - 1; i >= 0; i--) {
        dynobj_t* o = &ctx->objs[i];
        if (!o->ready) continue;
        if (o->fini_addr) {
            if (index == 0) return (int32_t)o->fini_addr;
            index--;
        }
        if (o->fini_array && o->fini_arraysz) {
            uint32_t entries = o->fini_arraysz / 4u;
            if (index < entries) {
                uint32_t rev_idx = entries - 1u - index;
                uint32_t fn = 0;
                if (read_user_u32(ctx->dir, o->fini_array + rev_idx * 4u, &fn) == 0) return (int32_t)fn;
                return 0;
            } else {
                index -= entries;
            }
        }
    }
    return 0;
}

static int build_candidate(char* out, uint32_t outsz, const char* dir, const char* name) {
    if (!out || !dir || !name) return -1;
    size_t dl = strlen(dir); if (dl >= outsz) dl = outsz - 1;
    memcpy(out, dir, dl);
    size_t pos = dl;
    if (pos == 0 || out[pos - 1] != '/') {
        if (pos + 1 < outsz) out[pos++] = '/';
    }
    size_t nl = strlen(name);
    if (pos + nl >= outsz) nl = outsz - pos - 1;
    memcpy(out + pos, name, nl);
    out[pos + nl] = '\0';
    return 0;
}

int32_t sys_dlopen(const char* path, uint32_t flags) {
    (void)flags;
    process_t* cur = process_get_current();
    if (!cur) return -1;
    dynlink_ctx_t* ctx = &cur->dlctx;
    if (!ctx->dir) {
        dynlink_ctx_init(ctx, cur->page_directory);
    }
    //dlopen(NULL, ...) returns a special handle for the main program namespace
    if (path == NULL) {
        return -2; //special handle: MAIN
    }
    char name[96];
    if (copy_user_string(path, name, sizeof(name)) != 0) return -1;

    //if it already exists (by SONAME or basename) return existing handle
    int idx_existing = dynlink_find_loaded(ctx, name);
    if (idx_existing >= 0) {
        return idx_existing;
    }

    // If contains '/', try directly
    int loaded = 0;
    dynobj_t* child = NULL;
    if (strchr(name, '/')) {
        if (dynlink_load_shared(ctx, name, &child) == 0 && child) loaded = 1;
    } else {
        //try LD_LIBRARY_PATH list
        if (ctx->ld_library_path[0]) {
            const char* s = ctx->ld_library_path; const char* start = s;
            char cand[128];
            while (!loaded) {
                if (*s == ':' || *s == '\0') {
                    size_t len = (size_t)(s - start);
                    if (len > 0 && len < sizeof(cand) - 2) {
                        char dir[96]; if (len >= sizeof(dir)) len = sizeof(dir) - 1;
                        memcpy(dir, start, len); dir[len] = '\0';
                        build_candidate(cand, sizeof(cand), dir, name);
                        if (dynlink_load_shared(ctx, cand, &child) == 0 && child) { loaded = 1; break; }
                    }
                    if (*s == '\0') break;
                    start = s + 1;
                }
                s++;
            }
        }
        //fallback /lib/<name>
        if (!loaded) {
            char cand[128];
            build_candidate(cand, sizeof(cand), "/lib", name);
            if (dynlink_load_shared(ctx, cand, &child) == 0 && child) loaded = 1;
        }
    }

    if (!loaded || !child) return -1;
    int start_idx = cur->dlctx.count - 1; //'child' is the last loaded object index
    if (start_idx < 0) start_idx = 0;
    (void)dynlink_load_needed(ctx, child);
    //apply relocations only to newly loaded objects (avoid re-relocating existing ones)
    int apply_start = start_idx; //conservative if load_needed added more they are >= start_idx
    if (dynlink_apply_relocations_from(ctx, apply_start) != 0) return -1;
    //handle is index of child
    int handle = (int)(child - &ctx->objs[0]);
    return handle;
}

int32_t sys_dlsym(int32_t handle, const char* name) {
    process_t* cur = process_get_current();
    if (!cur) return 0;
    dynlink_ctx_t* ctx = &cur->dlctx;
    if (!ctx->dir) return 0;
    char sym[96];
    if (copy_user_string(name, sym, sizeof(sym)) != 0) return 0;
    serial_write_string("[DLSYM] ");
    serial_write_string(sym);
    serial_write_string(" handle=");
    serial_printf("%d", handle);
    serial_write_string("\n");

    void* va = 0;
    if (handle >= 0 && handle < ctx->count) {
        va = dynlink_lookup_symbol_in(ctx, handle, sym);
    } else if (handle == -2) {
        //main namespace: search main object only (base==0)
        int main_idx = 0;
        for (int i = 0; i < ctx->count; i++) { if (ctx->objs[i].base == 0) { main_idx = i; break; } }
        va = dynlink_lookup_symbol_in(ctx, main_idx, sym);
    } else {
        //global search across all loaded objects
        va = dynlink_lookup_symbol(ctx, sym);
    }

    serial_write_string("[DLSYM] result=");
    serial_printf("%x", (uint32_t)(uintptr_t)va);
    serial_write_string("\n");
    return (int32_t)(uint32_t)(uintptr_t)va;
}

int32_t sys_dlclose(int32_t handle) {
    (void)handle; //unloading not supported yet
    return 0;
}

int32_t sys_link(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) return -1;
    char src[VFS_MAX_PATH], dst[VFS_MAX_PATH];
    if (normalize_user_path(oldpath, src, sizeof(src)) != 0) return -1;
    if (normalize_user_path(newpath, dst, sizeof(dst)) != 0) return -1;
    int r = vfs_link(src, dst);
    return (r == 0) ? 0 : -1;
}

int32_t sys_kill(uint32_t pid, uint32_t sig) {
    process_t* p = process_get_by_pid(pid);
    if (!p) return -1;
    signal_raise(p, (int)sig);
    process_wake(p);
    return 0;
}

int32_t sys_symlink(const char* target, const char* linkpath) {
    if (!target || !linkpath) return -1;
    char lp[VFS_MAX_PATH];
    if (normalize_user_path(linkpath, lp, sizeof(lp)) != 0) return -1;
    int r = vfs_symlink(target, lp);
    return (r == 0) ? 0 : -1;
}

int32_t sys_readlink(const char* path, char* buf, uint32_t bufsiz) {
    if (!path || !buf || bufsiz == 0) return -1;
    if (!user_range_ok(buf, bufsiz, 1)) return -1;
    char ap[VFS_MAX_PATH];
    if (normalize_user_path(path, ap, sizeof(ap)) != 0) return -1;
    return vfs_readlink(ap, buf, bufsiz);
}

int32_t sys_getuid(void) {
    process_t* cur = process_get_current();
    #if LOG_SYSCALL
    serial_write_string("[SYSCALL] getuid -> ");
    serial_printf("%d", cur ? (int)cur->uid : -1);
    serial_write_string("\n");
    #endif
    return cur ? (int32_t)cur->uid : -1;
}

int32_t sys_geteuid(void) {
    process_t* cur = process_get_current();
    #if LOG_SYSCALL
    serial_write_string("[SYSCALL] geteuid -> ");
    serial_printf("%d", cur ? (int)cur->euid : -1);
    serial_write_string("\n");
    #endif
    return cur ? (int32_t)cur->euid : -1;
}

int32_t sys_getgid(void) {
    process_t* cur = process_get_current();
    return cur ? (int32_t)cur->gid : -1;
}

int32_t sys_getegid(void) {
    process_t* cur = process_get_current();
    return cur ? (int32_t)cur->egid : -1;
}

int32_t sys_umask(int32_t new_mask) {
    process_t* cur = process_get_current();
    if (!cur) return -1;
    int32_t old = (int32_t)cur->umask;
    if (new_mask >= 0) cur->umask = (uint32_t)(new_mask & 0777);
    return old;
}

static uint32_t vfs_type_to_ifmt(uint32_t t)
{
    switch (t) {
        case VFS_FILE_TYPE_DIRECTORY: return S_IFDIR;
        case VFS_FILE_TYPE_SYMLINK:   return S_IFLNK;
        case VFS_FILE_TYPE_DEVICE:    return S_IFCHR;
        case VFS_FILE_TYPE_FILE:
        default: return S_IFREG;
    }
}

static void fill_stat_from_node(vfs_node_t* n, stat32_t* st)
{
    st->st_mode = (n->mode & 07777) | vfs_type_to_ifmt(n->type);
    st->st_uid = n->uid;
    st->st_gid = n->gid;
    st->st_size = n->size;
}

int32_t sys_stat(const char* path, void* stat_out)
{
    if (!path || !stat_out) return -1;
    if (!user_range_ok(stat_out, sizeof(stat32_t), 1)) return -1;
    char ap[VFS_MAX_PATH];
    if (normalize_user_path(path, ap, sizeof(ap)) != 0) return -1;
    vfs_node_t* n = vfs_resolve_path(ap);
    if (!n) return -1;
    stat32_t st; fill_stat_from_node(n, &st);
    memcpy(stat_out, &st, sizeof(st));
    vfs_close(n);
    return 0;
}

int32_t sys_lstat(const char* path, void* stat_out) {
    if (!path || !stat_out) return -1;
    if (!user_range_ok(stat_out, sizeof(stat32_t), 1)) return -1;
    char ap[VFS_MAX_PATH];
    if (normalize_user_path(path, ap, sizeof(ap)) != 0) return -1;
    vfs_node_t* n = vfs_resolve_path_nofollow(ap);
    if (!n) return -1;
    stat32_t st; fill_stat_from_node(n, &st);
    memcpy(stat_out, &st, sizeof(st));
    vfs_close(n);
    return 0;
}

int32_t sys_fstat(int32_t fd, void* stat_out) {
    if (!stat_out) return -1;
    if (!user_range_ok(stat_out, sizeof(stat32_t), 1)) return -1;
    vfs_file_t* f = fd_get(fd);
    if (!f || !f->node) return -1;
    stat32_t st; fill_stat_from_node(f->node, &st);
    memcpy(stat_out, &st, sizeof(st));
    return 0;
}

int32_t sys_chmod(const char* path, int32_t mode) {
    if (!path) return -1;
    char ap[VFS_MAX_PATH];
    if (normalize_user_path(path, ap, sizeof(ap)) != 0) return -1;
    vfs_node_t* n = vfs_resolve_path(ap);
    if (!n) return -1;
    process_t* cur = process_get_current();
    if (!cur) { vfs_close(n); return -1; }
    if (!(cur->euid == 0 || cur->euid == n->uid)) { vfs_close(n); return -1; }
    n->mode = (uint32_t)mode & 07777;
    vfs_close(n);
    //persist via overlay so changes stick across re-resolve
    vfs_set_metadata_override(ap, 1, (uint32_t)mode & 07777, 0, 0, 0, 0);
    return 0;
}

int32_t sys_chown(const char* path, int32_t uid, int32_t gid) {
    if (!path) return -1;
    char ap[VFS_MAX_PATH];
    if (normalize_user_path(path, ap, sizeof(ap)) != 0) return -1;
    vfs_node_t* n = vfs_resolve_path(ap);
    if (!n) return -1;
    process_t* cur = process_get_current();
    if (!cur) { vfs_close(n); return -1; }
    if (cur->euid != 0) { vfs_close(n); return -1; }
    if (uid >= 0) n->uid = (uint32_t)uid;
    if (gid >= 0) n->gid = (uint32_t)gid;
    vfs_close(n);
    vfs_set_metadata_override(ap, 0, 0, uid >= 0, (uint32_t)uid, gid >= 0, (uint32_t)gid);
    return 0;
}

int32_t sys_fchmod(int32_t fd, int32_t mode) {
    vfs_file_t* f = fd_get(fd);
    if (!f || !f->node) return -1;
    process_t* cur = process_get_current();
    if (!cur) return -1;
    if (!(cur->euid == 0 || cur->euid == f->node->uid)) return -1;
    f->node->mode = (uint32_t)mode & 07777;
    return 0;
}

int32_t sys_fchown(int32_t fd, int32_t uid, int32_t gid) {
    vfs_file_t* f = fd_get(fd);
    if (!f || !f->node) return -1;
    process_t* cur = process_get_current();
    if (!cur) return -1;
    if (cur->euid != 0) return -1;
    if (uid >= 0) f->node->uid = (uint32_t)uid;
    if (gid >= 0) f->node->gid = (uint32_t)gid;
    return 0;
}

int32_t sys_waitpid(int32_t pid, int32_t* status, int32_t options) {
    process_t* parent = process_get_current();
    if (!parent) return -1;

    for (;;) {
        bool have_children = false;
        bool found_match = false;
        //scan children for a zombie matching pid (or any if pid == -1)
        process_t* child = parent->children;
        #if LOG_PROC
        serial_write_string("[WAITPID] parent="); serial_printf("%d", (int)parent->pid);
        serial_write_string(" req="); serial_printf("%d", (int)pid);
        serial_write_string(" opts="); serial_printf("%d", (int)options);
        serial_write_string("\n");
        #endif
        while (child) {
            have_children = true;
            #if LOG_PROC
            serial_write_string("[WAITPID] scan child="); serial_printf("%d", (int)child->pid);
            serial_write_string(" state="); serial_printf("%d", (int)child->state);
            serial_write_string("\n");
            #endif
            if (pid == -1 || (int32_t)child->pid == pid) {
                found_match = true;
                if (child->state == PROC_ZOMBIE) {
                    int cpid = (int)child->pid;
                    int ec = child->exit_code;
                    if (status) *status = ec; //raw exit code per sys_wait
                    process_destroy(child);
                    return cpid;
                }
            }
            child = child->sibling;
        }
        //if no children yet or no matching child yet optionally block unless WNOHANG
        if (!have_children || !found_match) {
            if (options & 1) { //WNOHANG
                return 0;
            }
            parent->state = PROC_SLEEPING;
            schedule();
            continue;
        }
        //matching child exists but none zombie block unless WNOHANG
        if (options & 1) { //WNOHANG
            return 0;
        }
        parent->state = PROC_SLEEPING;
        schedule();
    }
}
