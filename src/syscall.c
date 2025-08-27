#include "syscall.h"
#include "interrupts/idt.h"
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
    //only support stdout (fd=1) and stderr (fd=2)
    if (fd == 1 || fd == 2) {
        //write to console create a temporary buffer for print
        char temp_buf[256];
        uint32_t copy_count = (count < 255) ? count : 255;
        for (uint32_t i = 0; i < copy_count; i++) {
            if (buf[i] == '\0') break;
            temp_buf[i] = buf[i];
        }
        temp_buf[copy_count] = '\0';
        print(temp_buf, 0x0F);
        return (int32_t)count;
    }
    return -1; //EBADF - bad file descriptor
}

int32_t sys_read(int32_t fd, char* buf, uint32_t count) {
    (void)buf;   //suppress unused parameter warning
    (void)count; //suppress unused parameter warning
    
    //only support stdin (fd=0)
    if (fd == 0) {
        //will read keyboard buffer later
        return 0;
    }
    return -1; //EBADF - bad file descriptor
}

int32_t sys_open(const char* pathname, int32_t flags) {
    //not implemented yet
    (void)pathname;
    (void)flags;
    return -1; //ENOSYS - Function not implemented
}

int32_t sys_close(int32_t fd) {
    //not implemented yet
    (void)fd;
    return -1;
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
