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

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_ANON    0x1
#define MAP_FIXED   0x10
#define MMAP_SCAN_START 0x04000000u   //avoid low 8MB identity region
#define MMAP_SCAN_END   0x7F000000u   //keep under 2GiB to avoid sign issues
#define USER_HEAP_BASE 0x03000000u


//external assembly handler
extern void syscall_handler_asm(void);

//compute UNIX epoch seconds from RTC time (naive and UTC assumption)
static int is_leap(unsigned y) { 
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0); 
}
static const int mdays_norm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

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
            if (vmm_get_physical_addr(base + off) != 0) { ok = false; break; }
        }
        if (ok) return base;
    }
    return 0;
}

void syscall_mark_exit(void) {
    process_t* cur = process_get_current();
    if (cur) cur->in_kernel = false;
}

//initialize syscall system
void syscall_init(void) {
    //install syscall handler at interrupt 0x80
    //use DPL=3 trap gate (0xEF) so IF remains enabled inside syscalls
    idt_set_gate(SYSCALL_INT, (uint32_t)syscall_handler_asm, 0x08, 0xEF);
    fd_init();
}

//capture user-mode return frame at syscall entry so fork() can clone the exact return point
void syscall_capture_user_frame(uint32_t eip, uint32_t cs, uint32_t eflags, uint32_t useresp, uint32_t ss, uint32_t ebp) {
    process_t* cur = process_get_current();
    if (!cur) return;
    cur->context.eip = eip;
    cur->context.cs = cs;
    cur->context.eflags = eflags;
    cur->context.esp = useresp;
    cur->context.ss = ss;
    cur->context.ebp = ebp;
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
            // map into dst directory at vaddr
            if (vmm_map_page_in_directory(dst, vaddr, dst_phys, flags) != 0) return -1;
        }
    }
    return 0;
}

//main syscall dispatcher called from assembly
int32_t syscall_dispatch(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    (void)arg4; //suppress unused parameter warning
    (void)arg5; //suppress unused parameter warning
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
            return sys_execve(p, avp, evp);
        }
        case SYS_WAIT:
            return sys_wait((int32_t*)arg1);
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
    
    if (fd == 1 || fd == 2) {
        //route stdout/stderr to controlling TTY device
        process_t* curp = process_get_current();
        device_t* dev = (curp) ? curp->tty : NULL;
        if (dev) {
            int wr = device_write(dev, 0, buf, count);
            return (wr < 0) ? wr : (int32_t)count;
        } else {
            int written = tty_write(buf, count);
            return (written < 0) ? written : (int32_t)count;
        }
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
    int bytes_written = vfs_write(file->node, file->offset, count, buf);
    if (bytes_written >= 0) {
        file->offset += bytes_written;
    }
    #if LOG_SYSCALL
    serial_write_string("[SYSCALL] Write completed, bytes: ");
    serial_printf("%d", bytes_written);
    serial_write_string("\n");
    #endif
    return bytes_written;
}

int32_t sys_read(int32_t fd, char* buf, uint32_t count) {
    if (fd == 0) {
        //read from controlling TTY using the current process TTY mode
        process_t* cur = process_get_current();
        uint32_t mode = (cur) ? cur->tty_mode : (TTY_MODE_CANON | TTY_MODE_ECHO);
        device_t* dev = (cur) ? cur->tty : NULL;
        if (!dev || strcmp(dev->name, "tty0") == 0) {
            //text console keyboard path
            return tty_read_mode(buf, count, mode);
        } else {
            //serial or other device: implement a minimal line/raw reader via device manager
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
                return (int32_t)pos;
            }
        }
    }
    
    vfs_file_t* file = fd_get(fd);
    if (!file) {
        return -1; //EBADF
    }

    int bytes_read = vfs_read(file->node, file->offset, count, buf);
    if (bytes_read >= 0) {
        file->offset += bytes_read;
    }
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
    
    vfs_node_t* node = vfs_open(pathname, vfs_flags);
    if (!node) {
        return -1;
    }
    return fd_alloc(node, flags);
}

int32_t sys_close(int32_t fd) {
    fd_close(fd);
    return 0;
}

int32_t sys_creat(const char* pathname, int32_t mode) {
    int result = vfs_create(pathname, mode);
    if (result != 0) {
        return -1;
    }
    return sys_open(pathname, 0); //open the file with default flags
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
    child->context.eax = 0;                        //fork() returns 0 in child

    //inherit TTY mode and controlling TTY
    child->tty = parent->tty;
    child->tty_mode = parent->tty_mode;

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

    int er = elf_execve(pathname, argv, envp);
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
            cur->tty_mode = *(uint32_t*)arg;
            return 0;
        } else if (cmd == TTY_IOCTL_GET_MODE) {
            if (!arg) return -1;
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
    return vfs_unlink(path);
}

int32_t sys_mkdir(const char* path, int32_t mode) {
    (void)mode; //modes not supported yet
    if (!path) return -1;
    return vfs_mkdir(path, 0);
}

int32_t sys_rmdir(const char* path) {
    if (!path) return -1;
    return vfs_rmdir(path);
}

int32_t sys_readdir_fd(int32_t fd, uint32_t index, char* name_buf, uint32_t buf_size, uint32_t* out_type) {
    vfs_file_t* file = fd_get(fd);
    if (!file || !file->node) return -1;
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
    int r = vfs_mount(device, mount_point, fs_type);
    return (r == 0) ? 0 : -1;
}

int32_t sys_umount(const char* mount_point) {
    if (!mount_point) return -1;
    int r = vfs_unmount(mount_point);
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
    rtc_time_t t;
    if (!rtc_read(&t)) return -1;
    unsigned y = t.year;
    unsigned m = t.month;
    unsigned d = t.day;
    unsigned hh = t.hour;
    unsigned mm = t.minute;
    unsigned ss = t.second;
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
    uint64_t days = 0;
    for (unsigned yr = 1970; yr < y; ++yr) days += is_leap(yr) ? 366 : 365;
    for (unsigned i = 1; i < m; ++i) {
        days += mdays_norm[i-1];
        if (i == 2 && is_leap(y)) days += 1;
    }
    days += (d - 1);
    uint64_t secs = days * 86400ull + hh * 3600ull + mm * 60ull + ss;
    return (int32_t)secs;
}