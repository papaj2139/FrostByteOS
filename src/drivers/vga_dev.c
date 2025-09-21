#include "vga_dev.h"
#include "../device_manager.h"
#include "../mm/heap.h"
#include "../libc/string.h"
#include "../gui/vga.h"

//VGA framebuffer device: /dev/vga0
//-write linear pixels (one byte per pixel color index) at given byte offset
//-offset is interpreted as pixel index in current mode (w*h)
//-after write we blit to VRAM: if single row span present that row rect else full present

static device_t g_vga_dev;
static uint8_t* g_fb = NULL;            //staging buffer (linear chunky buffer)
static uint32_t g_fb_capacity = 0;      //bytes allocated

static void vga_ensure_buffer(void) {
    //max mode is 640x480 so 307200 bytes
    const uint32_t need = 640u * 480u;
    if (!g_fb || g_fb_capacity < need) {
        if (g_fb) kfree(g_fb);
        g_fb = (uint8_t*)kmalloc(need);
        if (g_fb) {
            g_fb_capacity = need;
            memset(g_fb, 0, g_fb_capacity);
        } else {
            g_fb_capacity = 0;
        }
    }
}

static int vga_dev_init(struct device* d) {
    (void)d;
    vga_ensure_buffer();
    if (!g_fb) return -1;
    //use staging buffer as draw surface so writes to g_fb can be presented
    vga_set_draw_surface(g_fb);
    return 0;
}

static int vga_dev_read(struct device* d, uint32_t off, void* buf, uint32_t sz) {
    (void)d; (void)off; (void)buf; (void)sz; 
    return -1; //not supported
}

static int vga_dev_write(struct device* d, uint32_t off, const void* buf, uint32_t sz) {
    (void)d;
    if (!buf || sz == 0) return 0;
    vga_ensure_buffer();
    if (!g_fb) return -1;
    int w = vga_width();
    int h = vga_height();
    if (w <= 0 || h <= 0) return -1;
    uint32_t max_bytes = (uint32_t)w * (uint32_t)h; //one byte per pixel index
    if (off >= max_bytes) return 0; //nothing to write
    uint32_t to_copy = sz;
    if (off + to_copy > max_bytes) to_copy = max_bytes - off;
    memcpy(g_fb + off, buf, to_copy);

    //present: if single-row span present that rectangle otherwise present full frame
    uint32_t start_px = off;
    uint32_t end_px = off + to_copy - 1;
    uint32_t y0 = start_px / (uint32_t)w;
    uint32_t y1 = end_px   / (uint32_t)w;
    if (y0 == y1) {
        int x = (int)(start_px % (uint32_t)w);
        int y = (int)y0;
        int rw = (int)to_copy;
        if (x + rw > w) rw = w - x;
        if (rw > 0) vga_present_rect(x, y, rw, 1, g_fb);
    } else {
        vga_present(g_fb);
    }
    return (int)to_copy;
}

static int vga_dev_ioctl(struct device* d, uint32_t cmd, void* arg) { 
    (void)d; (void)cmd; (void)arg; 
    return -1; 
}

static void vga_dev_cleanup(struct device* d) { 
    (void)d;
    //keep buffer for reuse
}

static const device_ops_t vga_ops = {
    .init = vga_dev_init,
    .read = vga_dev_read,
    .write = vga_dev_write,
    .ioctl = vga_dev_ioctl,
    .cleanup = vga_dev_cleanup,
};

int vga_device_register(void) {
    memset(&g_vga_dev, 0, sizeof(g_vga_dev));
    strcpy(g_vga_dev.name, "vga0");
    g_vga_dev.type = DEVICE_TYPE_OUTPUT;
    g_vga_dev.subtype = DEVICE_SUBTYPE_DISPLAY;
    g_vga_dev.status = DEVICE_STATUS_UNINITIALIZED;
    g_vga_dev.ops = &vga_ops;
    if (device_register(&g_vga_dev) != 0) return -1;
    if (device_init(&g_vga_dev) != 0) {
        device_unregister(g_vga_dev.device_id);
        return -1;
    }
    g_vga_dev.status = DEVICE_STATUS_READY;
    return 0;
}

void vga_device_on_mode_changed(void) {
    vga_ensure_buffer();
    if (g_fb) {
        //after mode change keep using g_fb as draw surface and clear it
        memset(g_fb, 0, g_fb_capacity);
        vga_set_draw_surface(g_fb);
        vga_present(g_fb);
    }
}
