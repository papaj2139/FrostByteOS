#include <stdint.h>
#include "desktop.h"

#define VGA_WIDTH   320
#define VGA_HEIGHT  200
#define VGA_ADDRESS 0xA0000
#define TASKBAR_HEIGHT 16

// Forward declaration for kernel shutdown function
extern void kshutdown(void);

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
    int active;
    int process_id;
} window_t;

// Forward declaration after window_t is defined
static void draw_close_button(window_t *win);

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
    if (!win->active) return;
    
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

// Process Manager
#define MAX_PROCESSES 16
typedef enum {
    PROC_WELCOME,
    PROC_CALCULATOR,
    PROC_NOTEPAD,
    PROC_ABOUT
} process_type_t;

typedef struct {
    int pid;
    process_type_t type;
    window_t window;
    int active;
} process_t;

static process_t processes[MAX_PROCESSES];
static int next_pid = 1;
static int process_count = 0;

static int create_process(process_type_t type, int x, int y) {
    if (process_count >= MAX_PROCESSES) return -1;
    
    int pid = next_pid++;
    process_t *proc = &processes[process_count++];
    
    proc->pid = pid;
    proc->type = type;
    proc->active = 1;
    proc->window.active = 1;
    proc->window.process_id = pid;
    proc->window.x = x;
    proc->window.y = y;
    proc->window.border = 40;
    proc->window.fill = 8;
    proc->window.titlebar = 0;
    proc->window.content_count = 0;
    
    switch (type) {
        case PROC_WELCOME:
            proc->window.w = 150;
            proc->window.h = 80;
            strncpy(proc->window.title, "Welcome!", sizeof(proc->window.title)-1);
            window_add_label(&proc->window, 10, 20, "Welcome to frostbyte!", 15);
            break;
            
        case PROC_CALCULATOR:
            proc->window.w = 180;
            proc->window.h = 120;
            strncpy(proc->window.title, "Calculator", sizeof(proc->window.title)-1);
            window_add_label(&proc->window, 10, 10, "Calculator App", 15);
            window_add_label(&proc->window, 10, 25, "Display: 0", 14);
            // Add calculator buttons
            window_add_rect(&proc->window, 10, 40, 20, 15, 12);
            window_add_label(&proc->window, 17, 43, "7", 0);
            window_add_rect(&proc->window, 35, 40, 20, 15, 12);
            window_add_label(&proc->window, 42, 43, "8", 0);
            window_add_rect(&proc->window, 60, 40, 20, 15, 12);
            window_add_label(&proc->window, 67, 43, "9", 0);
            window_add_rect(&proc->window, 85, 40, 20, 15, 12);
            window_add_label(&proc->window, 92, 43, "+", 0);
            break;
            
        case PROC_NOTEPAD:
            proc->window.w = 200;
            proc->window.h = 150;
            strncpy(proc->window.title, "Notepad", sizeof(proc->window.title)-1);
            window_add_label(&proc->window, 10, 10, "Text Editor", 15);
            window_add_rect(&proc->window, 10, 25, 170, 100, 7);
            window_add_label(&proc->window, 15, 30, "Type your text here...", 0);
            break;
            
        case PROC_ABOUT:
            proc->window.w = 160;
            proc->window.h = 100;
            strncpy(proc->window.title, "About", sizeof(proc->window.title)-1);
            window_add_label(&proc->window, 10, 10, "FrostByte OS v1.0", 15);
            window_add_label(&proc->window, 10, 25, "A simple OS project", 14);
            window_add_label(&proc->window, 10, 40, "Built with C", 14);
            break;
    }
    
    return pid;
}

static void close_process(int pid) {
    for (int i = 0; i < process_count; i++) {
        if (processes[i].pid == pid) {
            processes[i].active = 0;
            processes[i].window.active = 0;
            // Shift remaining processes down
            for (int j = i; j < process_count - 1; j++) {
                processes[j] = processes[j + 1];
            }
            process_count--;
            break;
        }
    }
}

static process_t* get_process(int pid) {
    for (int i = 0; i < process_count; i++) {
        if (processes[i].pid == pid && processes[i].active) {
            return &processes[i];
        }
    }
    return 0;
}

static void draw_all_windows(void) {
    for (int i = 0; i < process_count; i++) {
        if (processes[i].active) {
            draw_window(&processes[i].window);
            draw_close_button(&processes[i].window);
        }
    }
}

#define CURSOR_W 32
#define CURSOR_H 64

