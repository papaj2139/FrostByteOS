#include <stdint.h>
#include "desktop.h"
#include "string.h"
#include "io.h"
#include "drivers/keyboard.h"
#include "gui/vga.h"

#define TASKBAR_HEIGHT 16
#define MAX_CONTENT 32
#define MAX_PROCESSES 16
#define CURSOR_W 32
#define CURSOR_H 64

// Forward declaration for kernel shutdown function
extern void kshutdown(void);

char current_notepad_text[128] = "";

//structs and typedefs
typedef enum { WC_LABEL, WC_RECT, WC_TEXTAREA } win_content_type;
typedef struct {
    win_content_type type;
    int x, y, w, h;
    uint8_t color;
    char text[64];
} win_content_t;

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

typedef enum {
    PROC_WELCOME,
    PROC_CALCULATOR,
    PROC_NOTEPAD,
    PROC_ABOUT,
    PROC_TEXTMODERET
} process_type_t;

typedef struct {
    int pid;
    process_type_t type;
    window_t window;
    int active;
} process_t;

typedef struct {
    char name[32];
    void (*action)(void);
} start_menu_item_t;

//global variables
static process_t processes[MAX_PROCESSES];
static int next_pid = 1;
static int process_count = 0;
static int start_menu_open = 0;
static int start_button_width = 50;
static int start_menu_item_count = 5;
static uint8_t cursor_bg[CURSOR_H][CURSOR_W];
static uint8_t mcycle = 0;
static int8_t  mbytes[3];
static int win_dragging = -1;
static int drag_offset_x = 0, drag_offset_y = 0;

//arrays
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

//function declarations
static void action_calculator(void);
static void action_notepad(void);
static void action_about(void);
static void action_shutdown(void);
static void action_textmode(void);
static void draw_close_button(window_t *win);

static start_menu_item_t start_menu_items[] = {
    {"Calculator", action_calculator},
    {"Notepad", action_notepad},
    {"About", action_about},
    {"Frosty CLI", action_textmode},
    {"Shutdown", action_shutdown}
};


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
            window_add_label(&proc->window, 15, 30, current_notepad_text, 0);
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

// Start Menu functions
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

static void action_textmode(void) {
    create_process(PROC_TEXTMODERET, 120, 80);
}

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
        for(int i = 0; i < process_count; i++){
            if(processes[i].type == PROC_TEXTMODERET){
                vga_set_mode_12h();
                return;
            }
            if(processes[i].type == PROC_NOTEPAD && processes[i].active){
                char key = kb_poll();
                if (key) {
                    size_t len = strlen(current_notepad_text);
                    if(len < sizeof(current_notepad_text)-1){
                        current_notepad_text[len] = key;
                        current_notepad_text[len+1] = '\0';
                    }
                }
            }
        }
    }
}