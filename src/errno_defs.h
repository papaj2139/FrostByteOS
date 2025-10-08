#ifndef ERRNO_DEFS_H
#define ERRNO_DEFS_H

//standard error codes
#define EPERM           1      //operation not permitted
#define ENOENT          2      //no such file or directory
#define ESRCH           3      //no such process
#define EINTR           4      //interrupted system call
#define EIO             5      //I/O error
#define ENXIO           6      //no such device or address
#define E2BIG           7      //argument list too long
#define ENOEXEC         8      //exec format error
#define EBADF           9      //bad file number
#define ECHILD          10     //no child processes
#define EAGAIN          11     //try again
#define ENOMEM          12     //out of memory
#define EACCES          13     //permission denied
#define EFAULT          14     //bad address
#define ENOTBLK         15     //block device required
#define EBUSY           16     //device or resource busy
#define EEXIST          17     //file exists
#define EXDEV           18     //cross-device link
#define ENODEV          19     //no such device
#define ENOTDIR         20     //not a directory
#define EISDIR          21     //is a directory
#define EINVAL          22     //invalid argument
#define ENFILE          23     //file table overflow
#define EMFILE          24     //too many open files
#define ENOTTY          25     //not a typewriter
#define ETXTBSY         26     //text file busy
#define EFBIG           27     //file too large
#define ENOSPC          28     //no space left on device
#define ESPIPE          29     //illegal seek
#define EROFS           30     //read-only file system
#define EMLINK          31     //too many links
#define EPIPE           32     //broken pipe
#define EDOM            33     //math argument out of domain of func
#define ERANGE          34     //math result not representable
#define ECONNABORTED    103    //software caused connection abort
#define EWOULDBLOCK     EAGAIN //operation would block
#define EAFNOSUPPORT    97     //address family not supported by protocol
#define EOPNOTSUPP      95     //operation not supported on transport endpoint
#define ECONNREFUSED    111    //connection refused
#define ENOTCONN        107    //transport endpoint is not connected

//file flags
#define O_RDONLY        0
#define O_WRONLY        1
#define O_RDWR          2
#define O_CREAT         0100
#define O_EXCL          0200
#define O_NOCTTY        0400
#define O_TRUNC         01000
#define O_APPEND        02000
#define O_NONBLOCK      04000

#endif
