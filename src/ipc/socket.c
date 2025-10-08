#include "socket.h"
#include "../errno_defs.h"
#include "../fs/vfs.h"
#include "../process.h"
#include "../fd.h"
#include "../mm/heap.h"
#include "../drivers/serial.h"
#include <string.h>

#define MAX_SOCKETS 256
#define SOCK_BUFFER_SIZE 8192
#define MAX_PENDING_CONNECTIONS 32

//socket buffer (ring buffer)
typedef struct {
	char data[SOCK_BUFFER_SIZE];
	uint32_t read_pos;
	uint32_t write_pos;
	uint32_t count;
} socket_buffer_t;

//socket state
typedef enum {
	SOCK_STATE_UNBOUND = 0,
	SOCK_STATE_BOUND,
	SOCK_STATE_LISTENING,
	SOCK_STATE_CONNECTED,
	SOCK_STATE_CLOSED
} socket_state_t;

//socket structure
typedef struct socket {
    int valid;
    int domain;              //AF_UNIX
    int type;                //SOCK_STREAM, SOCK_DGRAM
    int protocol;
    socket_state_t state;
    int flags;               //socket flags (O_NONBLOCK etc)

    char path[108];          //unix socket path

    socket_buffer_t recv_buffer;
    socket_buffer_t send_buffer;

    struct socket* peer;     //connected peer socket
    struct socket* listen_queue[MAX_PENDING_CONNECTIONS];
    int listen_queue_len;
    int max_backlog;

    wait_queue_t accept_wq;
    wait_queue_t recv_wq;
    wait_queue_t send_wq;

    vfs_node_t* vfs_node;    //VFS node for this socket
} socket_t;

static socket_t sockets[MAX_SOCKETS];
static int sockets_initialized = 0;

//orward decls for VFS operations
static int socket_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer);
static int socket_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer);
static int socket_vfs_close(vfs_node_t* node);
static int socket_poll_can_read(vfs_node_t* node);
static int socket_poll_can_write(vfs_node_t* node);

static vfs_operations_t socket_ops = {
    .open = NULL,
    .close = socket_vfs_close,
    .read = socket_vfs_read,
    .write = socket_vfs_write,
    .create = NULL,
    .unlink = NULL,
    .mkdir = NULL,
    .rmdir = NULL,
    .readdir = NULL,
    .finddir = NULL,
    .get_size = NULL,
    .ioctl = NULL,
    .readlink = NULL,
    .symlink = NULL,
    .link = NULL,
    .poll_can_read = socket_poll_can_read,
    .poll_can_write = socket_poll_can_write
};

void socket_init(void) {
	memset(sockets, 0, sizeof(sockets));
	sockets_initialized = 1;
	serial_write_string("[IPC] Socket system initialized\n");
}

static socket_t* alloc_socket(void) {
    if (!sockets_initialized) return NULL;

    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i].valid) {
            memset(&sockets[i], 0, sizeof(socket_t));
            sockets[i].valid = 1;
            wait_queue_init(&sockets[i].accept_wq);
            wait_queue_init(&sockets[i].recv_wq);
            wait_queue_init(&sockets[i].send_wq);
            return &sockets[i];
        }
    }
    return NULL;
}

static socket_t* get_socket_from_fd(int fd) {
	vfs_file_t* file = fd_get(fd);
	if (!file || !file->node) return NULL;
	
	//check if this is a socket node
	if (file->node->type != VFS_FILE_TYPE_DEVICE) return NULL;
	
	return (socket_t*)file->node->private_data;
}

static socket_t* find_listening_socket(const char* path) {
	for (int i = 0; i < MAX_SOCKETS; i++) {
		if (sockets[i].valid && 
			sockets[i].state == SOCK_STATE_LISTENING &&
			strcmp(sockets[i].path, path) == 0) {
			return &sockets[i];
		}
	}
	return NULL;
}

//VFS operations for sockets
static int socket_vfs_read(vfs_node_t* node, uint32_t offset, uint32_t size, char* buffer) {
    (void)offset; //sockets don't use offset

    socket_t* sock = (socket_t*)node->private_data;
    if (!sock || !sock->valid) return -EBADF;

    if (sock->state != SOCK_STATE_CONNECTED) return -ENOTCONN;

    socket_buffer_t* rb = &sock->recv_buffer;

    while (rb->count == 0) {
        if (!sock->peer || !sock->peer->valid || sock->peer->state == SOCK_STATE_CLOSED) {
            return 0;
        }
        if (sock->flags & O_NONBLOCK) {
            return -EAGAIN;
        }
        process_wait_on(&sock->recv_wq);
        if (!sock->valid || sock->state != SOCK_STATE_CONNECTED) {
            return 0;
        }
    }

    uint32_t to_read = (size < rb->count) ? size : rb->count;
    uint32_t read = 0;

    while (read < to_read) {
        buffer[read++] = rb->data[rb->read_pos++];
        if (rb->read_pos >= SOCK_BUFFER_SIZE) {
            rb->read_pos = 0;
        }
        rb->count--;
    }

    if (read > 0) {
        wait_queue_wake_all(&sock->send_wq);
    }

    return (int)read;
}

