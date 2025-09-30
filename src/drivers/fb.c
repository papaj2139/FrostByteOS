#include "fb.h"
#include "../mm/vmm.h"
#include "../mm/heap.h"
#include <string.h>

//linear framebuffer driver /dev/fb0 (assumes XRGB8888 if bpp==32)
static uint8_t* g_fb_virt = 0;
static uint32_t g_fb_phys = 0;
static uint32_t g_w = 0, g_h = 0, g_bpp = 0, g_pitch = 0;
static device_t g_fb_dev;

static int fb_dev_init(struct device* d) {
    (void)d;
    return (g_fb_virt && g_w && g_h) ? 0 : -1;
}

static int fb_dev_read(struct device* d, uint32_t off, void* buf, uint32_t sz) {
    (void)d;
    (void)off;
    (void)buf;
    (void)sz;
    return -1;
}

static int fb_dev_ioctl(struct device* d, uint32_t cmd, void* arg) {
    (void)d;
    if (!g_fb_virt || !g_w || !g_h) return -1;
    if (cmd == FB_IOCTL_BLIT) {
        if (!arg) return -1;
        fb_blit_args_t* a = (fb_blit_args_t*)arg;
        if (!a->src) return -1;
        //clamp rect
        uint32_t x = a->x, y = a->y, w = a->w, h = a->h;
        if (x >= g_w || y >= g_h || w == 0 || h == 0) return 0;
        if (x + w > g_w) w = g_w - x;
        if (y + h > g_h) h = g_h - y;
        uint32_t bpp_bytes = g_bpp / 8u;
        uint32_t src_pitch = a->src_pitch;
        if (a->flags == 0) {
            //raw copy src contains native bpp scanlines
            if (src_pitch == 0) src_pitch = w * bpp_bytes;
            for (uint32_t row = 0; row < h; row++) {
                const uint8_t* src = (const uint8_t*)a->src + row * src_pitch;
                uint8_t* dst = g_fb_virt + (y + row) * g_pitch + x * bpp_bytes;
                //copy exactly w pixels worth of bytes
                uint32_t bytes = w * bpp_bytes;
                memcpy(dst, src, bytes);
            }
            return 0;
        } else if (a->flags == 1) {
            //8-bit grayscale to native format
            if (src_pitch == 0) src_pitch = a->w; //original requested width bytes
            for (uint32_t row = 0; row < h; row++) {
                const uint8_t* src = (const uint8_t*)a->src + row * src_pitch;
                uint8_t* dst = g_fb_virt + (y + row) * g_pitch + x * bpp_bytes;
                if (g_bpp == 32) {
                    uint32_t* p32 = (uint32_t*)dst;
                    for (uint32_t col = 0; col < w; col++) {
                        uint32_t v = src[col];
                        uint32_t c = (v << 16) | (v << 8) | v; //0xRRGGBB
                        p32[col] = c;
                    }
                } else if (g_bpp == 24) {
                    for (uint32_t col = 0; col < w; col++) {
                        uint32_t v = src[col];
                        uint32_t off = col * 3u;
                        dst[off+0] = (uint8_t)v; //B
                        dst[off+1] = (uint8_t)v; //G
                        dst[off+2] = (uint8_t)v; //R
                    }
                } else if (g_bpp == 16) {
                    uint16_t* p16 = (uint16_t*)dst;
                    for (uint32_t col = 0; col < w; col++) {
                        uint32_t v = src[col];
                        uint16_t c = (uint16_t)(((v>>3)<<11) | ((v>>2)<<5) | (v>>3));
                        p16[col] = c;
                    }
                } else {
                    //unsupported
                    return -1;
                }
            }
            return 0;
        }
        return -1;
    }
    return -1;
}
static void fb_dev_cleanup(struct device* d) {
    (void)d;
}

static int fb_dev_write(struct device* d, uint32_t off, const void* buf, uint32_t sz) {
    (void)d;
    if (!buf || sz == 0 || !g_fb_virt) return 0;
    uint32_t fb_size = g_pitch * g_h;
    //fast path: full-frame write ignores 'off' and replaces the entire framebuffer
    if (sz >= fb_size) {
        memcpy(g_fb_virt, buf, fb_size);
        return (int)fb_size;
    }
    //otherwise honor offset within bounds
    if (off >= fb_size) return 0;
    uint32_t to_copy = sz;
    if (off + to_copy > fb_size) to_copy = fb_size - off;
    memcpy(g_fb_virt + off, buf, to_copy);
    return (int)to_copy;
}

static const device_ops_t fb_ops = {
    .init = fb_dev_init,
    .read = fb_dev_read,
    .write = fb_dev_write,
    .ioctl = fb_dev_ioctl,
    .cleanup = fb_dev_cleanup,
};

int fb_register_from_vbe(uint32_t phys_base, uint32_t width, uint32_t height, uint32_t bpp, uint32_t pitch) {
    if (!phys_base || !width || !height || !bpp) return -1;
    //map framebuffer into kernel virtual space at a fixed VA
    const uint32_t FB_VIRT_BASE = 0xD0000000; //pick an unused high-half VA region
    uint32_t bytes = pitch ? (pitch * height) : (width * (bpp/8) * height);
    uint32_t pages = (bytes + 4095) / 4096;

    for (uint32_t i = 0; i < pages; i++) {
        uint32_t pa = phys_base + i * 4096;
        uint32_t va = FB_VIRT_BASE + i * 4096;
        if (vmm_map_page(va, pa, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            return -1;
        }
    }

    g_fb_virt = (uint8_t*)FB_VIRT_BASE;
    g_fb_phys = phys_base;
    g_w = width;
    g_h = height;
    g_bpp = bpp;
    g_pitch = pitch ? pitch : (width * (bpp/8));

    memset(&g_fb_dev, 0, sizeof(g_fb_dev));
    strcpy(g_fb_dev.name, "fb0");
    g_fb_dev.type = DEVICE_TYPE_OUTPUT;
    g_fb_dev.subtype = DEVICE_SUBTYPE_DISPLAY;
    g_fb_dev.status = DEVICE_STATUS_UNINITIALIZED;
    g_fb_dev.ops = &fb_ops;

    if (device_register(&g_fb_dev) != 0) return -1;
    if (device_init(&g_fb_dev) != 0) {
        device_unregister(g_fb_dev.device_id);
        return -1;
    }
    g_fb_dev.status = DEVICE_STATUS_READY;
    return 0;
}

int fb_get_info(uint8_t** out_virt, uint32_t* out_w, uint32_t* out_h, uint32_t* out_bpp, uint32_t* out_pitch) {
    if (!g_fb_virt || !g_w || !g_h || !g_bpp) return -1;
    if (out_virt) *out_virt = g_fb_virt;
    if (out_w) *out_w = g_w;
    if (out_h) *out_h = g_h;
    if (out_bpp) *out_bpp = g_bpp;
    if (out_pitch) *out_pitch = g_pitch;
    return 0;
}
