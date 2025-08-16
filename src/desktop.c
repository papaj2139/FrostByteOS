#include <stdint.h>
#include "desktop.h"

#define VGA_WIDTH   320
#define VGA_HEIGHT  200
#define VGA_ADDRESS 0xA0000
#define TASKBAR_HEIGHT 16

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outb(uint16_t port, uint8_t val){
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port){
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void strncpy(char* dest, const char* src, unsigned int n) {
    unsigned int i = 0;
    while (i < n && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    if (i < n) dest[i] = '\0';
}

unsigned int strlen(const char* str) {
    unsigned int len = 0;
    while (str[len] != '\0') len++;
    return len;
}

static uint8_t* const VGA = (uint8_t*)VGA_ADDRESS;

static inline void putpx(int x, int y, uint8_t color) {
    if ((unsigned)x < VGA_WIDTH && (unsigned)y < VGA_HEIGHT)
        VGA[y * VGA_WIDTH + x] = color;
}

static inline uint8_t getpx(int x, int y) {
    if ((unsigned)x < VGA_WIDTH && (unsigned)y < VGA_HEIGHT)
        return VGA[y * VGA_WIDTH + x];
    return 0;
}

static const uint8_t font8x8[128][8] = {
    [0 ... 31] = {0},
    [32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    [33] = {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    [34] = {0x36,0x36,0x24,0x00,0x00,0x00,0x00,0x00},
    [35] = {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    [36] = {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},
    [37] = {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    [38] = {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},
    [39] = {0x06,0x06,0x0C,0x00,0x00,0x00,0x00,0x00},
    [40] = {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},
    [41] = {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    [42] = {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    [43] = {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    [44] = {0x00,0x00,0x00,0x00,0x0C,0x0C,0x18,0x00},
    [45] = {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    [46] = {0x00,0x00,0x00,0x00,0x0C,0x0C,0x00,0x00},
    [47] = {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},

    [48] = {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
    [49] = {0x0C,0x0E,0x0F,0x0C,0x0C,0x0C,0x3F,0x00},
    [50] = {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},
    [51] = {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    [52] = {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
    [53] = {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    [54] = {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},
    [55] = {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    [56] = {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},
    [57] = {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},

    [65] = {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    [66] = {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
    [67] = {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    [68] = {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
    [69] = {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    [70] = {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
    [71] = {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    [72] = {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},
    [73] = {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    [74] = {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
    [75] = {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    [76] = {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
    [77] = {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    [78] = {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
    [79] = {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    [80] = {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
    [81] = {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    [82] = {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
    [83] = {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    [84] = {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    [85] = {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    [86] = {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},
    [87] = {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    [88] = {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
    [89] = {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    [90] = {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},

    [97]  = {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    [98]  = {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},
    [99]  = {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    [100] = {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00},
    [101] = {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00},
    [102] = {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00},
    [103] = {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    [104] = {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},
    [105] = {0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x1E,0x00},
    [106] = {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E},
    [107] = {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    [108] = {0x1C,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    [109] = {0x00,0x00,0x36,0x7F,0x7F,0x6B,0x63,0x00},
    [110] = {0x00,0x00,0x3E,0x33,0x33,0x33,0x33,0x00},
    [111] = {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    [112] = {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},
    [113] = {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    [114] = {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},
    [115] = {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    [116] = {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},
    [117] = {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    [118] = {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},
    [119] = {0x00,0x00,0x63,0x6B,0x7F,0x36,0x36,0x00},
    [120] = {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},
    [121] = {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    [122] = {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},
    [123] = {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    [124] = {0x0C,0x0C,0x0C,0x00,0x0C,0x0C,0x0C,0x00},
    [125] = {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    [126] = {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},
    [127] = {0},
};


static void draw_char_small(int x, int y, char ch, uint8_t color) {
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

static void draw_string_small(int x, int y, const char *str, uint8_t color) {
    while (*str) {
        draw_char_small(x, y, *str, color);
        x += 8;
        str++;
    }
}

static void draw_rect(int x, int y, int w, int h, uint8_t color) {
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

static void draw_char(int x, int y, char ch, uint8_t color) {
    if ((unsigned)ch >= 128) return;
    const uint8_t *glyph = font8x8[(int)ch];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++)
            if (bits & (1 << col))
                putpx(x + col, y + row, color);
    }
}

static void draw_string(int x, int y, const char *str, uint8_t color) {
    while (*str) {
        draw_char(x, y, *str++, color);
        x += 8;
    }
}

typedef enum { WC_LABEL, WC_RECT } win_content_type;
typedef struct {
    win_content_type type;
    int x, y, w, h;
    uint8_t color;
    char text[64];
} win_content_t;

#define MAX_CONTENT 32
typedef struct {
    int x, y, w, h;
    uint8_t border;
    uint8_t fill;
    uint8_t titlebar;
    char title[32];
    win_content_t content[MAX_CONTENT];
    int content_count;
} window_t;

static void window_add_label(window_t *win, int x, int y, const char *text, uint8_t color) {
    if (win->content_count >= MAX_CONTENT) return;
    win_content_t *c = &win->content[win->content_count++];
    c->type = WC_LABEL; c->x = x; c->y = y; c->color = color;
    strncpy(c->text, text, sizeof(c->text)-1);
}

static void window_add_rect(window_t *win, int x, int y, int w, int h, uint8_t color) {
    if (win->content_count >= MAX_CONTENT) return;
    win_content_t *c = &win->content[win->content_count++];
    c->type = WC_RECT; c->x = x; c->y = y; c->w = w; c->h = h; c->color = color;
}

static void draw_window(window_t *win) {
    draw_rect(win->x + 1, win->y + 1, win->w - 2, win->h - 2, win->fill);

    for (int i = 0; i < win->w; i++) {
        putpx(win->x + i, win->y, win->border);
        putpx(win->x + i, win->y + win->h - 1, win->border);
    }
    for (int j = 0; j < win->h; j++) {
        putpx(win->x, win->y + j, win->border);
        putpx(win->x + win->w - 1, win->y + j, win->border);
    }

    draw_rect(win->x + 1, win->y + 1, win->w - 2, 8, win->titlebar);
    draw_string_small(win->x + 4, win->y, win->title, 15);

    for (int i = 0; i < win->content_count; i++) {
        win_content_t *c = &win->content[i];
        int cx = win->x + c->x;
        int cy = win->y + 10 + c->y;
        switch (c->type) {
            case WC_LABEL: draw_string_small(cx, cy, c->text, c->color); break;
            case WC_RECT:  draw_rect(cx, cy, c->w, c->h, c->color); break;
        }
    }
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

static inline void save_cursor_bg(int x, int y) {
    for (int r = 0; r < CURSOR_H; r++)
        for (int c = 0; c < CURSOR_W; c++)
            cursor_bg[r][c] = getpx(x+c, y+r);
}

static inline void restore_cursor_bg(int x, int y) {
    for (int r = 0; r < CURSOR_H; r++)
        for (int c = 0; c < CURSOR_W; c++)
            putpx(x+c, y+r, cursor_bg[r][c]);
}

static void draw_cursor(int x, int y) {
    for (int r = 0; r < CURSOR_H; r++)
        for (int c = 0; c < CURSOR_W; c++)
            if (cursor_bitmap[r][c] != 0)
                putpx(x+c, y+r, cursor_bitmap[r][c]);
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

static void draw_taskbar(void) {
    draw_rect(0, VGA_HEIGHT - TASKBAR_HEIGHT, VGA_WIDTH, TASKBAR_HEIGHT, 12);
}

static int win_dragging = -1;
static int drag_offset_x = 0, drag_offset_y = 0;

static int mouse_over(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && my >= y && mx < x + w && my < y + h;
}

static void draw_close_button(window_t *win) {
    int size = 8;
    int bx = win->x + win->w - size - 2;
    int by = win->y + 2;
    draw_rect(bx, by, size, size, 12);
    draw_string_small(bx + 2, by + 1, "X", 15);
}

static int clicked_close(window_t *win, int mx, int my) {
    int size = 8;
    int bx = win->x + win->w - size - 2;
    int by = win->y + 2;
    return mouse_over(mx, my, bx, by, size, size);
}

void cmd_desktop(const char *args) {
    (void)args;

    vga_set_mode_13h();

    static uint8_t screen_buffer[VGA_WIDTH * VGA_HEIGHT];
    
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA[i] = 3;

    draw_taskbar();

    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            screen_buffer[y * VGA_WIDTH + x] = getpx(x, y);

    window_t win1 = {.x = 50, .y = 50, .w = 150, .h = 80, .border = 40, .fill = 8, .titlebar = 0, .title = "Welcome!"};
    window_add_label(&win1, 10, 20, "Welcome to frostbyte!", 15);

    draw_window(&win1);
    draw_close_button(&win1);

    int cx = VGA_WIDTH / 2 - CURSOR_W / 2;
    int cy = VGA_HEIGHT / 2 - CURSOR_H / 2;

    mouse_init();

    int win_closed = 0;

    for (;;) {
        if (poll_mouse_packet()) {
            cx += mbytes[1];
            cy -= mbytes[2];

            if (cx < 0) cx = 0;
            if (cy < 0) cy = 0;

            int left_click = (mbytes[0] & 0x01) != 0;

            if (!win_closed && left_click) {
                if (win_dragging == -1 && mouse_over(cx, cy, win1.x + 1, win1.y + 1, win1.w - 2, 8)) {
                    win_dragging = 0;
                    drag_offset_x = cx - win1.x;
                    drag_offset_y = cy - win1.y;
                }
                if (!win_closed && clicked_close(&win1, cx, cy)) {
                    win_closed = 1;
                }
            } else if (!left_click) {
                win_dragging = -1;
            }

            if (win_dragging != -1) {
                win1.x = cx - drag_offset_x;
                win1.y = cy - drag_offset_y;
                if (win1.x < 0) win1.x = 0;
                if (win1.y < 0) win1.y = 0;
                if (win1.x + win1.w > VGA_WIDTH) win1.x = VGA_WIDTH - win1.w;
                if (win1.y + win1.h > VGA_HEIGHT - TASKBAR_HEIGHT) win1.y = VGA_HEIGHT - TASKBAR_HEIGHT - win1.h;
            }

            for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
                VGA[i] = screen_buffer[i];

            if (!win_closed) {
                draw_window(&win1);
                draw_close_button(&win1);
            }

            draw_cursor(cx, cy);
        }
    }
}