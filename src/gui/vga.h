#ifndef GUI_VGA_H
#define GUI_VGA_H

#include <stdint.h>

#define VGA_WIDTH   320
#define VGA_HEIGHT  200
#define VGA_ADDRESS 0xA0000

extern uint8_t* const VGA;

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

//double buffering utilities
//set the active draw surface pass NULL or VGA to draw directly to vram
void vga_set_draw_surface(uint8_t* surface);

//block until vertical retrace and then copy the provided surface to vram
void vga_present(const uint8_t* surface);

//wait for vertical retrace period
void vga_wait_vsync(void);

//vsync control (runtime A/B testing)
void vga_set_vsync_enabled(int enabled);
int  vga_get_vsync_enabled(void);

#endif
