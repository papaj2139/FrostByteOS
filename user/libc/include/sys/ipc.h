#ifndef _SYS_IPC_H
#define _SYS_IPC_H

#include <sys/types.h>

//mode bits for IPC 
#define IPC_CREAT  01000   //create key if key does not exist 
#define IPC_EXCL   02000   //fail if key exists 
#define IPC_NOWAIT 04000   //return error on wait 

//control commands
#define IPC_RMID   0       //remove identifier 
#define IPC_SET    1       //set options 
#define IPC_STAT   2       //get options 

//special key values 
#define IPC_PRIVATE ((key_t)0)

typedef int key_t;

struct ipc_perm {
    key_t  key;
    uid_t  uid;
    gid_t  gid;
    uid_t  cuid;
    gid_t  cgid;
    mode_t mode;
    int    seq;
};

key_t ftok(const char *pathname, int proj_id);

#endif
