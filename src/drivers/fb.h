#ifndef FB_H
#define FB_H

#include <stdint.h>
#include "../device_manager.h"

//fb0 IOCTLs
#define FB_IOCTL_BLIT 0x0001u
#define FB_IOCTL_SET_CONSOLE 0x0002u  //enable (1) or disable (0) console output

typedef struct fb_blit_args {
    uint32_t x;           //destination X in pixels
    uint32_t y;           //destination Y in pixels
    uint32_t w;           //width in pixels
    uint32_t h;           //height in pixels
    uint32_t src_pitch;   //bytes per row in src buffer
    uint32_t flags;       //0 = raw/native bpp 1 = 8-bit grayscale
    const void* src;      //pointer to source pixels
} fb_blit_args_t;

#ifdef __cplusplus
extern "C" {
#endif

//initialize framebuffer device from VBE mode info
//returns 0 on success -1 if not initialized
int fb_register_from_vbe(uint32_t phys_base, uint32_t width, uint32_t height, uint32_t bpp, uint32_t pitch);

//query mapped framebuffer info (returns 0 on success)
int fb_get_info(uint8_t** out_virt, uint32_t* out_w, uint32_t* out_h, uint32_t* out_bpp, uint32_t* out_pitch);

#ifdef __cplusplus
}
#endif

#endif
