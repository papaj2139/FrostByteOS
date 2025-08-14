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

void vga_set_mode_13h(){
    static const uint8_t g_320x200x256[] = {
        0x63,
        0x03,0x01,0x0F,0x00,0x0E,
        0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,
        0x00,0x41,0x00,0x00,0x00,0x00,
        0x9C,0x0E,0x8F,0x28,0x40,0x96,0xB9,0xA3,0xFF,
        0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,
        0x00
    };

    uint8_t* registers = (uint8_t*)g_320x200x256;
    outb(0x3C2, *registers++);
    for (uint8_t i = 0; i < 5; i++) outb(0x3C4, i), outb(0x3C5, *registers++);
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);
    for (uint8_t i = 0; i < 25; i++) outb(0x3D4, i), outb(0x3D5, *registers++);
    for (uint8_t i = 0; i < 9; i++) outb(0x3C0, i), outb(0x3C0, *registers++);
    outb(0x3C0, 0x20);
}

#define CURSOR_WIDTH  16
#define CURSOR_HEIGHT 24

static const uint8_t cursor_bitmap[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {15,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {15,0,15,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {15,0,0,15,0,0,0,0,0,0,0,0,0,0,0,0},
    {15,0,0,0,15,0,0,0,0,0,0,0,0,0,0,0},
    {15,0,0,0,0,15,0,0,0,0,0,0,0,0,0,0},
    {15,0,0,0,0,0,15,0,0,0,0,0,0,0,0,0},
    {15,0,0,0,0,0,0,15,0,0,0,0,0,0,0,0},
    {15,0,0,0,0,0,0,0,15,0,0,0,0,0,0,0},
    {15,0,0,0,0,0,0,0,0,15,0,0,0,0,0,0},
    {15,0,0,0,0,0,0,0,0,0,15,0,0,0,0,0},
    {15,0,0,0,0,0,0,0,0,0,0,15,0,0,0,0},
    {15,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0},
    {15,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0},
    {15,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0},
    {15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15},
    {15,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0},
    {15,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0},
    {15,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0},
    {15,0,0,0,0,0,0,0,0,0,0,15,0,0,0,0},
    {15,0,0,0,0,0,0,0,0,0,15,0,0,0,0,0},
    {15,0,0,0,0,0,0,0,0,15,0,0,0,0,0,0},
    {15,0,0,0,0,0,0,0,15,0,0,0,0,0,0,0},
    {15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15}
};

static uint8_t cursor_bg[CURSOR_HEIGHT][CURSOR_WIDTH];

static int mouse_x = VGA_WIDTH / 2;
static int mouse_y = VGA_HEIGHT / 2;

void save_cursor_bg(int x, int y) {
    for (int j = 0; j < CURSOR_HEIGHT; j++) {
        for (int i = 0; i < CURSOR_WIDTH; i++) {
            if (x + i < VGA_WIDTH && y + j < VGA_HEIGHT)
                cursor_bg[j][i] = VGA[(y + j) * VGA_WIDTH + (x + i)];
        }
    }
}

void restore_cursor_bg(int x, int y) {
    for (int j = 0; j < CURSOR_HEIGHT; j++) {
        for (int i = 0; i < CURSOR_WIDTH; i++) {
            if (x + i < VGA_WIDTH && y + j < VGA_HEIGHT)
                VGA[(y + j) * VGA_WIDTH + (x + i)] = cursor_bg[j][i];
        }
    }
}

void draw_cursor(int x, int y) {
    for (int j = 0; j < CURSOR_HEIGHT; j++) {
        for (int i = 0; i < CURSOR_WIDTH; i++) {
            if (x + i < VGA_WIDTH && y + j < VGA_HEIGHT) {
                uint8_t color = cursor_bitmap[j][i];
                if (color != 0) VGA[(y + j) * VGA_WIDTH + (x + i)] = color;
            }
        }
    }
}

void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if (inb(0x64) & 1) return;
        }
    } else {
        while (timeout--) {
            if (!(inb(0x64) & 2)) return;
        }
    }
}

void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, data);
}

uint8_t mouse_read() {
    mouse_wait(0);
    return inb(0x60);
}

void mouse_install() {
    outb(0x64, 0xA8);
    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    uint8_t status = (inb(0x60) | 2);
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);
    mouse_write(0xF6);
    mouse_read();
    mouse_write(0xF4);
    mouse_read();
}

int8_t mouse_dx = 0, mouse_dy = 0;
uint8_t mouse_cycle = 0;
uint8_t mouse_byte[3];

void poll_mouse_packet() {
    if (inb(0x64) & 1) {
        int8_t data = inb(0x60);
        if (mouse_cycle == 0 && !(data & 0x08)) return;
        mouse_byte[mouse_cycle++] = data;
        if (mouse_cycle == 3) {
            mouse_dx = mouse_byte[1];
            mouse_dy = -mouse_byte[2];
            mouse_cycle = 0;
        }
    }
}

void cmd_desktop(const char* args) {
    vga_set_mode_13h();
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) VGA[i] = 3;
    mouse_x = VGA_WIDTH / 2;
    mouse_y = VGA_HEIGHT / 2;
    save_cursor_bg(mouse_x, mouse_y);
    draw_cursor(mouse_x, mouse_y);
    mouse_install();

    for (;;) {
        poll_mouse_packet();
        if (mouse_dx || mouse_dy) {
            restore_cursor_bg(mouse_x, mouse_y);
            mouse_x += mouse_dx;
            mouse_y += mouse_dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > VGA_WIDTH - CURSOR_WIDTH) mouse_x = VGA_WIDTH - CURSOR_WIDTH;
            if (mouse_y > VGA_HEIGHT - CURSOR_HEIGHT) mouse_y = VGA_HEIGHT - CURSOR_HEIGHT;
            save_cursor_bg(mouse_x, mouse_y);
            draw_cursor(mouse_x, mouse_y);
            mouse_dx = 0;
            mouse_dy = 0;
        }
    }
}
