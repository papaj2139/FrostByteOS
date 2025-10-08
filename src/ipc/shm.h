#ifndef IPC_SHM_H
#define IPC_SHM_H

#include <stdint.h>
#include <stddef.h>
#include "../errno_defs.h"

//IPC constants
#define IPC_PRIVATE 0
#define IPC_CREAT  01000
#define IPC_EXCL   02000
#define IPC_RMID   0
#define IPC_STAT   2
#define IPC_SET    1

//shared memory flags
#define SHM_RDONLY 010000
#define SHM_RND    020000

typedef int key_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int mode_t;
typedef int pid_t;

void shm_init(void);
int sys_shmget(int key, int size, int shmflg);
int sys_shmat(int shmid, const void* shmaddr, int shmflg);
int sys_shmdt(const void* shmaddr);
int sys_shmctl(int shmid, int cmd, void* buf);

#endif