static int socket_vfs_write(vfs_node_t* node, uint32_t offset, uint32_t size, const char* buffer) {
    (void)offset; //sockets don't use offset

    socket_t* sock = (socket_t*)node->private_data;
    if (!sock || !sock->valid) return -EBADF;

    if (sock->state != SOCK_STATE_CONNECTED || !sock->peer || !sock->peer->valid) return -EPIPE;

    socket_buffer_t* rb = &sock->peer->recv_buffer;
    uint32_t written = 0;
    while (written < size) {
        if (!sock->peer || !sock->peer->valid || sock->peer->state != SOCK_STATE_CONNECTED) {
            return written ? (int)written : -EPIPE;
        }

        if (rb->count >= SOCK_BUFFER_SIZE) {
            if (sock->flags & O_NONBLOCK) {
                return written ? (int)written : -EAGAIN;
            }

            process_wait_on(&sock->peer->send_wq);
            continue;
        }

        rb->data[rb->write_pos++] = buffer[written++];
        if (rb->write_pos >= SOCK_BUFFER_SIZE) {
            rb->write_pos = 0;
        }
        rb->count++;
        wait_queue_wake_all(&sock->peer->recv_wq);
    }

    return (int)written;
}

static int socket_vfs_close(vfs_node_t* node) {
	socket_t* sock = (socket_t*)node->private_data;
	if (!sock || !sock->valid) return 0;

	//disconnect peer if connected
	if (sock->peer) {
		socket_t* peer = sock->peer;
		sock->peer = NULL;
		peer->peer = NULL;
		peer->state = SOCK_STATE_CLOSED;
		wait_queue_wake_all(&peer->accept_wq);
		wait_queue_wake_all(&peer->recv_wq);
		wait_queue_wake_all(&peer->send_wq);
	}

	sock->state = SOCK_STATE_CLOSED;
	sock->valid = 0;
	wait_queue_wake_all(&sock->accept_wq);
	wait_queue_wake_all(&sock->recv_wq);
	wait_queue_wake_all(&sock->send_wq);

	return 0;
}

static int socket_poll_can_read(vfs_node_t* node) {
    socket_t* sock = (socket_t*)node->private_data;
    if (!sock || !sock->valid) {
        return 1;
    }

    if (sock->state == SOCK_STATE_LISTENING) {
        return sock->listen_queue_len > 0;
    }

    if (sock->state != SOCK_STATE_CONNECTED) {
        return 1;
    }

    socket_buffer_t* rb = &sock->recv_buffer;
    if (rb->count > 0) {
        return 1;
    }

    if (!sock->peer || !sock->peer->valid || sock->peer->state == SOCK_STATE_CLOSED) {
        return 1;
    }

    return 0;
}

static int socket_poll_can_write(vfs_node_t* node) {
    socket_t* sock = (socket_t*)node->private_data;
    if (!sock || !sock->valid) {
        return 1;
    }

    if (sock->state != SOCK_STATE_CONNECTED) {
        return 1;
    }

    if (!sock->peer || !sock->peer->valid || sock->peer->state == SOCK_STATE_CLOSED) {
        return 1;
    }

    socket_buffer_t* peer_rb = &sock->peer->recv_buffer;
    return peer_rb->count < SOCK_BUFFER_SIZE;
}

//syscals
int sys_socket(int domain, int type, int protocol) {

	if (domain != AF_UNIX) return -EAFNOSUPPORT;
	if (type != SOCK_STREAM && type != SOCK_DGRAM) return -EINVAL;
	
	socket_t* sock = alloc_socket();
	if (!sock) return -ENOMEM;
	
	sock->domain = domain;
	sock->type = type;
	sock->protocol = protocol;
	sock->state = SOCK_STATE_UNBOUND;
	
	// Create a VFS node for this socket
	vfs_node_t* node = vfs_create_node("socket", VFS_FILE_TYPE_DEVICE, 0);
	if (!node) {
		sock->valid = 0;
		return -ENOMEM;
	}
	
	node->ops = &socket_ops;
	node->private_data = sock;
	sock->vfs_node = node;
	
	//allocate FD for this socket
	int fd = fd_alloc(node, O_RDWR, 0);
	if (fd < 0) {
		vfs_destroy_node(node);
		sock->valid = 0;
		return -EMFILE;
	}
	
	return fd;
}

int sys_bind(int sockfd, const void* addr, uint32_t addrlen) {
	(void)addrlen;
	
	socket_t* sock = get_socket_from_fd(sockfd);
	if (!sock) return -EBADF;
	
	if (sock->state != SOCK_STATE_UNBOUND) return -EINVAL;
	
	//parse sockaddr_un
	struct sockaddr_un {
		uint16_t sun_family;
		char sun_path[108];
	};
	
	const struct sockaddr_un* un_addr = (const struct sockaddr_un*)addr;
	if (un_addr->sun_family != AF_UNIX) return -EINVAL;
	
	//copy path
	strncpy(sock->path, un_addr->sun_path, sizeof(sock->path) - 1);
	sock->path[sizeof(sock->path) - 1] = '\0';
	
	//create socket file in VFS at sock->path
	//this is needed so that connect() can find the listening socket
	int create_result = vfs_create(sock->path, 0);
	if (create_result != 0) {
		//if creation fails we still allow binding but connection lookup will fail
		//this is acceptable for now since we're primarily using this for IPC
		serial_write_string("[IPC] Warning: Failed to create socket file in VFS\n");
	}
	
	sock->state = SOCK_STATE_BOUND;
	return 0;
}

