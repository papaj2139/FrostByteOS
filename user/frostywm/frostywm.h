#ifndef FROSTYWM_H
#define FROSTYWM_H

#include <stdint.h>

//forward declarations
typedef struct fwm_connection fwm_connection_t;
typedef struct fwm_window fwm_window_t;
typedef struct fwm_surface fwm_surface_t;

//event types
typedef enum {
    FWM_EVENT_NONE = 0,
    FWM_EVENT_MOUSE_MOTION,
    FWM_EVENT_MOUSE_BUTTON_PRESS,
    FWM_EVENT_MOUSE_BUTTON_RELEASE,
    FWM_EVENT_KEY_PRESS,
    FWM_EVENT_KEY_RELEASE,
    FWM_EVENT_WINDOW_CLOSE,
    FWM_EVENT_WINDOW_CONFIGURE,
    FWM_EVENT_WINDOW_FOCUS,
    FWM_EVENT_WINDOW_UNFOCUS,
} fwm_event_type_t;

//mouse button codes
#define FWM_BUTTON_LEFT   0x01
#define FWM_BUTTON_RIGHT  0x02
#define FWM_BUTTON_MIDDLE 0x04

//event structures
typedef struct {
    fwm_event_type_t type;
    fwm_window_t* window;
    union {
        struct {
            int x, y;           //absolute screen coordinates
            int rel_x, rel_y;   //relative motion
        } motion;
        struct {
            uint8_t button;
            int x, y;
        } button;
        struct {
            uint32_t keycode;
            char ascii;
        } key;
        struct {
            int x, y;
            int width, height;
        } configure;
    };
} fwm_event_t;

//color type (ARGB8888)
typedef uint32_t fwm_color_t;

//helper macros for colors
#define FWM_RGB(r, g, b) (0xFF000000u | ((r) << 16) | ((g) << 8) | (b))
#define FWM_RGBA(r, g, b, a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

//connection management
fwm_connection_t* fwm_connect(void);
void fwm_disconnect(fwm_connection_t* conn);
int fwm_get_fd(fwm_connection_t* conn);

//display information
void fwm_get_screen_size(fwm_connection_t* conn, int* width, int* height);

//window management
fwm_window_t* fwm_create_window(fwm_connection_t* conn, int x, int y, int width, int height, const char* title);
void fwm_destroy_window(fwm_window_t* win);
void fwm_show_window(fwm_window_t* win);
void fwm_hide_window(fwm_window_t* win);
void fwm_move_window(fwm_window_t* win, int x, int y);
void fwm_resize_window(fwm_window_t* win, int width, int height);
void fwm_set_window_title(fwm_window_t* win, const char* title);

//surface/buffer management
fwm_surface_t* fwm_window_get_surface(fwm_window_t* win);
uint32_t* fwm_surface_get_buffer(fwm_surface_t* surf, int* width, int* height, int* stride);
void fwm_surface_damage(fwm_surface_t* surf, int x, int y, int width, int height);
void fwm_surface_commit(fwm_surface_t* surf);

//drawing helpers (convenience functions)
void fwm_draw_rect(uint32_t* buffer, int buf_width, int x, int y, int w, int h, fwm_color_t color);
void fwm_draw_filled_rect(uint32_t* buffer, int buf_width, int x, int y, int w, int h, fwm_color_t color);
void fwm_draw_text(uint32_t* buffer, int buf_width, int x, int y, const char* text, fwm_color_t color);

//event handling
int fwm_poll_event(fwm_connection_t* conn, fwm_event_t* event);
int fwm_wait_event(fwm_connection_t* conn, fwm_event_t* event);

//cursor control
void fwm_set_cursor_visible(fwm_connection_t* conn, int visible);
void fwm_get_cursor_position(fwm_connection_t* conn, int* x, int* y);

#endif
