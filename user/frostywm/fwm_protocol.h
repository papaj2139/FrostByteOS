#ifndef FWM_PROTOCOL_H
#define FWM_PROTOCOL_H

#include <stdint.h>

//message types (client -> server)
typedef enum {
    FWM_MSG_CONNECT = 1,
    FWM_MSG_DISCONNECT,
    FWM_MSG_CREATE_WINDOW,
    FWM_MSG_DESTROY_WINDOW,
    FWM_MSG_SHOW_WINDOW,
    FWM_MSG_HIDE_WINDOW,
    FWM_MSG_MOVE_WINDOW,
    FWM_MSG_RESIZE_WINDOW,
    FWM_MSG_SET_TITLE,
    FWM_MSG_DAMAGE,
    FWM_MSG_COMMIT,
    FWM_MSG_POLL_EVENT,
} fwm_msg_type_t;

//message types (server -> client)
typedef enum {
    FWM_REPLY_OK = 100,
    FWM_REPLY_ERROR,
    FWM_REPLY_CONNECT_OK,
    FWM_REPLY_WINDOW_CREATED,
    FWM_REPLY_EVENT,
    FWM_REPLY_NO_EVENT,
} fwm_reply_type_t;

//generic message header
typedef struct {
    uint32_t type;      //fwm_msg_type_t or fwm_reply_type_t
    uint32_t length;    //total message length including header
    uint32_t client_id; //client identifier
    uint32_t seq;       //sequence number for request/reply matching
} fwm_msg_header_t;

//connect message
typedef struct {
    fwm_msg_header_t header;
    uint32_t version;   //protocol version
    char app_name[64];  //application name
} fwm_msg_connect_t;

//connect reply
typedef struct {
    fwm_msg_header_t header;
    uint32_t client_id;
    uint32_t screen_width;
    uint32_t screen_height;
} fwm_reply_connect_t;

//create window message
typedef struct {
    fwm_msg_header_t header;
    int32_t x, y;
    uint32_t width, height;
    char title[128];
} fwm_msg_create_window_t;

//window created reply
typedef struct {
    fwm_msg_header_t header;
    uint32_t window_id;
    uint32_t shm_key;   //shared memory key for window buffer
} fwm_reply_window_created_t;

//window operation messages
typedef struct {
    fwm_msg_header_t header;
    uint32_t window_id;
} fwm_msg_window_op_t;

//move window message
typedef struct {
    fwm_msg_header_t header;
    uint32_t window_id;
    int32_t x, y;
} fwm_msg_move_window_t;

//resize window message
typedef struct {
    fwm_msg_header_t header;
    uint32_t window_id;
    uint32_t width, height;
} fwm_msg_resize_window_t;

//set title message
typedef struct {
    fwm_msg_header_t header;
    uint32_t window_id;
    char title[128];
} fwm_msg_set_title_t;

//damage message
typedef struct {
    fwm_msg_header_t header;
    uint32_t window_id;
    int32_t x, y;
    uint32_t width, height;
} fwm_msg_damage_t;

//event message (server -> client)
typedef struct {
    fwm_msg_header_t header;
    uint32_t event_type;    //fwm_event_type_t
    uint32_t window_id;
    union {
        struct {
            int32_t x, y;
            int32_t rel_x, rel_y;
        } motion;
        struct {
            uint8_t button;
            int32_t x, y;
        } button;
        struct {
            uint32_t keycode;
            char ascii;
        } key;
        struct {
            int32_t x, y;
            uint32_t width, height;
        } configure;
    } data;
} fwm_msg_event_t;

//protocol constants
#define FWM_PROTOCOL_VERSION 1
#define FWM_SOCKET_PATH "/tmp/.frostywm-socket"

#endif
