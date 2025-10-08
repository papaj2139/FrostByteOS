#include "../frostywm/libfwm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <stdint.h>
#include <stdarg.h>

//colors (ARGB 8888)
#define COLOR_DESKTOP_BG    0xFF1E90FF  //dodger blue
#define COLOR_PANEL_BG      0xFF2C3E50  //dark gray-blue
#define COLOR_PANEL_TEXT    0xFFFFFFFF  //white
#define COLOR_WINDOW_BG     0xFFECF0F1  //light gray
#define COLOR_WINDOW_TITLE  0xFF3498DB  //blue
#define COLOR_WINDOW_TEXT   0xFF2C3E50  //dark gray
#define COLOR_BUTTON_BG     0xFF3498DB  //blue
#define COLOR_BUTTON_HOVER  0xFF2980B9  //darker blue
#define COLOR_WHITE         0xFFFFFFFF
#define COLOR_BLACK         0xFF000000

//panel height
#define PANEL_HEIGHT 32

//window manager state
typedef struct {
    fwm_connection_t* conn;
    uint32_t screen_w, screen_h;
    
    //desktop windows
    fwm_window_t panel_window;
    fwm_window_t desktop_window;
    fwm_window_t test_window;
    
    //window buffers
    uint32_t* panel_buffer;
    uint32_t* desktop_buffer;
    uint32_t* test_buffer;
    
    int running;
} frostyde_t;

static frostyde_t g_de;
static char g_clock_text[6];
static uint32_t g_clock_fallback = 0;

typedef struct {
    int glyph_width;
    int glyph_height;
    int glyph_stride;
    int glyph_count;
    const uint8_t* glyph_data;
    uint8_t* allocated_data;
    int lsb_first;
} fwm_font_t;

static fwm_font_t g_font;
static int serial_fd = -1;

#ifndef DE_DEBUG_LOGS
#define DE_DEBUG_LOGS 0
#endif

#if DE_DEBUG_LOGS
#define DE_DEBUG_LOG(...) log_serial(__VA_ARGS__)
#else
#define DE_DEBUG_LOG(...) do { } while (0)
#endif


