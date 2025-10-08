#include "libfwm.h"
#include "fwm_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>

#define MAX_WINDOWS 64

typedef struct {
    fwm_window_t id;
    uint32_t width;
    uint32_t height;
    uint32_t shm_key;
    void* buffer;
    int shm_id;
} fwm_window_info_t;

struct fwm_connection {
    int fd;
    uint32_t client_id;
    uint32_t seq;
    uint32_t screen_width;
    uint32_t screen_height;
    fwm_window_info_t windows[MAX_WINDOWS];
    int num_windows;
};

static int send_message(fwm_connection_t* conn, const void* msg, size_t len) {
    return write(conn->fd, msg, len) == (ssize_t)len ? 0 : -1;
}

static int recv_message(fwm_connection_t* conn, void* msg, size_t max_len) {
    fwm_msg_header_t header;
    
    //try to read with retries for slow server responses
    int retries = 10;
    ssize_t n;
    while (retries > 0) {
        n = read(conn->fd, &header, sizeof(header));
        if (n == sizeof(header)) break;
        if (n < 0 && errno == EAGAIN) {
            //would block - wait a bit
            usleep(10000); //10ms
            retries--;
            continue;
        }
        if (n == 0 || n < 0) return -1; //error or disconnect
        //partial read - try again
        retries--;
        usleep(1000);
    }
    if (n != sizeof(header)) return -1;
    
    if (header.length > max_len) return -1;
    
    memcpy(msg, &header, sizeof(header));
    if (header.length > sizeof(header)) {
        size_t remaining = header.length - sizeof(header);
        retries = 10;
        while (remaining > 0 && retries > 0) {
            n = read(conn->fd, (char*)msg + sizeof(header) + (header.length - sizeof(header) - remaining), remaining);
            if (n > 0) {
                remaining -= n;
            } else if (n < 0 && errno == EAGAIN) {
                usleep(1000);
                retries--;
            } else {
                return -1;
            }
        }
        if (remaining > 0) return -1;
    }
    
    return 0;
}

