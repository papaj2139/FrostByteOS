#ifndef LIBFWM_H
#define LIBFWM_H

#include <stdint.h>

//opaque connection handle
typedef struct fwm_connection fwm_connection_t;

//window handle
typedef uint32_t fwm_window_t;

//event types
typedef enum {
    FWM_EVENT_NONE = 0,
    FWM_EVENT_KEY_PRESS,
    FWM_EVENT_KEY_RELEASE,
    FWM_EVENT_BUTTON_PRESS,
    FWM_EVENT_BUTTON_RELEASE,
    FWM_EVENT_MOTION,
    FWM_EVENT_ENTER,
    FWM_EVENT_LEAVE,
    FWM_EVENT_FOCUS_IN,
    FWM_EVENT_FOCUS_OUT,
    FWM_EVENT_EXPOSE,
    FWM_EVENT_CONFIGURE,
    FWM_EVENT_CLOSE,
} fwm_event_type_t;

//event structure
typedef struct {
    fwm_event_type_t type;
    fwm_window_t window;
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
        struct {
            int32_t x, y;
            uint32_t width, height;
        } expose;
    } data;
} fwm_event_t;

//connection management
fwm_connection_t* fwm_connect(const char* app_name);
void fwm_disconnect(fwm_connection_t* conn);
int fwm_get_fd(fwm_connection_t* conn);

//screen information
uint32_t fwm_get_screen_width(fwm_connection_t* conn);
uint32_t fwm_get_screen_height(fwm_connection_t* conn);

//window management
fwm_window_t fwm_create_window(fwm_connection_t* conn, int32_t x, int32_t y,
                                uint32_t width, uint32_t height, const char* title);
void fwm_destroy_window(fwm_connection_t* conn, fwm_window_t window);
void fwm_show_window(fwm_connection_t* conn, fwm_window_t window);
void fwm_hide_window(fwm_connection_t* conn, fwm_window_t window);
void fwm_move_window(fwm_connection_t* conn, fwm_window_t window, int32_t x, int32_t y);
void fwm_resize_window(fwm_connection_t* conn, fwm_window_t window, uint32_t width, uint32_t height);
void fwm_set_title(fwm_connection_t* conn, fwm_window_t window, const char* title);

//window buffer access
uint32_t* fwm_get_buffer(fwm_connection_t* conn, fwm_window_t window);
void fwm_damage(fwm_connection_t* conn, fwm_window_t window, int32_t x, int32_t y,
                uint32_t width, uint32_t height);
void fwm_commit(fwm_connection_t* conn, fwm_window_t window);

//event handling
int fwm_poll_event(fwm_connection_t* conn, fwm_event_t* event);
int fwm_wait_event(fwm_connection_t* conn, fwm_event_t* event);

//utility functions
void fwm_flush(fwm_connection_t* conn);

#endif
