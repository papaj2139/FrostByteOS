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
#include <stdint.h>
#include <string.h>

//forward declarations
void print(char* msg, unsigned char colour);
void kpanic_msg(const char* reason);

//external assembly handler
extern void syscall_handler_asm(void);

//initialize syscall system
void syscall_init(void) {
    //install syscall handler at interrupt 0x80
    //use DPL=3 trap gate (0xEF) so IF remains enabled inside syscalls
    idt_set_gate(SYSCALL_INT, (uint32_t)syscall_handler_asm, 0x08, 0xEF);
    fd_init();
}

//capture user-mode return frame at syscall entry so fork() can clone the exact return point
void syscall_capture_user_frame(uint32_t eip, uint32_t cs, uint32_t eflags, uint32_t useresp, uint32_t ss) {
    process_t* cur = process_get_current();
    if (!cur) return;
    cur->context.eip = eip;
    cur->context.cs = cs;
    cur->context.eflags = eflags;
    cur->context.esp = useresp;
    cur->context.ss = ss;
}

//clone user space from src->dst directories (user part only)
static int clone_user_space(page_directory_t src, page_directory_t dst) {
    if (!src || !dst) return -1;
    const uint32_t TMP_SRC = 0xE0000000; //high kernel scratch outside kernel heap
    const uint32_t TMP_DST = 0xE0001000;
    for (int i = 0; i < 768; i++) { //user space only
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
        case SYS_EXECVE:
            return sys_execve((const char*)arg1, (char* const*)arg2, (char* const*)arg3);
        case SYS_WAIT:
            return sys_wait((int32_t*)arg1);
        case SYS_YIELD:
            return sys_yield();
        case SYS_IOCTL:
            return sys_ioctl((int32_t)arg1, arg2, (void*)arg3);
        default:
            print("Unknown syscall\n", 0x0F);
            return -1; //ENOSYS = Function not implemented
    }
}

//syscall implementations
int32_t sys_exit(int32_t status) {
    serial_write_string("[EXIT] sys_exit called with status=\n");
    serial_printf("%d", status);
    serial_write_string("\n");
    process_exit(status);
    return 0; //Never reached
}

int32_t sys_write(int32_t fd, const char* buf, uint32_t count) {
    serial_write_string("[SYSCALL] Write called - fd: ");
    serial_printf("%d", fd);
    serial_write_string(", count: ");
    serial_printf("%d", count);
    serial_write_string(", buf=0x");
    serial_printf("%x", (uint32_t)buf);
    serial_write_string("\n");
    
    if (fd == 1 || fd == 2) {
        serial_write_string("[SYSCALL] Writing to TTY\n");
        int written = tty_write(buf, count);
        return (written < 0) ? written : (int32_t)count;
    }
    
    vfs_file_t* file = fd_get(fd);
    if (!file) {
        serial_write_string("[SYSCALL] Invalid file descriptor\n");
        return -1; //EBADF
    }

    serial_write_string("[SYSCALL] Writing to file via VFS\n");
    int bytes_written = vfs_write(file->node, file->offset, count, buf);
    if (bytes_written >= 0) {
        file->offset += bytes_written;
    }
    serial_write_string("[SYSCALL] Write completed, bytes: ");
    serial_printf("%d", bytes_written);
    serial_write_string("\n");
    return bytes_written;
}