fwm_connection_t* fwm_connect(const char* app_name) {
    printf("[libfwm] fwm_connect: Starting connection for %s\n", app_name ? app_name : "(null)");
    
    fwm_connection_t* conn = calloc(1, sizeof(fwm_connection_t));
    if (!conn) {
        printf("[libfwm] fwm_connect: calloc failed\n");
        return NULL;
    }
    
    //create unix domain socket
    printf("[libfwm] fwm_connect: Creating socket...\n");
    conn->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (conn->fd < 0) {
        printf("[libfwm] fwm_connect: socket() failed, fd=%d errno=%d\n", conn->fd, errno);
        free(conn);
        return NULL;
    }
    printf("[libfwm] fwm_connect: Socket created, fd=%d\n", conn->fd);
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, FWM_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    printf("[libfwm] fwm_connect: Connecting to %s...\n", FWM_SOCKET_PATH);
    if (connect(conn->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("[libfwm] fwm_connect: connect() failed, errno=%d\n", errno);
        close(conn->fd);
        free(conn);
        return NULL;
    }
    printf("[libfwm] fwm_connect: Connected successfully\n");
    
    //send connect message
    fwm_msg_connect_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = FWM_MSG_CONNECT;
    msg.header.length = sizeof(msg);
    msg.header.seq = ++conn->seq;
    msg.version = FWM_PROTOCOL_VERSION;
    strncpy(msg.app_name, app_name ? app_name : "FrostyApp", sizeof(msg.app_name) - 1);
    
    printf("[libfwm] fwm_connect: Sending connect message...\n");
    if (send_message(conn, &msg, sizeof(msg)) < 0) {
        printf("[libfwm] fwm_connect: send_message failed\n");
        close(conn->fd);
        free(conn);
        return NULL;
    }
    printf("[libfwm] fwm_connect: Connect message sent, waiting for reply...\n");
    
    //receive connect reply
    fwm_reply_connect_t reply;
    if (recv_message(conn, &reply, sizeof(reply)) < 0) {
        printf("[libfwm] fwm_connect: recv_message failed\n");
        close(conn->fd);
        free(conn);
        return NULL;
    }
    
    if (reply.header.type != FWM_REPLY_CONNECT_OK) {
        printf("[libfwm] fwm_connect: Bad reply type: %d (expected %d)\n", reply.header.type, FWM_REPLY_CONNECT_OK);
        close(conn->fd);
        free(conn);
        return NULL;
    }
    printf("[libfwm] fwm_connect: Connection established!\n");
    
    conn->client_id = reply.client_id;
    conn->screen_width = reply.screen_width;
    conn->screen_height = reply.screen_height;
    
    return conn;
}

void fwm_disconnect(fwm_connection_t* conn) {
    if (!conn) return;
    
    //detach all shared memory segments
    for (int i = 0; i < conn->num_windows; i++) {
        if (conn->windows[i].buffer) {
            shmdt(conn->windows[i].buffer);
        }
    }
    
    //send disconnect message
    fwm_msg_header_t msg;
    msg.type = FWM_MSG_DISCONNECT;
    msg.length = sizeof(msg);
    msg.client_id = conn->client_id;
    msg.seq = ++conn->seq;
    send_message(conn, &msg, sizeof(msg));
    
    close(conn->fd);
    free(conn);
}

int fwm_get_fd(fwm_connection_t* conn) {
    return conn ? conn->fd : -1;
}

uint32_t fwm_get_screen_width(fwm_connection_t* conn) {
    return conn ? conn->screen_width : 0;
}

uint32_t fwm_get_screen_height(fwm_connection_t* conn) {
    return conn ? conn->screen_height : 0;
}

fwm_window_t fwm_create_window(fwm_connection_t* conn, int32_t x, int32_t y,
                                uint32_t width, uint32_t height, const char* title) {
    if (!conn || conn->num_windows >= MAX_WINDOWS) return 0;
    
    fwm_msg_create_window_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = FWM_MSG_CREATE_WINDOW;
    msg.header.length = sizeof(msg);
    msg.header.client_id = conn->client_id;
    msg.header.seq = ++conn->seq;
    msg.x = x;
    msg.y = y;
    msg.width = width;
    msg.height = height;
    strncpy(msg.title, title ? title : "FrostyWindow", sizeof(msg.title) - 1);
    
    if (send_message(conn, &msg, sizeof(msg)) < 0) return 0;
    
    fwm_reply_window_created_t reply;
    if (recv_message(conn, &reply, sizeof(reply)) < 0 || 
        reply.header.type != FWM_REPLY_WINDOW_CREATED) {
        return 0;
    }
    
    //attach to shared memory
    int shm_id = shmget(reply.shm_key, width * height * 4, 0666);
    if (shm_id < 0) return 0;
    
    void* buffer = shmat(shm_id, NULL, 0);
    if (buffer == (void*)-1) return 0;
    
    //store window info
    fwm_window_info_t* winfo = &conn->windows[conn->num_windows++];
    winfo->id = reply.window_id;
    winfo->width = width;
    winfo->height = height;
    winfo->shm_key = reply.shm_key;
    winfo->buffer = buffer;
    winfo->shm_id = shm_id;
    
    return reply.window_id;
}

void fwm_destroy_window(fwm_connection_t* conn, fwm_window_t window) {
    if (!conn) return;
    
    //find and detach window buffer
    for (int i = 0; i < conn->num_windows; i++) {
        if (conn->windows[i].id == window) {
            if (conn->windows[i].buffer) {
                shmdt(conn->windows[i].buffer);
            }
            //remove from array
            memmove(&conn->windows[i], &conn->windows[i + 1],
                    (conn->num_windows - i - 1) * sizeof(fwm_window_info_t));
            conn->num_windows--;
            break;
        }
    }
    
    fwm_msg_window_op_t msg;
    msg.header.type = FWM_MSG_DESTROY_WINDOW;
    msg.header.length = sizeof(msg);
    msg.header.client_id = conn->client_id;
    msg.header.seq = ++conn->seq;
    msg.window_id = window;
    send_message(conn, &msg, sizeof(msg));
}

void fwm_show_window(fwm_connection_t* conn, fwm_window_t window) {
    if (!conn) return;
    
    fwm_msg_window_op_t msg;
    msg.header.type = FWM_MSG_SHOW_WINDOW;
    msg.header.length = sizeof(msg);
    msg.header.client_id = conn->client_id;
    msg.header.seq = ++conn->seq;
    msg.window_id = window;
    send_message(conn, &msg, sizeof(msg));
}

void fwm_hide_window(fwm_connection_t* conn, fwm_window_t window) {
    if (!conn) return;
    
    fwm_msg_window_op_t msg;
    msg.header.type = FWM_MSG_HIDE_WINDOW;
    msg.header.length = sizeof(msg);
    msg.header.client_id = conn->client_id;
    msg.header.seq = ++conn->seq;
    msg.window_id = window;
    send_message(conn, &msg, sizeof(msg));
}

void fwm_move_window(fwm_connection_t* conn, fwm_window_t window, int32_t x, int32_t y) {
    if (!conn) return;
    
    fwm_msg_move_window_t msg;
    msg.header.type = FWM_MSG_MOVE_WINDOW;
    msg.header.length = sizeof(msg);
    msg.header.client_id = conn->client_id;
    msg.header.seq = ++conn->seq;
    msg.window_id = window;
    msg.x = x;
    msg.y = y;
    send_message(conn, &msg, sizeof(msg));
}

void fwm_resize_window(fwm_connection_t* conn, fwm_window_t window, uint32_t width, uint32_t height) {
    if (!conn) return;
    
    fwm_msg_resize_window_t msg;
    msg.header.type = FWM_MSG_RESIZE_WINDOW;
    msg.header.length = sizeof(msg);
    msg.header.client_id = conn->client_id;
    msg.header.seq = ++conn->seq;
    msg.window_id = window;
    msg.width = width;
    msg.height = height;
    send_message(conn, &msg, sizeof(msg));
}

void fwm_set_title(fwm_connection_t* conn, fwm_window_t window, const char* title) {
    if (!conn) return;
    
    fwm_msg_set_title_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.header.type = FWM_MSG_SET_TITLE;
    msg.header.length = sizeof(msg);
    msg.header.client_id = conn->client_id;
    msg.header.seq = ++conn->seq;
    msg.window_id = window;
    strncpy(msg.title, title ? title : "", sizeof(msg.title) - 1);
    send_message(conn, &msg, sizeof(msg));
}

uint32_t* fwm_get_buffer(fwm_connection_t* conn, fwm_window_t window) {
    if (!conn) return NULL;
    
    for (int i = 0; i < conn->num_windows; i++) {
        if (conn->windows[i].id == window) {
            return (uint32_t*)conn->windows[i].buffer;
        }
    }
    return NULL;
}

void fwm_damage(fwm_connection_t* conn, fwm_window_t window, int32_t x, int32_t y,
                uint32_t width, uint32_t height) {
    if (!conn) return;
    
    fwm_msg_damage_t msg;
    msg.header.type = FWM_MSG_DAMAGE;
    msg.header.length = sizeof(msg);
    msg.header.client_id = conn->client_id;
    msg.header.seq = ++conn->seq;
    msg.window_id = window;
    msg.x = x;
    msg.y = y;
    msg.width = width;
    msg.height = height;
    send_message(conn, &msg, sizeof(msg));
}

void fwm_commit(fwm_connection_t* conn, fwm_window_t window) {
    if (!conn) return;
    
    fwm_msg_window_op_t msg;
    msg.header.type = FWM_MSG_COMMIT;
    msg.header.length = sizeof(msg);
    msg.header.client_id = conn->client_id;
    msg.header.seq = ++conn->seq;
    msg.window_id = window;
    send_message(conn, &msg, sizeof(msg));
}

int fwm_poll_event(fwm_connection_t* conn, fwm_event_t* event) {
    if (!conn || !event) return 0;
    
    fwm_msg_header_t msg;
    msg.type = FWM_MSG_POLL_EVENT;
    msg.length = sizeof(msg);
    msg.client_id = conn->client_id;
    msg.seq = ++conn->seq;
    
    if (send_message(conn, &msg, sizeof(msg)) < 0) return 0;
    
    fwm_msg_event_t reply;
    if (recv_message(conn, &reply, sizeof(reply)) < 0) return 0;
    
    if (reply.header.type == FWM_REPLY_NO_EVENT) {
        return 0;
    }
    
    if (reply.header.type != FWM_REPLY_EVENT) return 0;
    
    //convert protocol event to client event
    event->type = reply.event_type;
    event->window = reply.window_id;
    memcpy(&event->data, &reply.data, sizeof(event->data));
    
    return 1;
}

int fwm_wait_event(fwm_connection_t* conn, fwm_event_t* event) {
    //Simple blocking wait - just poll repeatedly
    //TODO: improve with select/poll
    while (1) {
        if (fwm_poll_event(conn, event)) return 1;
        usleep(1000); //1ms sleep
    }
}

void fwm_flush(fwm_connection_t* conn) {
    if (!conn) return;
    //nothing to do for now - all operations are synchronous
}
