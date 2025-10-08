#ifndef _SYS_UN_H
#define _SYS_UN_H

#include <sys/socket.h>

//maximum length of socket path
#define UNIX_PATH_MAX 108

struct sockaddr_un {
    sa_family_t sun_family;  //AF_UNIX 
    char        sun_path[UNIX_PATH_MAX];
};

#endif