int32_t sys_read(int32_t fd, char* buf, uint32_t count) {
    if (fd == 0) {
        //read from controlling TTY using the current process TTY mode
        process_t* cur = process_get_current();
        uint32_t mode = (cur) ? cur->tty_mode : (TTY_MODE_CANON | TTY_MODE_ECHO);
        return tty_read_mode(buf, count, mode);
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
    uint64_t now = timer_get_ticks();
    uint32_t ticks = seconds * 100;
    serial_write_string("[SLEEP] seconds=\n");
    serial_printf("%d", seconds);
    serial_write_string(" ticks=\n");
    serial_printf("%d", ticks);
    serial_write_string(" now=\n");
    serial_printf("%u", (uint32_t)now);
    serial_write_string(" wake_at=\n");
    serial_printf("%u", (uint32_t)(now + ticks));
    serial_write_string("\n");
    process_sleep(ticks); //convert to ticks
    serial_write_string("[SLEEP] woke up\n");
    return 0;
}

int32_t sys_fork(void) {
    process_t* parent = process_get_current();
    if (!parent) return -1;
    serial_write_string("[FORK] enter\n");

    //disable interrupts during fork to avoid reentrancy
    uint32_t eflags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags) :: "memory");
    serial_write_string("[FORK] post-cli\n");

    serial_write_string("[FORK] pre-create\n");

    //create child as USER-MODE process
    process_t* child = process_create(parent->name, (void*)parent->context.eip, true);
    if (!child) {
        serial_write_string("[FORK] process_create failed\n");
        //restore IF if it was set
        if (eflags & 0x200) __asm__ volatile ("sti");
        return -1;
    }
    serial_write_string("[FORK] created\n");

    //clone user address space once per-process CR3 is enabled but that later
    //if (clone_user_space(parent->page_directory, child->page_directory) != 0) {
    //    serial_write_string("[FORK] clone_user_space failed\n");
    //    process_destroy(child);
    //    if (eflags & 0x200) __asm__ volatile ("sti");
    //    return -1;
    //}

    //inherit minimal context so child returns to the same user EIP with ESP preserved
    //and EAX=0 in the child per POSIX semantics
    child->context.eip = parent->context.eip;      //return point in user space
    child->context.esp = parent->context.esp;      //user stack pointer at syscall entry
    child->context.ebp = parent->context.esp;      //best-effort (we don't capture exact EBP)
    child->context.cs  = 0x1B;                     //user code segment
    child->context.ss  = 0x23;                     //user stack segment
    child->context.ds = child->context.es = child->context.fs = child->context.gs = 0x23;
    child->context.eflags = parent->context.eflags; //preserve IF and flags
    child->context.eax = 0;                        //fork() returns 0 in child

    //inherit TTY mode and controlling TTY
    child->tty = parent->tty;
    child->tty_mode = parent->tty_mode;

    //ensure parent saved user context reflects fork return value as well
    parent->context.eax = (uint32_t)child->pid;

    //restore IF if it was set
    if (eflags & 0x200) __asm__ volatile ("sti");
    serial_write_string("[FORK] return\n");

    //return child's PID in parent
    return (int32_t)child->pid;
}