static const uint8_t g_fallback_font[128][8] = {
    [0 ... 31] = {0},
    [32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, //' ' space
    [33] = {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, //!
    [34] = {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, //"
    [35] = {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, //#
    [36] = {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, //$
    [37] = {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, //%
    [38] = {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, //&
    [39] = {0x18,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, //'
    [40] = {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, //(
    [41] = {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, //)
    [42] = {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, //*
    [43] = {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, //+
    [44] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x0C}, //,
    [45] = {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, //-
    [46] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, //.
    [47] = {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // /
    [48] = {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, //0
    [49] = {0x18,0x1C,0x18,0x18,0x18,0x18,0x7E,0x00}, //1
    [50] = {0x3E,0x63,0x60,0x3C,0x06,0x03,0x7F,0x00}, //2
    [51] = {0x3E,0x63,0x60,0x3C,0x60,0x63,0x3E,0x00}, //3
    [52] = {0x30,0x38,0x3C,0x36,0x7F,0x30,0x78,0x00}, //4
    [53] = {0x7F,0x03,0x03,0x3F,0x60,0x63,0x3E,0x00}, //5
    [54] = {0x3C,0x06,0x03,0x3F,0x63,0x63,0x3E,0x00}, //6
    [55] = {0x7F,0x63,0x30,0x18,0x0C,0x0C,0x0C,0x00}, //7
    [56] = {0x3E,0x63,0x63,0x3E,0x63,0x63,0x3E,0x00}, //8
    [57] = {0x3E,0x63,0x63,0x7E,0x60,0x30,0x1E,0x00}, //9
    [58] = {0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x00}, //:
    [59] = {0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x0C}, //;
    [60] = {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00}, //<
    [61] = {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, //=
    [62] = {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, //>
    [63] = {0x3E,0x63,0x30,0x18,0x18,0x00,0x18,0x00}, //?
    [64] = {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x3E,0x00}, //@
    [65] = {0x1C,0x36,0x63,0x63,0x7F,0x63,0x63,0x00}, //A
    [66] = {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, //B
    [67] = {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, //C
    [68] = {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, //D
    [69] = {0x7F,0x06,0x06,0x3E,0x06,0x06,0x7F,0x00}, //E
    [70] = {0x7F,0x06,0x06,0x3E,0x06,0x06,0x06,0x00}, //F
    [71] = {0x3C,0x66,0x03,0x03,0x7B,0x66,0x7C,0x00}, //G
    [72] = {0x63,0x63,0x63,0x7F,0x63,0x63,0x63,0x00}, //H
    [73] = {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, //I
    [74] = {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, //J
    [75] = {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, //K
    [76] = {0x06,0x06,0x06,0x06,0x06,0x06,0x7F,0x00}, //L
    [77] = {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, //M
    [78] = {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, //N
    [79] = {0x3E,0x63,0x63,0x63,0x63,0x63,0x3E,0x00}, //O
    [80] = {0x3F,0x66,0x66,0x3E,0x06,0x06,0x06,0x00}, //P
    [81] = {0x3E,0x63,0x63,0x63,0x6B,0x36,0x6C,0x00}, //Q
    [82] = {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, //R
    [83] = {0x3E,0x63,0x06,0x3E,0x60,0x63,0x3E,0x00}, //S
    [84] = {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, //T
    [85] = {0x63,0x63,0x63,0x63,0x63,0x63,0x3E,0x00}, //U
    [86] = {0x63,0x63,0x63,0x63,0x36,0x1C,0x08,0x00}, //V
    [87] = {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, //W
    [88] = {0x63,0x63,0x36,0x1C,0x36,0x63,0x63,0x00}, //X
    [89] = {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, //Y
    [90] = {0x7F,0x60,0x30,0x18,0x0C,0x06,0x7F,0x00}, //Z
    [91] = {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, //[
    [92] = {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, //backslash
    [93] = {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, //]
    [94] = {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, //^
    [95] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, //_
    [96] = {0x0C,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, //`
    [97] = {0x00,0x00,0x3E,0x60,0x7E,0x63,0x7E,0x00}, //a
    [98] = {0x03,0x03,0x3F,0x63,0x63,0x63,0x3F,0x00}, //b
    [99] = {0x00,0x00,0x3E,0x63,0x03,0x63,0x3E,0x00}, //c
    [100] = {0x60,0x60,0x7E,0x63,0x63,0x63,0x7E,0x00}, //d
    [101] = {0x00,0x00,0x3E,0x63,0x7F,0x03,0x3E,0x00}, //e
    [102] = {0x1C,0x36,0x06,0x1F,0x06,0x06,0x06,0x00}, //f
    [103] = {0x00,0x00,0x7E,0x63,0x63,0x7E,0x60,0x3E}, //g
    [104] = {0x03,0x03,0x3F,0x63,0x63,0x63,0x63,0x00}, //h
    [105] = {0x18,0x00,0x1C,0x18,0x18,0x18,0x3C,0x00}, //i
    [106] = {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, //j
    [107] = {0x03,0x03,0x33,0x1B,0x0F,0x1B,0x33,0x00}, //k
    [108] = {0x1C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, //l
    [109] = {0x00,0x00,0x37,0x7F,0x6B,0x6B,0x63,0x00}, //m
    [110] = {0x00,0x00,0x3F,0x63,0x63,0x63,0x63,0x00}, //n
    [111] = {0x00,0x00,0x3E,0x63,0x63,0x63,0x3E,0x00}, //o
    [112] = {0x00,0x00,0x3F,0x63,0x63,0x3F,0x03,0x03}, //p
    [113] = {0x00,0x00,0x7E,0x63,0x63,0x7E,0x60,0x60}, //q
    [114] = {0x00,0x00,0x3B,0x6E,0x06,0x06,0x06,0x00}, //r
    [115] = {0x00,0x00,0x3E,0x03,0x3E,0x60,0x3F,0x00}, //s
    [116] = {0x08,0x0C,0x3E,0x0C,0x0C,0x6C,0x38,0x00}, //t
    [117] = {0x00,0x00,0x63,0x63,0x63,0x63,0x7E,0x00}, //u
    [118] = {0x00,0x00,0x63,0x63,0x36,0x1C,0x08,0x00}, //v
    [119] = {0x00,0x00,0x63,0x6B,0x6B,0x7F,0x36,0x00}, //w
    [120] = {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, //x
    [121] = {0x00,0x00,0x63,0x63,0x63,0x7E,0x60,0x3E}, //y
    [122] = {0x00,0x00,0x7F,0x30,0x18,0x0C,0x7F,0x00}, //z
    [123] = {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, //{
    [124] = {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, //|
    [125] = {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, //}
    [126] = {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, //~
    [127] = {0},
};

static void font_unload(void) {
    if (g_font.allocated_data) {
        free(g_font.allocated_data);
        g_font.allocated_data = NULL;
    }
    g_font.glyph_data = NULL;
    g_font.glyph_count = 0;
    g_font.glyph_height = 0;
    g_font.glyph_width = 0;
    g_font.glyph_stride = 0;
    g_font.lsb_first = 0;
}

static void log_serial(const char* fmt, ...) {
    if (serial_fd < 0) {
        serial_fd = open("/dev/serial0", O_WRONLY);
    }
    if (serial_fd < 0) {
        return;
    }

    char buffer[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (len <= 0) {
        return;
    }
    if (len > (int)sizeof(buffer)) {
        len = (int)sizeof(buffer);
    }
    write(serial_fd, buffer, (size_t)len);
}

static uint32_t* wait_for_window_buffer(const char* label, fwm_window_t window) {
    if (!g_de.conn || !window) return NULL;

    const int max_attempts = 50;
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        uint32_t* buf = fwm_get_buffer(g_de.conn, window);
        if (buf) {
            log_serial("FrostyDE: %s buffer ready at %p\n", label, (void*)buf);
            return buf;
        }
        usleep(1000);
    }

    log_serial("FrostyDE: Timed out waiting for %s buffer\n", label);
    return NULL;
}

static int font_load_psf(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return -1;
    }

    size_t file_size = (size_t)st.st_size;
    uint8_t* file_data = (uint8_t*)malloc(file_size);
    if (!file_data) {
        close(fd);
        return -1;
    }

    size_t offset = 0;
    while (offset < file_size) {
        ssize_t r = read(fd, file_data + offset, file_size - offset);
        if (r <= 0) {
            free(file_data);
            close(fd);
            return -1;
        }
        offset += (size_t)r;
    }

    close(fd);

    int success = -1;
    if (file_size >= 32) {
        const uint32_t* header32 = (const uint32_t*)file_data;
        if (header32[0] == 0x864ab572) { //PSF2 magic (little endian)
            uint32_t header_size = header32[2];
            uint32_t length = header32[4];
            uint32_t charsize = header32[5];
            uint32_t height = header32[6];
            uint32_t width = header32[7];
            if (width >= 8 && width <= 32 && height >= 8 && height <= 64 && length >= 256) {
                size_t glyph_bytes = (size_t)length * charsize;
                if (header_size + glyph_bytes <= file_size && charsize != 0) {
                    uint8_t* glyphs = (uint8_t*)malloc(glyph_bytes);
                    if (glyphs) {
                        memcpy(glyphs, file_data + header_size, glyph_bytes);
                        font_unload();
                        g_font.glyph_width = (int)width;
                        g_font.glyph_height = (int)height;
                        g_font.glyph_stride = (int)((width + 7) / 8);
                        g_font.glyph_count = (int)length;
                        g_font.allocated_data = glyphs;
                        g_font.glyph_data = glyphs;
                        g_font.lsb_first = 0;
                        success = 0;
                    }
                }
            }
        }
    }

    if (success != 0 && file_size >= 4) {
        const uint8_t* header8 = file_data;
        if (header8[0] == 0x36 && header8[1] == 0x04) { //PSF1 magic
            uint8_t mode = header8[2];
            uint8_t charsize = header8[3];
            uint32_t length = (mode & 0x01) ? 512u : 256u;
            size_t glyph_bytes = (size_t)length * charsize;
            if (4 + glyph_bytes <= file_size && charsize > 0) {
                uint8_t* glyphs = (uint8_t*)malloc(glyph_bytes);
                if (glyphs) {
                    memcpy(glyphs, file_data + 4, glyph_bytes);
                    font_unload();
                    g_font.glyph_width = 8;
                    g_font.glyph_height = charsize;
                    g_font.glyph_stride = 1;
                    g_font.glyph_count = (int)length;
                    g_font.allocated_data = glyphs;
                    g_font.glyph_data = glyphs;
                    g_font.lsb_first = 0;
                    success = 0;
                }
            }
        }
    }

    free(file_data);
    return success;
}

static void font_init(void) {
    if (font_load_psf("/etc/font.psf") == 0) {
        return;
    }

    font_unload();
    g_font.glyph_width = 8;
    g_font.glyph_height = 8;
    g_font.glyph_stride = 1;
    g_font.glyph_count = 128;
    g_font.glyph_data = (const uint8_t*)g_fallback_font;
    g_font.allocated_data = NULL;
    g_font.lsb_first = 1;
}

static const uint8_t* font_get_glyph(unsigned char ch) {
    unsigned char index = ch;
    if (g_font.glyph_count > 0 && index >= (unsigned int)g_font.glyph_count) {
        index = '?';
    }
    if (!g_font.glyph_data) {
        return g_fallback_font[index];
    }
    size_t glyph_size = (size_t)g_font.glyph_stride * (size_t)g_font.glyph_height;
    return g_font.glyph_data + (size_t)index * glyph_size;
}

//draw a filled rectangle in a buffer
static void draw_rect(uint32_t* buffer, uint32_t buf_width, uint32_t buf_height,
                      int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { 
        w += x; 
        x = 0; 
    }
    if (y < 0) { 
        h += y; 
        y = 0;
    }
    if (x >= (int)buf_width || y >= (int)buf_height) return;
    if (x + w > (int)buf_width) w = buf_width - x;
    if (y + h > (int)buf_height) h = buf_height - y;
    if (w <= 0 || h <= 0) return;
    
    for (int dy = 0; dy < h; dy++) {
        uint32_t* row = buffer + (y + dy) * buf_width + x;
        for (int dx = 0; dx < w; dx++) {
            row[dx] = color;
        }
    }
}

static void draw_char(uint32_t* buffer, uint32_t buf_width, uint32_t buf_height,
                      int x, int y, unsigned char ch, uint32_t color) {
    const uint8_t* glyph = font_get_glyph(ch);
    if (!glyph) return;

    for (int row = 0; row < g_font.glyph_height; row++) {
        int dst_y = y + row;
        if (dst_y < 0 || dst_y >= (int)buf_height) continue;

        const uint8_t* glyph_row = glyph + row * g_font.glyph_stride;
        for (int col = 0; col < g_font.glyph_width; col++) {
            int dst_x = x + col;
            if (dst_x < 0 || dst_x >= (int)buf_width) continue;

            int byte_index = col / 8;
            int bit_index = col % 8;
            uint8_t mask = g_font.lsb_first ? (uint8_t)(1u << bit_index)
                                            : (uint8_t)(1u << (7 - bit_index));
            if (glyph_row[byte_index] & mask) {
                buffer[dst_y * buf_width + dst_x] = color;
            }
        }
    }
}

static void draw_text(uint32_t* buffer, uint32_t buf_width, uint32_t buf_height,
                      int x, int y, const char* text, uint32_t color) {
    if (!text) return;
    int advance = g_font.glyph_width ? (g_font.glyph_width + 1) : 9;
    int cursor_x = x;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        draw_char(buffer, buf_width, buf_height, cursor_x, y, *p, color);
        cursor_x += advance;
    }
}

//launch FrostyWM as a child process
static int launch_frostywm() {
    log_serial("FrostyDE: Launching FrostyWM display server...\n");
    
    pid_t pid = fork();
    if (pid < 0) {
        log_serial("FrostyDE: Failed to fork for FrostyWM\n");
        return -1;
    }
    
    if (pid == 0) {
        //child process - exec FrostyWM
        char* argv[] = {"/bin/frostywm", NULL};
        char* envp[] = {NULL};
        execve("/bin/frostywm", argv, envp);
        
        // If execve returns, it failed
        log_serial("FrostyDE: Failed to exec FrostyWM\n");
        exit(1);
    }
    
    //parent process - wait a moment for FrostyWM to start
    log_serial("FrostyDE: FrostyWM started with PID %d\n", pid);
    log_serial("FrostyDE: Waiting for FrostyWM to initialize...\n");
    sleep(1);  //give FrostyWM time to create its socket
    
    return 0;
}

//initialize the desktop environment
static int init_desktop() {
    //launch FrostyWM first
    if (launch_frostywm() != 0) {
        log_serial("FrostyDE: Failed to launch FrostyWM\n");
        return -1;
    }
    
    //connect to FrostyWM
    log_serial("FrostyDE: Connecting to FrostyWM...\n");
    
    //try to connect with retry
    int retries = 5;
    while (retries > 0) {
        g_de.conn = fwm_connect("FrostyDE");
        if (g_de.conn) break;
        
        log_serial("FrostyDE: Connection failed, retrying... (%d left)\n", retries);
        sleep(1);
        retries--;
    }
    
    if (!g_de.conn) {
        log_serial("FrostyDE: Failed to connect to FrostyWM after retries\n");
        log_serial("FrostyDE: Check if /bin/frostywm exists\n");
        return -1;
    }
    
    g_de.screen_w = fwm_get_screen_width(g_de.conn);
    g_de.screen_h = fwm_get_screen_height(g_de.conn);
    
    log_serial("FrostyDE: Connected! Screen: %ux%u\n", g_de.screen_w, g_de.screen_h);
    
    //create desktop background window
    log_serial("FrostyDE: Creating desktop window...\n");
    g_de.desktop_window = fwm_create_window(g_de.conn, 0, 0,
                                            g_de.screen_w, g_de.screen_h - PANEL_HEIGHT,
                                            "Desktop");
    if (!g_de.desktop_window) {
        log_serial("FrostyDE: Failed to create desktop window\n");
        return -1;
    }
    
    g_de.desktop_buffer = wait_for_window_buffer("desktop", g_de.desktop_window);
    if (!g_de.desktop_buffer) {
        log_serial("FrostyDE: Failed to get desktop buffer\n");
        return -1;
    }
    
    //create panel window
    log_serial("FrostyDE: Creating panel window...\n");
    g_de.panel_window = fwm_create_window(g_de.conn, 0, g_de.screen_h - PANEL_HEIGHT,
                                          g_de.screen_w, PANEL_HEIGHT,
                                          "Panel");
    if (!g_de.panel_window) {
        log_serial("FrostyDE: Failed to create panel window\n");
        return -1;
    }
    
    g_de.panel_buffer = wait_for_window_buffer("panel", g_de.panel_window);
    if (!g_de.panel_buffer) {
        log_serial("FrostyDE: Failed to get panel buffer\n");
        return -1;
    }
    
    //create test application window
    log_serial("FrostyDE: Creating test window...\n");
    g_de.test_window = fwm_create_window(g_de.conn, 100, 100, 400, 300, "Terminal");
    if (!g_de.test_window) {
        log_serial("FrostyDE: Failed to create test window\n");
        return -1;
    }
    
    g_de.test_buffer = wait_for_window_buffer("test", g_de.test_window);
    if (!g_de.test_buffer) {
        log_serial("FrostyDE: Failed to get test buffer\n");
        return -1;
    }
    
    //show all windows
    fwm_show_window(g_de.conn, g_de.desktop_window);
    fwm_show_window(g_de.conn, g_de.panel_window);
    fwm_show_window(g_de.conn, g_de.test_window);

    g_de.running = 1;
    
    log_serial("FrostyDE: Desktop initialized successfully!\n");
    return 0;
}
//render the desktop background
static void render_desktop() {
    if (!g_de.desktop_buffer) {
        g_de.desktop_buffer = wait_for_window_buffer("desktop(retry)", g_de.desktop_window);
        if (!g_de.desktop_buffer) {
            DE_DEBUG_LOG("FrostyDE: render_desktop skipped - buffer not ready\n");
            return;
        }
    }
    uint32_t w = g_de.screen_w;
    uint32_t h = g_de.screen_h - PANEL_HEIGHT;
    
    //fill with desktop background color
    draw_rect(g_de.desktop_buffer, w, h, 0, 0, w, h, COLOR_DESKTOP_BG);
    
    //draw "FrostByte" text in center
    const char* text = "FrostByte Desktop";
    int text_x = (w / 2) - (strlen(text) * 4);
    int text_y = h / 2;
    draw_text(g_de.desktop_buffer, w, h, text_x, text_y, text, COLOR_WHITE);
    
    fwm_damage(g_de.conn, g_de.desktop_window, 0, 0, w, h);
    fwm_commit(g_de.conn, g_de.desktop_window);
}

//format clock text
static void format_clock_text(char* out, size_t len) {
    time_t now;
    if (time(&now) != (time_t)-1) {
        struct tm* tm_now = localtime(&now);
        if (tm_now) {
            snprintf(out, len, "%02d:%02d", tm_now->tm_hour, tm_now->tm_min);
            return;
        }
    }

    uint32_t minutes = (g_clock_fallback / 60) % 60;
    uint32_t seconds = g_clock_fallback % 60;
    snprintf(out, len, "%02u:%02u", minutes, seconds);
    g_clock_fallback = (g_clock_fallback + 1) % 3600;
}

static void render_panel(int force_full) {
    if (!g_de.panel_buffer) {
        g_de.panel_buffer = wait_for_window_buffer("panel(retry)", g_de.panel_window);
        if (!g_de.panel_buffer) {
            DE_DEBUG_LOG("FrostyDE: render_panel skipped - buffer not ready\n");
            return;
        }
    }
    uint32_t w = g_de.screen_w;
    uint32_t h = PANEL_HEIGHT;

    int glyph_w = g_font.glyph_width ? g_font.glyph_width : 6;
    int glyph_h = g_font.glyph_height ? g_font.glyph_height : 12;
    int clock_chars = 5;
    int text_spacing = glyph_w + 1;
    int clock_width = clock_chars * text_spacing;
    int clock_height = glyph_h;
    int clock_x = (int)w - clock_width - 16;
    if (clock_x < 0) clock_x = 0;
    int clock_y = (int)h - glyph_h - 4;
    if (clock_y < 0) clock_y = 0;

    if (force_full) {
        draw_rect(g_de.panel_buffer, w, h, 0, 0, w, h, COLOR_PANEL_BG);
        draw_text(g_de.panel_buffer, w, h, 8, (int)h - glyph_h - 4, "FrostByte", COLOR_PANEL_TEXT);
        memset(g_clock_text, 0, sizeof(g_clock_text));
    }

    char new_clock[6];
    format_clock_text(new_clock, sizeof(new_clock));

    if (force_full || strcmp(new_clock, g_clock_text) != 0) {
        snprintf(g_clock_text, sizeof(g_clock_text), "%s", new_clock);
        draw_rect(g_de.panel_buffer, w, h, clock_x, clock_y, clock_width, clock_height, COLOR_PANEL_BG);
        draw_text(g_de.panel_buffer, w, h, clock_x, clock_y, g_clock_text, COLOR_PANEL_TEXT);

        if (force_full) {
            fwm_damage(g_de.conn, g_de.panel_window, 0, 0, w, h);
        } else {
            fwm_damage(g_de.conn, g_de.panel_window, clock_x, clock_y, clock_width, clock_height);
        }
        fwm_commit(g_de.conn, g_de.panel_window);
    }
}

//render test window content
static void render_test_window() {
    if (!g_de.test_buffer) {
        g_de.test_buffer = wait_for_window_buffer("test(retry)", g_de.test_window);
        if (!g_de.test_buffer) {
            DE_DEBUG_LOG("FrostyDE: render_test_window skipped - buffer not ready\n");
            return;
        }
    }
    uint32_t w = 400;
    uint32_t h = 300;
    
    //fill with window background
    draw_rect(g_de.test_buffer, w, h, 0, 0, w, h, COLOR_WINDOW_BG);
    
    //draw some test content
    draw_text(g_de.test_buffer, w, h, 10, 10, "Terminal Window", COLOR_WINDOW_TEXT);
    draw_text(g_de.test_buffer, w, h, 10, 30, "Type commands here...", COLOR_WINDOW_TEXT);
    
    //draw a button
    draw_rect(g_de.test_buffer, w, h, 10, 60, 80, 24, COLOR_BUTTON_BG);
    draw_text(g_de.test_buffer, w, h, 20, 68, "Button", COLOR_WHITE);
    
    //mark as damaged
    fwm_damage(g_de.conn, g_de.test_window, 0, 0, w, h);
    fwm_commit(g_de.conn, g_de.test_window);
}

//main event loop
static void event_loop() {
    log_serial("FrostyDE: Starting event loop...\n");

    //initial render
    render_desktop();
    render_panel(1);
    render_test_window();

    while (g_de.running) {
        fwm_event_t event;
        while (fwm_poll_event(g_de.conn, &event) > 0) {
            DE_DEBUG_LOG("FrostyDE: Got event type %u for window %u\n", event.type, event.window);
        }

        DE_DEBUG_LOG("FrostyDE: Rendering frame - buffers desktop=%p panel=%p test=%p\n",
                     (void*)g_de.desktop_buffer, (void*)g_de.panel_buffer, (void*)g_de.test_buffer);

        render_panel(0);

        usleep(8000);
    }
}

//cleanup
static void cleanup() {
    if (g_de.conn) {
        if (g_de.test_window) fwm_destroy_window(g_de.conn, g_de.test_window);
        if (g_de.panel_window) fwm_destroy_window(g_de.conn, g_de.panel_window);
        if (g_de.desktop_window) fwm_destroy_window(g_de.conn, g_de.desktop_window);
        fwm_disconnect(g_de.conn);
    }
    font_unload();
}

int main(int argc, char** argv) {
    (void)argc; 
    (void)argv;
        
    memset(&g_de, 0, sizeof(g_de));
    font_init();
    
    if (init_desktop() != 0) {
        log_serial("FrostyDE: Failed to initialize desktop\n");
        cleanup();
        return 1;
    }
    
    log_serial("FrostyDE: Starting desktop...\n");
    event_loop();

    cleanup();
    log_serial("FrostyDE: Shutdown complete\n");
    
    return 0;
}
