#ifndef IPC_SOCKET_H
#define IPC_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include "../errno_defs.h"

//socket constants
#define AF_UNIX 1
#define SOCK_STREAM 1
#define SOCK_DGRAM 2

//forward declarations
typedef struct file file_t;

void socket_init(void);
int sys_socket(int domain, int type, int protocol);
int sys_bind(int sockfd, const void* addr, uint32_t addrlen);
int sys_listen(int sockfd, int backlog);
int sys_accept(int sockfd, void* addr, uint32_t* addrlen);
int sys_connect(int sockfd, const void* addr, uint32_t addrlen);
int socket_read(file_t* file, char* buf, size_t count);
int socket_write(file_t* file, const char* buf, size_t count);

#endif
