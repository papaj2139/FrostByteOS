#include "syscall.h"
#include "fs/vfs.h"
#include "fd.h"
#include "io.h"
#include "mm/pmm.h"
#include "interrupts/idt.h"
#include "drivers/serial.h"
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
    //use DPL=3 (0xEE) to allow user-mode access now that we have TSS
    idt_set_gate(SYSCALL_INT, (uint32_t)syscall_handler_asm, 0x08, 0xEE);
    fd_init();
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
        default:
            print("Unknown syscall\n", 0x0F);
            return -1; //ENOSYS = Function not implemented
    }
}

//syscall implementations
int32_t sys_exit(int32_t status) {
    print("Process exiting with status ", 0x0F);
    if (status == 0) {
        print("0\n", 0x0F);
    } else {
        print("non-zero\n", 0x0F);
    }
    //just halt theres no process manager yet
    while(1) {
        __asm__ volatile ("hlt");
    }
    return 0;
}

int32_t sys_write(int32_t fd, const char* buf, uint32_t count) {
    serial_write_string("[SYSCALL] Write called - fd: ");
    serial_printf("%d", fd);
    serial_write_string(", count: ");
    serial_printf("%d", count);
    serial_write_string("\n");
    
    if (fd == 1 || fd == 2) {
        serial_write_string("[SYSCALL] Writing to stdout\n");
        char temp_buf[256];
        uint32_t remaining = count;
        uint32_t offset = 0;
        while (remaining > 0) {
            uint32_t chunk = (remaining < 255) ? remaining : 255;
            memcpy(temp_buf, buf + offset, chunk);
            temp_buf[chunk] = '\0';
            print(temp_buf, 0x0F);
            offset += chunk;
            remaining -= chunk;
        }
        return (int32_t)count;
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
        //stdin not implemented
        return 0;
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
    //always return PID 1
    return 1;
}

int32_t sys_sleep(uint32_t seconds) {
    //just busy-wait there's no scheduler yet
    print("Sleeping for ", 0x0F);
    if (seconds == 1) {
        print("1 second...\n", 0x0F);
    } else {
        print("multiple seconds...\n", 0x0F);
    }
    
    for (uint32_t i = 0; i < seconds * 50000; i++) {
        __asm__ volatile ("nop");
    }
    
    print("Sleep completed!\n", 0x0F);
    return 0;
}

