#ifndef GUI_VGA_H
#define GUI_VGA_H

#include <stdint.h>

#define VGA_WIDTH   320
#define VGA_HEIGHT  200
#define VGA_ADDRESS 0xA0000

extern uint8_t* const VGA;

//current VGA mode state
typedef enum {
    VGA_MODE_13H = 0, //320x200x256 (chunky 8bpp)
    VGA_MODE_12H = 1, //640x480x16  (planar 4bpp)
    VGA_MODE_TEXT = 2 //80x25 text mode (character/attribute)
} vga_mode_t;

//mode management
void vga_set_mode(vga_mode_t mode);
vga_mode_t vga_get_mode(void);
int vga_width(void);
int vga_height(void);

//pixel access
void putpx(int x, int y, uint8_t color);
uint8_t getpx(int x, int y);

//text rendering
void draw_char_small(int x, int y, char ch, uint8_t color);
void draw_string_small(int x, int y, const char *str, uint8_t color);
void draw_char(int x, int y, char ch, uint8_t color);
void draw_string(int x, int y, const char *str, uint8_t color);

//shapes
void draw_rect(int x, int y, int w, int h, uint8_t color);

//mode set 
void vga_set_mode_13h(void);
void vga_set_mode_12h(void);
void vga_set_text_mode(void);

//double buffering utilities
//set the active draw surface pass NULL or VGA to draw directly to vram
void vga_set_draw_surface(uint8_t* surface);

//block until vertical retrace and then copy the provided surface to vram
void vga_present(const uint8_t* surface);
//copy only a rectangular region from the surface to vram
void vga_present_rect(int x, int y, int w, int h, const uint8_t* surface);

//wait for vertical retrace period
void vga_wait_vsync(void);

//vsync control (runtime A/B testing)
void vga_set_vsync_enabled(int enabled);
int  vga_get_vsync_enabled(void);

#endif
