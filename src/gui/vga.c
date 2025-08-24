#include <stdint.h>
#include "vga.h"
#include "../font.h"
#include "../io.h"

uint8_t* const VGA = (uint8_t*)VGA_ADDRESS;
static uint8_t* g_draw_surface = (uint8_t*)VGA_ADDRESS; //defaults to vram
static int g_vsync_enabled = 1; // runtime toggle

void putpx(int x, int y, uint8_t color) {
    if ((unsigned)x < VGA_WIDTH && (unsigned)y < VGA_HEIGHT)
        g_draw_surface[y * VGA_WIDTH + x] = color;
}

uint8_t getpx(int x, int y) {
    if ((unsigned)x < VGA_WIDTH && (unsigned)y < VGA_HEIGHT)
        return g_draw_surface[y * VGA_WIDTH + x];
    return 0;
}

void draw_char_small(int x, int y, char ch, uint8_t color) {
    if ((unsigned)ch >= 128) return;
    const uint8_t *glyph = font8x8[(int)ch];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 6; col++) {
            if (bits & (1 << col))
                putpx(x + col, y + row, color);
        }
    }
}

void draw_string_small(int x, int y, const char *str, uint8_t color) {
    while (*str) {
        draw_char_small(x, y, *str, color);
        x += 8;
        str++;
    }
}

void draw_rect(int x, int y, int w, int h, uint8_t color) {
    int x_end = x + w;
    int y_end = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x_end > VGA_WIDTH) x_end = VGA_WIDTH;
    if (y_end > VGA_HEIGHT) y_end = VGA_HEIGHT;

    for (int j = y; j < y_end; j++)
        for (int i = x; i < x_end; i++)
            putpx(i, j, color);
}

void draw_char(int x, int y, char ch, uint8_t color) {
    if ((unsigned)ch >= 128) return;
    const uint8_t *glyph = font8x8[(int)ch];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++)
            if (bits & (1 << col))
                putpx(x + col, y + row, color);
    }
}

void draw_string(int x, int y, const char *str, uint8_t color) {
    while (*str) {
        draw_char(x, y, *str++, color);
        x += 8;
    }
}

void vga_set_mode_13h(void) {
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

void vga_set_mode_12h(void) {
    __asm__ volatile (
        "mov $0x03, %%ax\n"
        "int $0x10"
        :
        :
        : "ax"
    );
    //just restart 
}

void vga_set_draw_surface(uint8_t* surface){
    g_draw_surface = surface ? surface : (uint8_t*)VGA_ADDRESS;
}

void vga_set_vsync_enabled(int enabled){
    g_vsync_enabled = enabled ? 1 : 0;
}

int vga_get_vsync_enabled(void){
    return g_vsync_enabled;
}

void vga_wait_vsync(void){
    //wait for end of vertical retrace
    while (inb(0x3DA) & 0x08) { }
    //wait for start of vertical retrace
    while (!(inb(0x3DA) & 0x08)) { }
}

void vga_present(const uint8_t* surface){
    if (!surface) surface = g_draw_surface;
    if (surface == VGA) return;
    if (g_vsync_enabled) vga_wait_vsync();
    const uint8_t* src8 = surface;
    uint8_t* dst8 = VGA;
    unsigned count = VGA_WIDTH * VGA_HEIGHT; //64000 bytes

    //align destination to 4 bytes for dword copy
    while (((uintptr_t)dst8 & 3) && count) {
        *dst8++ = *src8++;
        --count;
    }

    unsigned dwords = count >> 2;
    unsigned tail = count & 3;

    if (dwords) {
        const void* s = src8;
        void* d = dst8;
        unsigned n = dwords;
        __asm__ volatile (
            "cld\n\trep movsd"
            : "+S"(s), "+D"(d), "+c"(n)
            :
            : "memory"
        );
        src8 = (const uint8_t*)s;
        dst8 = (uint8_t*)d;
    }

    if (tail) {
        const void* s = src8;
        void* d = dst8;
        unsigned n = tail;
        __asm__ volatile (
            "cld\n\trep movsb"
            : "+S"(s), "+D"(d), "+c"(n)
            :
            : "memory"
        );
    }
}
