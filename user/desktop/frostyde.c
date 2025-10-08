#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>

//FB ioctl
#define FB_IOCTL_BLIT 0x0001u
#define FB_IOCTL_SET_CONSOLE 0x0002u

typedef struct {
    unsigned x, y;
    unsigned w, h;
    unsigned src_pitch;
    unsigned flags;
    const void* src;
} fb_blit_args_t;

//mouse event
typedef struct {
    unsigned time_ms;
    short rel_x, rel_y;
    unsigned char type;
    unsigned char button;
    unsigned short reserved;
} mouse_event_t;

typedef struct {
    int x, y;
    int width, height;
    char title[64];
    int visible;
} window_t;

typedef struct {
    int x, y;
    int buttons;
} mouse_state_t;

static int fb_fd = -1;
static int mouse_fd = -1;
static mouse_state_t mouse = {400, 300, 0}; //start in center
static unsigned* fb_buffer = NULL;
static int screen_w = 800;
static int screen_h = 600;

//Colors (RGB 888)
#define COLOR_TASKBAR   0x2C3E50
#define COLOR_WINDOW_BG 0xECF0F1
#define COLOR_WINDOW_TITLE 0x3498DB
#define COLOR_TEXT      0x2C3E50
#define COLOR_WHITE     0xFFFFFF
#define COLOR_BLACK     0x000000

//read screen size from /proc/fb0
static int read_screen_size(void) {
    int fd = open("/proc/fb0", O_RDONLY);
    if (fd < 0) return -1;
    
    char buf[256];
    int r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    
    if (r <= 0) return -1;
    buf[r] = '\0';
    
    //parse "width: X\nheight: Y\n"
    char* p = buf;
    while (*p) {
        if (strncmp(p, "width:", 6) == 0) {
            screen_w = atoi(p + 6);
        } else if (strncmp(p, "height:", 7) == 0) {
            screen_h = atoi(p + 7);
        }
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    
    return (screen_w > 0 && screen_h > 0) ? 0 : -1;
}

//initialize framebuffer
static int init_fb(void) {
    //get screen dimensions
    if (read_screen_size() != 0) {
        fprintf(2, "FrostyDE: Failed to read screen size from /proc/fb0\n");
        fprintf(2, "Using default 800x600\n");
        screen_w = 800;
        screen_h = 600;
    }
    
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        fprintf(2, "FrostyDE: Failed to open framebuffer\n");
        return -1;
    }
    
    //allocate backbuffer
    fb_buffer = malloc(screen_w * screen_h * sizeof(unsigned));
    if (!fb_buffer) {
        fprintf(2, "FrostyDE: Failed to allocate framebuffer\n");
        close(fb_fd);
        return -1;
    }
    
    //disable console output
    int disable = 0;
    if (ioctl(fb_fd, FB_IOCTL_SET_CONSOLE, &disable) == 0) {
        fprintf(1, "Console output disabled for exclusive framebuffer control\n");
    }
    
    fprintf(1, "Framebuffer: %dx%d initialized\n", screen_w, screen_h);
    return 0;
}

//initialize mouse
static int init_mouse(void) {
    mouse_fd = open("/dev/input/mouse", O_RDONLY);
    if (mouse_fd < 0) {
        fprintf(2, "FrostyDE: Failed to open mouse device\n");
        return -1;
    }
    fprintf(1, "Mouse device opened successfully\n");
    return 0;
}

//draw a filled rectangle
static void draw_rect(int x, int y, int w, int h, unsigned color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > screen_w) w = screen_w - x;
    if (y + h > screen_h) h = screen_h - y;
    if (w <= 0 || h <= 0) return;
    
    for (int dy = 0; dy < h; dy++) {
        unsigned* row = fb_buffer + (y + dy) * screen_w + x;
        for (int dx = 0; dx < w; dx++) {
            row[dx] = color;
        }
    }
}

//draw a single pixel
static void draw_pixel(int x, int y, unsigned color) {
    if (x >= 0 && x < screen_w && y >= 0 && y < screen_h) {
        fb_buffer[y * screen_w + x] = color;
    }
}

//draw text (stub)
static void draw_text(int x, int y, const char* text, unsigned color) {
    //stub just draw rectangle
    int len = strlen(text);
    for (int i = 0; i < len; i++) {
        draw_rect(x + i * 8, y, 6, 8, color);
    }
}

//draw window
static void draw_window(window_t* win) {
    if (!win->visible) return;
    
    //draw title bar
    draw_rect(win->x, win->y, win->width, 24, COLOR_WINDOW_TITLE);
    draw_text(win->x + 8, win->y + 6, win->title, COLOR_WHITE);
    
    //draw window body
    draw_rect(win->x, win->y + 24, win->width, win->height - 24, COLOR_WINDOW_BG);
    
    //draw window border
}

//draw taskbar
static void draw_taskbar(void) {
    draw_rect(0, screen_h - 32, screen_w, 32, COLOR_TASKBAR);
    draw_text(8, screen_h - 24, "FrostByte", COLOR_WHITE);
}

