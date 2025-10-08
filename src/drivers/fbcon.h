#ifndef FBCON_H
#define FBCON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//initialize fb console if fb0 present returns 0 on success
int fbcon_init(void);
int fbcon_available(void);
void fbcon_clear_with_attr(unsigned char attr);
int fbcon_putchar(char c, unsigned char attr);
//write string with ANSI escape code support
int fbcon_write(const char* buf, uint32_t size);
//reload PSF font from /etc/font.psf after VFS is mounted
int fbcon_reload_font(void);
//enable (1) or disable (0) the blinking text cursor in framebuffer console
int fbcon_set_cursor_enabled(int enable);
//get current cursor position
void fbcon_get_cursor(int* x, int* y);
//set cursor position
void fbcon_set_cursor(int x, int y);
//enable (1) or disable (0) console output completely
void fbcon_set_enabled(int enable);

#ifdef __cplusplus
}
#endif

#endif