int sys_listen(int sockfd, int backlog) {
	socket_t* sock = get_socket_from_fd(sockfd);
	if (!sock) return -EBADF;
	
	if (sock->state != SOCK_STATE_BOUND) return -EINVAL;
	if (sock->type != SOCK_STREAM) return -EOPNOTSUPP;
	
	sock->state = SOCK_STATE_LISTENING;
	sock->max_backlog = (backlog > MAX_PENDING_CONNECTIONS) ? MAX_PENDING_CONNECTIONS : backlog;
	
	return 0;
}

int sys_accept(int sockfd, void* addr, uint32_t* addrlen) {
	(void)addr; (void)addrlen; //TODO: fill in client address
	
	socket_t* sock = get_socket_from_fd(sockfd);
	if (!sock) return -EBADF;
	
	if (sock->state != SOCK_STATE_LISTENING) return -EINVAL;
	
	while (sock->listen_queue_len == 0) {
		if (sock->flags & O_NONBLOCK) {
			return -EAGAIN;
		}
		process_wait_on(&sock->accept_wq);
		if (!sock->valid || sock->state != SOCK_STATE_LISTENING) {
			return -ECONNABORTED;
		}
	}
	
	//get next connection from queue
	socket_t* client_sock = sock->listen_queue[0];
	for (int i = 0; i < sock->listen_queue_len - 1; i++) {
		sock->listen_queue[i] = sock->listen_queue[i + 1];
	}
	sock->listen_queue_len--;
	
	//create a new server-side socket for this connection
	socket_t* server_sock = alloc_socket();
	if (!server_sock) return -ENOMEM;
	
	server_sock->domain = sock->domain;
	server_sock->type = sock->type;
	server_sock->protocol = sock->protocol;
	server_sock->state = SOCK_STATE_CONNECTED;
	
	//establish bidirectional peer relationship
	server_sock->peer = client_sock;
	client_sock->peer = server_sock;
	wait_queue_wake_all(&client_sock->recv_wq);
	wait_queue_wake_all(&client_sock->send_wq);
	
	//create VFS node for the server-side accepted socket
	vfs_node_t* server_node = vfs_create_node("socket", VFS_FILE_TYPE_DEVICE, 0);
	if (!server_node) {
		server_sock->valid = 0;
		client_sock->peer = NULL;
		return -ENOMEM;
	}
	
	server_node->ops = &socket_ops;
	server_node->private_data = server_sock;
	server_sock->vfs_node = server_node;
	
	//allocate FD for accepted socket
	int server_fd = fd_alloc(server_node, O_RDWR, 0);
	if (server_fd < 0) {
		vfs_destroy_node(server_node);
		server_sock->valid = 0;
		client_sock->peer = NULL;
		return -EMFILE;
	}
	
	return server_fd;
}

int sys_connect(int sockfd, const void* addr, uint32_t addrlen) {
	(void)addrlen;
	
	socket_t* sock = get_socket_from_fd(sockfd);
	if (!sock) return -EBADF;
	if (sock->state != SOCK_STATE_UNBOUND && sock->state != SOCK_STATE_CONNECTED) return -EINVAL;
	
	//parse sockaddr_un
	struct sockaddr_un {
		uint16_t sun_family;
		char sun_path[108];
	};
	
	const struct sockaddr_un* un_addr = (const struct sockaddr_un*)addr;
	if (un_addr->sun_family != AF_UNIX) return -EINVAL;
	
	//find listening socket with this path
	socket_t* listen_sock = find_listening_socket(un_addr->sun_path);
	if (!listen_sock) return -ECONNREFUSED;
	
	//check if accept queue is full
	if (listen_sock->listen_queue_len >= listen_sock->max_backlog) {
		return -ECONNREFUSED;
	}
	
	//set up connection
	sock->state = SOCK_STATE_CONNECTED;
	sock->peer = NULL; //will be set by accept()

	listen_sock->listen_queue[listen_sock->listen_queue_len++] = sock;
	wait_queue_wake_one(&listen_sock->accept_wq);

	//for blocking sockets wait until the server accepts and establishes the peer link
	if (!(sock->flags & O_NONBLOCK)) {
		while (sock->peer == NULL && sock->state == SOCK_STATE_CONNECTED) {
			process_wait_on(&sock->recv_wq);
		}

		if (sock->peer == NULL) {
			//connection was not established (listener closed or other failure)
			sock->state = SOCK_STATE_CLOSED;
			return -ECONNREFUSED;
		}
	}

	return 0;
}