//draw mouse cursor
static void draw_cursor(void) {
    int x = mouse.x;
    int y = mouse.y;
    
    //1 = black fill, 2 = white outline, 0 = transparent
    static const unsigned char cursor_data[18][14] = {
        {2,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {2,2,0,0,0,0,0,0,0,0,0,0,0,0},
        {2,1,2,0,0,0,0,0,0,0,0,0,0,0},
        {2,1,1,2,0,0,0,0,0,0,0,0,0,0},
        {2,1,1,1,2,0,0,0,0,0,0,0,0,0},
        {2,1,1,1,1,2,0,0,0,0,0,0,0,0},
        {2,1,1,1,1,1,2,0,0,0,0,0,0,0},
        {2,1,1,1,1,1,1,2,0,0,0,0,0,0},
        {2,1,1,1,1,1,1,1,2,0,0,0,0,0},
        {2,1,1,1,1,1,2,2,2,2,0,0,0,0},
        {2,1,1,2,1,1,2,0,0,0,0,0,0,0},
        {2,1,2,0,2,1,1,2,0,0,0,0,0,0},
        {2,2,0,0,2,1,1,2,0,0,0,0,0,0},
        {0,0,0,0,0,2,1,1,2,0,0,0,0,0},
        {0,0,0,0,0,2,1,1,2,0,0,0,0,0},
        {0,0,0,0,0,0,2,1,1,2,0,0,0,0},
        {0,0,0,0,0,0,2,2,2,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0}
    };
    
    //draw cursor
    for (int dy = 0; dy < 18; dy++) {
        for (int dx = 0; dx < 14; dx++) {
            unsigned char pixel = cursor_data[dy][dx];
            if (pixel == 0) continue; //transparent
            
            unsigned color;
            if (pixel == 1) {
                color = COLOR_BLACK; //black fill
            } else { //pixel == 2
                color = COLOR_WHITE; //white outline
            }
            draw_pixel(x + dx, y + dy, color);
        }
    }
}

//update mouse state
static void update_mouse(void) {
    mouse_event_t event;
    
    if (mouse_fd < 0) return; //mouse not available
    
    //read all available mouse events (non-blocking)
    while (1) {
        int r = read(mouse_fd, &event, sizeof(event));
        if (r != sizeof(event)) break; //no more events
        
        if (event.type == 2) { //motion event
            mouse.x += event.rel_x;
            mouse.y -= event.rel_y; //invert Y-axis (PS/2 mouse reports inverted)
            
            //clamp to screen bounds
            if (mouse.x < 0) mouse.x = 0;
            if (mouse.x >= screen_w) mouse.x = screen_w - 1;
            if (mouse.y < 0) mouse.y = 0;
            if (mouse.y >= screen_h) mouse.y = screen_h - 1;
        } else if (event.type == 1) { //button press
            mouse.buttons |= event.button;
        } else if (event.type == 0) { //button release
            mouse.buttons &= ~event.button;
        }
    }
}

//flip backbuffer to screen
static void present(void) {
    fb_blit_args_t blit;
    blit.x = 0;
    blit.y = 0;
    blit.w = screen_w;
    blit.h = screen_h;
    blit.src_pitch = screen_w * 4;
    blit.flags = 0;
    blit.src = fb_buffer;
    
    ioctl(fb_fd, FB_IOCTL_BLIT, &blit);
}

//main event loop
static void event_loop(void) {
    window_t windows[4];
    int num_windows = 0;
    
    //create test window
    windows[0].x = 100;
    windows[0].y = 100;
    windows[0].width = 400;
    windows[0].height = 300;
    strncpy(windows[0].title, "Terminal", 63);
    windows[0].visible = 1;
    num_windows = 1;
    
    fprintf(1, "FrostyDE: Starting event loop\n");
    fprintf(1, "Mouse: %d, %d\n", mouse.x, mouse.y);
    
    int frame = 0;
    while (1) {
        //clear screen (background color)
        draw_rect(0, 0, screen_w, screen_h, 0x1E90FF); //dodger blue
        
        //draw windows
        for (int i = 0; i < num_windows; i++) {
            draw_window(&windows[i]);
        }
        
        //draw taskbar
        draw_taskbar();
        
        //update mouse
        update_mouse();
        
        //draw cursor
        draw_cursor();
        
        //present to screen
        present();
        
        //debug output every 60 frames
        if (++frame % 60 == 0) {
            fprintf(1, "Mouse: %d, %d | Buttons: %d\r", mouse.x, mouse.y, mouse.buttons);
        }
        
        //sleep a bit (30 FPS for now)
        usleep(33333);
    }
}

int main(int argc, char** argv, char** envp) {
    (void)argc; 
    (void)argv; 
    (void)envp;
    
    fprintf(1, "\033[2J\033[H");
    
    //initialize framebuffer
    if (init_fb() != 0) {
        fprintf(2, "ERROR: Failed to initialize framebuffer\n");
        fprintf(2, "Make sure VESA is configured and /dev/fb0 exists\n");
        return 1;
    }
    
    //initialize mouse
    if (init_mouse() != 0) {
        fprintf(2, "WARNING: Mouse not available\n");
        fprintf(2, "DE will run without mouse support\n");
    }
    
    fprintf(1, "Starting desktop environment...\n");
    
    //start event loop
    event_loop();
    
    //cleanup
    if (fb_buffer) free(fb_buffer);
    if (fb_fd >= 0) close(fb_fd);
    if (mouse_fd >= 0) close(mouse_fd);
    
    return 0;
}
