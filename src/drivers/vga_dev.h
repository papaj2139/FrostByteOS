#ifndef VGA_DEV_H
#define VGA_DEV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//register /dev/vga0 device backed by VGA framebuffer
int vga_device_register(void);

//notify the VGA device driver that mode changed so it can resize buffers
void vga_device_on_mode_changed(void);

#ifdef __cplusplus
}
#endif

#endif
