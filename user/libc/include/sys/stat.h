#ifndef LIBC_SYS_STAT_H
#define LIBC_SYS_STAT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

//permission bits
#ifndef S_IRUSR
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_ISUID 04000
#define S_ISGID 02000
#endif

//file type bits
#ifndef S_IFMT
#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFLNK  0120000
#define S_IFCHR  0020000
#endif

#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)

typedef struct stat {
    unsigned st_mode;  //type + perms
    unsigned st_uid;
    unsigned st_gid;
    unsigned st_size;
} stat_t;

int stat(const char* path, struct stat* st);
int lstat(const char* path, struct stat* st);
int fstat(int fd, struct stat* st);
int chmod(const char* path, int mode);
int chown(const char* path, int uid, int gid);
int fchmod(int fd, int mode);
int fchown(int fd, int uid, int gid);

#ifdef __cplusplus
}
#endif

#endif
