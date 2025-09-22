#ifndef TTY_H
#define TTY_H

#include <stdint.h>

//TTY mode bits
#define TTY_MODE_CANON (1u << 0)  //canonical (line) mode when set raw when clear
#define TTY_MODE_ECHO  (1u << 1)  //echo typed characters when set

//TTY ioctls
#define TTY_IOCTL_SET_MODE   1u
#define TTY_IOCTL_GET_MODE   2u
#define TTY_IOCTL_BEGIN_READ 3u
#define TTY_IOCTL_END_READ   4u

//initialize and register the TTY device (tty0)
int tty_register_device(void);

//TTY I/O helpers used by syscalls
int tty_read(char* buf, uint32_t size);
int tty_read_mode(char* buf, uint32_t size, uint32_t mode);
int tty_write(const char* buf, uint32_t size);
//returns non-zero if a process is currently blocked in tty_read_mode
int tty_is_reading(void);

//mode control used by syscall layer
void tty_set_mode(uint32_t mode);
uint32_t tty_get_mode(void);
int tty_ioctl(uint32_t cmd, void* arg);

#endif
