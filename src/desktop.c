#include <stdint.h>
#include "desktop.h"

#define VGA_WIDTH   320
#define VGA_HEIGHT  200
#define VGA_ADDRESS 0xA0000

static uint8_t* const VGA = (uint8_t*)VGA_ADDRESS;

static inline void outb(uint16_t port, uint8_t val){
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port){
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define CURSOR_W 16
#define CURSOR_H 24

static const uint8_t cursor_bitmap[CURSOR_H][CURSOR_W] = {
    {15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15,15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15,15,15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15,15,15,15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15,15,15,15,15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15,15,15,15,15,15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15,15,15,15,15,15,15, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15,15,15,15,15,15,15,15, 0, 0, 0, 0, 0, 0, 0, 0},
    {15,15,15,15,15,15,15,15,15, 0, 0, 0, 0, 0, 0, 0},
    {15,15,15,15,15,15,0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15, 0,0,15,15,15,0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0,0,15,15,15, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0,15,15,15, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0,15,15,0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 15,15,15, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 15,15, 0, 0, 0, 0, 0, 0, 0, 0},
};


static uint8_t cursor_bg[CURSOR_H][CURSOR_W];

static inline void putpx(int x, int y, uint8_t color) {
    if ((unsigned)x < VGA_WIDTH && (unsigned)y < VGA_HEIGHT) {
        VGA[y * VGA_WIDTH + x] = color;
    }
}
static inline uint8_t getpx(int x, int y) {
    if ((unsigned)x < VGA_WIDTH && (unsigned)y < VGA_HEIGHT) {
        return VGA[y * VGA_WIDTH + x];
    }
    return 0;
}

static void save_cursor_bg(int x, int y) {
    for (int r = 0; r < CURSOR_H; r++) {
        for (int c = 0; c < CURSOR_W; c++) {
            int px = x + c, py = y + r;
            cursor_bg[r][c] = getpx(px, py);
        }
    }
}
static void restore_cursor_bg(int x, int y) {
    for (int r = 0; r < CURSOR_H; r++) {
        for (int c = 0; c < CURSOR_W; c++) {
            int px = x + c, py = y + r;
            uint8_t col = cursor_bg[r][c];
            putpx(px, py, col);
        }
    }
}

static void draw_cursor(int x, int y) {
    for (int r = 0; r < CURSOR_H; r++) {
        for (int c = 0; c < CURSOR_W; c++) {
            uint8_t col = cursor_bitmap[r][c];
            if (col != 0) {
                putpx(x + c, y + r, col);
            }
        }
    }
}

static void vga_set_mode_13h(void) {
    outb(0x3C2, 0x63);
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);
    outb(0x3C4, 0x01); outb(0x3C5, 0x01);
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F);
    outb(0x3C4, 0x03); outb(0x3C5, 0x00);
    outb(0x3C4, 0x04); outb(0x3C5, 0x0E);

    static const uint8_t crtc[] = {
        0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,
        0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,
        0x9C,0x0E,0x8F,0x28,0x40,0x96,0xB9,0xA3,
        0xFF
    };
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & ~0x80);

    for (uint8_t i = 0; i < sizeof(crtc); i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
    }

    outb(0x3CE, 0x00); outb(0x3CF, 0x00);
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    outb(0x3CE, 0x02); outb(0x3CF, 0x00);
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x40);
    outb(0x3CE, 0x06); outb(0x3CF, 0x05);
    outb(0x3CE, 0x07); outb(0x3CF, 0x0F);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);

    for (uint8_t i = 0; i < 16; i++) {
        inb(0x3DA);
        outb(0x3C0, i);
        outb(0x3C0, i);
    }
    inb(0x3DA);
    outb(0x3C0, 0x10); outb(0x3C0, 0x41);
    inb(0x3DA);
    outb(0x3C0, 0x11); outb(0x3C0, 0x00);
    inb(0x3DA);
    outb(0x3C0, 0x12); outb(0x3C0, 0x0F);
    inb(0x3DA);
    outb(0x3C0, 0x13); outb(0x3C0, 0x00);
    inb(0x3DA);
    outb(0x3C0, 0x14); outb(0x3C0, 0x00);
    inb(0x3DA);
    outb(0x3C0, 0x20);
}

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) { if (inb(0x64) & 1) return; }
    } else {
        while (timeout--) { if ((inb(0x64) & 2) == 0) return; }
    }
}
static void mouse_write(uint8_t val) {
    mouse_wait(1); outb(0x64, 0xD4);
    mouse_wait(1); outb(0x60, val);
}
static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}
static void mouse_init(void) {
    mouse_wait(1); outb(0x64, 0xA8);
    mouse_wait(1); outb(0x64, 0x20);
    mouse_wait(0); uint8_t status = inb(0x60) | 2;
    mouse_wait(1); outb(0x64, 0x60);
    mouse_wait(1); outb(0x60, status);

    mouse_write(0xF6); (void)mouse_read();
    mouse_write(0xF4); (void)mouse_read();
}
static uint8_t mcycle = 0;
static int8_t  mbytes[3];

static int poll_mouse_packet(void) {
    if (!(inb(0x64) & 1)) return 0;
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) { (void)inb(0x60); return 0; }
    uint8_t data = inb(0x60);

    if (mcycle == 0 && !(data & 0x08)) return 0;
    mbytes[mcycle++] = (int8_t)data;
    if (mcycle == 3) { mcycle = 0; return 1; }
    return 0;
}

void cmd_desktop(const char *args) {
    (void)args;

    vga_set_mode_13h();

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) VGA[i] = 3;

    int cx = VGA_WIDTH / 2 - CURSOR_W / 2;
    int cy = VGA_HEIGHT / 2 - CURSOR_H / 2;

    save_cursor_bg(cx, cy);
    draw_cursor(cx, cy);

    mouse_init();

    for (;;) {
        int old_cx = cx;
        int old_cy = cy;

        if (poll_mouse_packet()) {
            old_cx = cx;
            old_cy = cy;

            cx += mbytes[1];
            cy -= mbytes[2];

            if (cx < 0) cx = 0;
            if (cy < 0) cy = 0;
            if (cx > VGA_WIDTH  - CURSOR_W) cx = VGA_WIDTH  - CURSOR_W;
            if (cy > VGA_HEIGHT - CURSOR_H) cy = VGA_HEIGHT - CURSOR_H;

            restore_cursor_bg(old_cx, old_cy);

            save_cursor_bg(cx, cy);
            draw_cursor(cx, cy);
        }
    }
}