static const uint8_t cursor_bitmap[CURSOR_H][CURSOR_W] = {
    { 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8, 7, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8,15, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8,15,15, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8,15,15, 7, 8, 8, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8,15,15,15, 7, 8, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8,15,15,15,15, 8, 8, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8,15,15,15,15, 7, 8, 8, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8,15,15,15,15,15, 7, 8, 8, 0, 0, 0, 0},
    { 0, 0, 0, 8,15,15,15,15,15,15, 7, 8, 0, 0, 0, 0},
    { 0, 0, 0, 8,15,15,15,15,15,15, 7, 8, 8, 0, 0, 0},
    { 0, 0, 0, 8,15,15,15,15,15,15,15, 7, 8, 0, 0, 0},
    { 0, 0, 0, 8,15,15,15,15,15,15,15,15, 8, 0, 0, 0},
    { 0, 0, 0, 8,15,15,15,15,15, 8, 8, 8, 8, 0, 0, 0},
    { 0, 0, 0, 8,15,15, 7,15,15, 8, 8, 8, 8, 0, 0, 0},
    { 0, 0, 0, 8,15,15, 8, 7,15, 7, 8, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8,15, 7, 8, 8,15, 7, 8, 0, 0, 0, 0, 0},
    { 0, 0, 0, 8, 7, 8, 8, 8,15,15, 8, 8, 0, 0, 0, 0},
    { 0, 0, 0, 8, 8, 8, 0, 8, 7,15, 7, 8, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 8, 7,15, 7, 8, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0},
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

// Start Menu variables
static int start_menu_open = 0;
static int start_button_width = 50;

typedef struct {
    char name[32];
    void (*action)(void);
} start_menu_item_t;

static void action_calculator(void) {
    create_process(PROC_CALCULATOR, 80, 60);
}

static void action_notepad(void) {
    create_process(PROC_NOTEPAD, 100, 40);
}

static void action_about(void) {
    create_process(PROC_ABOUT, 120, 80);
}

static void action_shutdown(void) {
    kshutdown();
}

static start_menu_item_t start_menu_items[] = {
    {"Calculator", action_calculator},
    {"Notepad", action_notepad},
    {"About", action_about},
    {"Shutdown", action_shutdown}
};
static int start_menu_item_count = 4;

static void draw_taskbar(void) {
    draw_rect(0, VGA_HEIGHT - TASKBAR_HEIGHT, VGA_WIDTH, TASKBAR_HEIGHT, 12);
    
    // Draw start button
    int button_color = start_menu_open ? 8 : 14; // Darker when pressed
    draw_rect(2, VGA_HEIGHT - TASKBAR_HEIGHT + 2, start_button_width, TASKBAR_HEIGHT - 4, button_color);
    draw_string_small(6, VGA_HEIGHT - TASKBAR_HEIGHT + 4, "Start", 0);
    
    // Draw process indicators in taskbar
    int taskbar_x = start_button_width + 10;
    for (int i = 0; i < process_count; i++) {
        if (processes[i].active) {
            draw_rect(taskbar_x, VGA_HEIGHT - TASKBAR_HEIGHT + 2, 60, TASKBAR_HEIGHT - 4, 9);
            draw_string_small(taskbar_x + 2, VGA_HEIGHT - TASKBAR_HEIGHT + 4, processes[i].window.title, 15);
            taskbar_x += 65;
            if (taskbar_x > VGA_WIDTH - 60) break; // Don't overflow taskbar
        }
    }
}

static void draw_start_menu(void) {
    if (!start_menu_open) return;
    
    int menu_width = 80;
    int menu_height = start_menu_item_count * 12 + 4;
    int menu_x = 2;
    int menu_y = VGA_HEIGHT - TASKBAR_HEIGHT - menu_height;
    
    // Menu background
    draw_rect(menu_x, menu_y, menu_width, menu_height, 7);
    
    // Menu border
    for (int i = 0; i < menu_width; i++) {
        putpx(menu_x + i, menu_y, 0);
        putpx(menu_x + i, menu_y + menu_height - 1, 0);
    }
    for (int j = 0; j < menu_height; j++) {
        putpx(menu_x, menu_y + j, 0);
        putpx(menu_x + menu_width - 1, menu_y + j, 0);
    }
    
    // Draw menu items
    for (int i = 0; i < start_menu_item_count; i++) {
        draw_string_small(menu_x + 4, menu_y + 2 + i * 12, start_menu_items[i].name, 0);
    }
}

static int clicked_start_button(int mx, int my) {
    return mx >= 2 && my >= VGA_HEIGHT - TASKBAR_HEIGHT + 2 && 
           mx < 2 + start_button_width && my < VGA_HEIGHT - 2;
}

static int clicked_start_menu_item(int mx, int my) {
    if (!start_menu_open) return -1;
    
    int menu_width = 80;
    int menu_height = start_menu_item_count * 12 + 4;
    int menu_x = 2;
    int menu_y = VGA_HEIGHT - TASKBAR_HEIGHT - menu_height;
    
    if (mx < menu_x || my < menu_y || mx >= menu_x + menu_width || my >= menu_y + menu_height) 
        return -1;
    
    int item_index = (my - menu_y - 2) / 12;
    if (item_index >= 0 && item_index < start_menu_item_count) {
        return item_index;
    }
    return -1;
}

static int clicked_taskbar_process(int mx, int my) {
    if (my < VGA_HEIGHT - TASKBAR_HEIGHT + 2 || my >= VGA_HEIGHT - 2) return -1;
    
    int taskbar_x = start_button_width + 10;
    for (int i = 0; i < process_count; i++) {
        if (processes[i].active) {
            if (mx >= taskbar_x && mx < taskbar_x + 60) {
                return processes[i].pid;
            }
            taskbar_x += 65;
            if (taskbar_x > VGA_WIDTH - 60) break;
        }
    }
    return -1;
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

static process_t* find_window_at_position(int mx, int my) {
    for (int i = process_count - 1; i >= 0; i--) {
        if (processes[i].active) {
            window_t *win = &processes[i].window;
            if (mouse_over(mx, my, win->x, win->y, win->w, win->h)) {
                return &processes[i];
            }
        }
    }
    return 0;
}

static void bring_to_front(int pid) {
    process_t *proc = get_process(pid);
    if (!proc) return;
    
    int index = -1;
    for (int i = 0; i < process_count; i++) {
        if (processes[i].pid == pid) {
            index = i;
            break;
        }
    }
    
    if (index == -1 || index == process_count - 1) return;
    
    process_t temp = processes[index];
    for (int i = index; i < process_count - 1; i++) {
        processes[i] = processes[i + 1];
    }
    processes[process_count - 1] = temp;
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

    create_process(PROC_WELCOME, 50, 50);

    int cx = VGA_WIDTH / 2 - CURSOR_W / 2;
    int cy = VGA_HEIGHT / 2 - CURSOR_H / 2;

    mouse_init();

    for (;;) {
        if (poll_mouse_packet()) {
            cx += mbytes[1];
            cy -= mbytes[2];

            if (cx < 0) cx = 0;
            if (cy < 0) cy = 0;

            int left_click = (mbytes[0] & 0x01) != 0;
            static int was_clicking = 0;
            int just_clicked = left_click && !was_clicking;
            was_clicking = left_click;

            if (just_clicked) {
                if (clicked_start_button(cx, cy)) {
                    start_menu_open = !start_menu_open;
                }
                else if (start_menu_open) {
                    int item = clicked_start_menu_item(cx, cy);
                    if (item >= 0) {
                        start_menu_items[item].action();
                        start_menu_open = 0;
                    } else {
                        start_menu_open = 0;
                    }
                }
                else {
                    int clicked_pid = clicked_taskbar_process(cx, cy);
                    if (clicked_pid > 0) {
                        bring_to_front(clicked_pid);
                    }
                    else {
                        process_t *clicked_proc = find_window_at_position(cx, cy);
                        if (clicked_proc) {
                            if (clicked_close(&clicked_proc->window, cx, cy)) {
                                close_process(clicked_proc->pid);
                            }
                            else if (mouse_over(cx, cy, clicked_proc->window.x + 1, clicked_proc->window.y + 1, 
                                              clicked_proc->window.w - 2, 8)) {
                                win_dragging = clicked_proc->pid;
                                drag_offset_x = cx - clicked_proc->window.x;
                                drag_offset_y = cy - clicked_proc->window.y;
                                bring_to_front(clicked_proc->pid);
                            }
                        }
                    }
                }
            } else if (!left_click) {
                win_dragging = -1;
            }

            if (win_dragging != -1) {
                process_t *drag_proc = get_process(win_dragging);
                if (drag_proc) {
                    drag_proc->window.x = cx - drag_offset_x;
                    drag_proc->window.y = cy - drag_offset_y;
                    if (drag_proc->window.x < 0) drag_proc->window.x = 0;
                    if (drag_proc->window.y < 0) drag_proc->window.y = 0;
                    if (drag_proc->window.x + drag_proc->window.w > VGA_WIDTH) 
                        drag_proc->window.x = VGA_WIDTH - drag_proc->window.w;
                    if (drag_proc->window.y + drag_proc->window.h > VGA_HEIGHT - TASKBAR_HEIGHT) 
                        drag_proc->window.y = VGA_HEIGHT - TASKBAR_HEIGHT - drag_proc->window.h;
                }
            }

            for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
                VGA[i] = screen_buffer[i];

            draw_taskbar();
            draw_start_menu();
            draw_all_windows();
            draw_cursor(cx, cy);
        }
    }
}