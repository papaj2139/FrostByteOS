#ifndef LIBC_TTY_H
#define LIBC_TTY_H

//TTY ioctl and mode bits
#define TTY_MODE_CANON (1u << 0)
#define TTY_MODE_ECHO  (1u << 1)

#define TTY_IOCTL_SET_MODE  1u
#define TTY_IOCTL_GET_MODE  2u

#endif