int32_t sys_execve(const char* pathname, char* const argv[], char* const envp[]) {
    (void)argv; (void)envp; //not yet used by flat loader
    if (!pathname) return -1;

    //open the target program from VFS
    vfs_node_t* node = vfs_open(pathname, VFS_FLAG_READ);
    if (!node) {
        serial_write_string("[EXEC] File not found\n");
        return -1;
    }

    int fsize = vfs_get_size(node);
    if (fsize <= 0) {
        vfs_close(node);
        serial_write_string("[EXEC] Invalid file size\n");
        return -1;
    }
    if (fsize > 4096) fsize = 4096; //single-page flat loader for now

    const uint32_t entry_va = 0x01000000;      //program entry VA
    const uint32_t temp_kmap = 0x00800000;     //temp kernel VA for copying
    const uint32_t ustack_top = 0x02000000;    //user stack top

    //allocate a new physical page for code
    uint32_t new_code_phys = pmm_alloc_page();
    if (!new_code_phys) {
        vfs_close(node);
        serial_write_string("[EXEC] pmm_alloc_page failed for code\n");
        return -1;
    }

    if (vmm_map_page(temp_kmap, new_code_phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        vfs_close(node);
        pmm_free_page(new_code_phys);
        serial_write_string("[EXEC] map temp page failed\n");
        return -1;
    }

    //zero and copy program into page
    memset((void*)temp_kmap, 0, 4096);
    uint32_t offset = 0;
    while (offset < (uint32_t)fsize) {
        int r = vfs_read(node, offset, (uint32_t)(fsize - offset), (char*)((uint8_t*)temp_kmap + offset));
        if (r <= 0) break;
        offset += (uint32_t)r;
    }
    vfs_close(node);
    vmm_unmap_page_nofree(temp_kmap);

    //replace mapping at entry_va with new code page
    uint32_t old_code_phys = vmm_get_physical_addr(entry_va) & ~0xFFF;
    if (vmm_map_page(entry_va, new_code_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
        pmm_free_page(new_code_phys);
        serial_write_string("[EXEC] map code in kernel dir failed\n");
        return -1;
    }

    //map into the process's user directory for future per-process CR3 use
    process_t* cur = process_get_current();
    if (cur && cur->page_directory) {
        (void)vmm_map_page_in_directory(cur->page_directory, entry_va, new_code_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
    }

    //free old code phys if it existed and differs
    if (old_code_phys && old_code_phys != new_code_phys) {
        pmm_free_page(old_code_phys);
    }

    //create a fresh user stack page and map it
    uint32_t new_stack_phys = pmm_alloc_page();
    if (!new_stack_phys) {
        serial_write_string("[EXEC] alloc user stack failed\n");
        return -1;
    }
    uint32_t ustack_va = ustack_top - 0x1000;
    uint32_t old_stack_phys = vmm_get_physical_addr(ustack_va) & ~0xFFF;
    if (vmm_map_page(ustack_va, new_stack_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE) != 0) {
        pmm_free_page(new_stack_phys);
        serial_write_string("[EXEC] map user stack (kernel dir) failed\n");
        return -1;
    }
    if (cur && cur->page_directory) {
        (void)vmm_map_page_in_directory(cur->page_directory, ustack_va, new_stack_phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
    }
    if (old_stack_phys && old_stack_phys != new_stack_phys) {
        pmm_free_page(old_stack_phys);
    }

    //build user stack with argc argv[] envp[] and strings
    uint32_t sp = ustack_top; //stack grows down

    //count argc and envc
    int argc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }
    int envc = 0;
    if (envp) {
        while (envp[envc]) envc++;
    }

    //calculate total size for strings
    uint32_t strings_size = 0;
    for (int i = 0; i < argc; i++) {
        strings_size += (uint32_t)strlen(argv[i]) + 1;
    }
    for (int i = 0; i < envc; i++) {
        strings_size += (uint32_t)strlen(envp[i]) + 1;
    }

    //space for vectors: argc + argv pointers + NULL + envp pointers + NULL
    uint32_t vector_words = 1 + (uint32_t)argc + 1 + (uint32_t)envc + 1;
    uint32_t vector_bytes = vector_words * 4;

    //ensure it fits in one page (naive check)
    if (strings_size + vector_bytes + 64 > 4096) {
        serial_write_string("[EXEC] argv/envp too large for one-page stack\n");
        return -1;
    }

    //copy strings to stack and record user pointers
    uint32_t* argv_user = NULL;
    uint32_t* envp_user = NULL;
    if (argc > 0) argv_user = (uint32_t*)kmalloc(sizeof(uint32_t) * (uint32_t)argc);
    if (envc > 0) envp_user = (uint32_t*)kmalloc(sizeof(uint32_t) * (uint32_t)envc);

    //copy envp strings
    for (int i = envc - 1; i >= 0; i--) {
        const char* s = envp[i];
        uint32_t len = (uint32_t)strlen(s) + 1;
        sp -= len;
        memcpy((void*)sp, s, len);
        envp_user[i] = sp;
    }
    //copy argv strings
    for (int i = argc - 1; i >= 0; i--) {
        const char* s = argv[i];
        uint32_t len = (uint32_t)strlen(s) + 1;
        sp -= len;
        memcpy((void*)sp, s, len);
        argv_user[i] = sp;
    }

    //aign stack to 16 bytes
    sp &= ~0xF;

    //push envp NULL
    sp -= 4; *(uint32_t*)sp = 0;
    //push envp pointers in reverse so they appear in ascending order in memory
    for (int i = envc - 1; i >= 0; i--) {
        sp -= 4; *(uint32_t*)sp = envp_user[i];
    }
    //push argv NULL
    sp -= 4; *(uint32_t*)sp = 0;
    //push argv pointers in reverse
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 4; *(uint32_t*)sp = argv_user[i];
    }
    //push argc
    sp -= 4; *(uint32_t*)sp = (uint32_t)argc;

    if (argv_user) kfree(argv_user);
    if (envp_user) kfree(envp_user);

    //reset process context and TTY defaults
    if (cur) {
        strncpy(cur->name, pathname, PROCESS_NAME_MAX - 1);
        cur->name[PROCESS_NAME_MAX - 1] = '\0';
        cur->context.eip = entry_va;
        cur->context.esp = sp;
        cur->context.ebp = sp;
        cur->user_eip = entry_va;
        cur->tty_mode = TTY_MODE_CANON | TTY_MODE_ECHO;
    }

    serial_write_string("[EXEC] Success\n");
    //jump directly to user mode at the new entry execve() does not return on success
    switch_to_user_mode(entry_va, sp);
    //not reached
    return -1;
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
                serial_write_string("[WAIT] returning pid=\n");
                serial_printf("%d", pid);
                serial_write_string(" status=\n");
                serial_printf("%d", ec);
                serial_write_string("\n");
                process_destroy(child);
                return pid;
            }
            child = child->sibling;
        }
        //no zombies if no children at all return -1 immediately
        if (!parent->children) return -1;
        //sleep until a child exits
        serial_write_string("[WAIT] sleeping parent pid=\n");
        serial_printf("%d", (int)parent->pid);
        serial_write_string("\n");
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

