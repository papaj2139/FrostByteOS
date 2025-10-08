#include "fwm_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <limits.h>

//ensure EAGAIN and EWOULDBLOCK are defined
#ifndef EAGAIN
#define EAGAIN 11
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

//stubs shit
#define perror(msg) do { (void)msg; } while(0)
#define stderr ((void*)2)
#define fprintf(stream, ...) printf(__VA_ARGS__)
#define SEEK_SET 0

#define FB_IOCTL_BLIT        0x0001u
#define FB_IOCTL_SET_CONSOLE 0x0002u

#define MAX_CLIENTS 16
#define MAX_WINDOWS 64
#define MAX_WINDOW_DIM 16384u

#define CURSOR_WIDTH 14
#define CURSOR_HEIGHT 18

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t src_pitch;
    uint32_t flags;
    const void* src;
} fb_blit_args_t;

typedef struct {
    uint32_t id;
    uint32_t client_id;
    int32_t x, y;
    uint32_t width, height;
    char title[128];
    int visible;
    int focused;
    uint32_t shm_key;
    void* buffer;
    int shm_id;
    int dirty;
} fwm_window_t;

typedef struct {
    uint32_t id;
    int fd;
    char app_name[64];
    int active;
} fwm_client_t;

typedef struct {
    int listen_fd;
    fwm_client_t clients[MAX_CLIENTS];
    fwm_window_t windows[MAX_WINDOWS];
    int num_clients;
    int num_windows;
    uint32_t next_client_id;
    uint32_t next_window_id;
    uint32_t next_shm_key;
    
    //framebuffer state
    int fb_fd;
    uint8_t* fb;
    uint32_t screen_width;
    uint32_t screen_height;
    size_t framebuffer_size;
    uint32_t fb_pitch_bytes;
    uint32_t fb_stride_pixels;
    uint32_t fb_bpp;
    uint32_t fb_bytes_per_pixel;
    struct {
        int valid;
        int32_t x1, y1, x2, y2;
    } dirty_rect;
    int first_frame;
    //mouse state
    int mouse_fd;
    int32_t mouse_x;
    int32_t mouse_y;
    uint8_t mouse_buttons;
    fwm_window_t* focused_window;
    
    uint8_t* backbuffer;
    uint8_t* cursor_backup;
    int cursor_backup_valid;
    int32_t cursor_backup_x;
    int32_t cursor_backup_y;
    uint32_t cursor_backup_w;
    uint32_t cursor_backup_h;
} fwm_server_t;

static fwm_server_t g_server;
static int g_serial_fd = -1;
static unsigned g_cursor_backup_miss = 0;
static unsigned g_cursor_restore_fail = 0;
static unsigned g_cursor_draw_skip = 0;

static void log_serial(const char* fmt, ...) {
    if (g_serial_fd < 0) {
        g_serial_fd = open("/dev/serial0", O_WRONLY);
    }
    if (g_serial_fd < 0) return;

    char buffer[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (len <= 0) return;
    if (len > (int)sizeof(buffer)) len = (int)sizeof(buffer);
    write(g_serial_fd, buffer, (size_t)len);
}

#ifndef WM_DEBUG_LOGS
#define WM_DEBUG_LOGS 0
#endif

#if WM_DEBUG_LOGS
#define WM_DEBUG_LOG(...) log_serial(__VA_ARGS__)
#else
#define WM_DEBUG_LOG(...) do { } while (0)
#endif

//optional timing jitter injection for IPC/socket paths (for heisenbug hunting)
#ifndef WM_JITTER_ENABLE
#define WM_JITTER_ENABLE 1
#endif
#ifndef WM_JITTER_MIN_USEC
#define WM_JITTER_MIN_USEC 500
#endif
#ifndef WM_JITTER_MAX_USEC
#define WM_JITTER_MAX_USEC 1000
#endif

#if WM_JITTER_ENABLE
static inline void wm_jitter(unsigned usec_min, unsigned usec_max) {
    if (usec_max < usec_min) usec_max = usec_min;
    unsigned span = usec_max - usec_min;
    unsigned r = (unsigned)rand();
    unsigned d = usec_min + (span ? (r % span) : 0u);
    if (d) usleep(d);
}
#else
static inline void wm_jitter(unsigned a, unsigned b) { (void)a; (void)b; }
#endif

#if WM_DEBUG_LOGS
static inline void dump_cursor_stats(void) {
    WM_DEBUG_LOG("FrostyWM: cursor stats: backup miss=%u restore fail=%u draw skip=%u\n",
                 g_cursor_backup_miss, g_cursor_restore_fail, g_cursor_draw_skip);
}
#else
static inline void dump_cursor_stats(void) { (void)0; }
#endif

static void mark_dirty_region(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;
    int32_t x1 = x;
    int32_t y1 = y;
    int32_t x2 = x + (int32_t)w;
    int32_t y2 = y + (int32_t)h;

    if (x2 <= x1 || y2 <= y1) return;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int32_t)g_server.screen_width) x2 = (int32_t)g_server.screen_width;
    if (y2 > (int32_t)g_server.screen_height) y2 = (int32_t)g_server.screen_height;

    if (x1 >= x2 || y1 >= y2) return;

    if (!g_server.dirty_rect.valid) {
        g_server.dirty_rect.valid = 1;
        g_server.dirty_rect.x1 = x1;
        g_server.dirty_rect.y1 = y1;
        g_server.dirty_rect.x2 = x2;
        g_server.dirty_rect.y2 = y2;
    } else {
        if (x1 < g_server.dirty_rect.x1) g_server.dirty_rect.x1 = x1;
        if (y1 < g_server.dirty_rect.y1) g_server.dirty_rect.y1 = y1;
        if (x2 > g_server.dirty_rect.x2) g_server.dirty_rect.x2 = x2;
        if (y2 > g_server.dirty_rect.y2) g_server.dirty_rect.y2 = y2;
    }
}

static void mark_entire_screen(void) {
    mark_dirty_region(0, 0, g_server.screen_width, g_server.screen_height);
}

static void get_cursor_visible_rect(int32_t x, int32_t y, int32_t* out_x, int32_t* out_y, int32_t* out_w, int32_t* out_h);

static void mark_cursor_dirty_area(int32_t x, int32_t y) {
    int32_t rx, ry, rw, rh;
    get_cursor_visible_rect(x, y, &rx, &ry, &rw, &rh);
    if (rw <= 0 || rh <= 0) return;
    mark_dirty_region(rx, ry, (uint32_t)rw, (uint32_t)rh);
}

static void get_cursor_visible_rect(int32_t x, int32_t y, int32_t* out_x, int32_t* out_y, int32_t* out_w, int32_t* out_h) {
    if (!out_x || !out_y || !out_w || !out_h) {
        return;
    }

    int32_t x0 = x;
    int32_t y0 = y;
    int32_t x1 = x + CURSOR_WIDTH;
    int32_t y1 = y + CURSOR_HEIGHT;

    if (x1 <= 0 || y1 <= 0 || x0 >= (int32_t)g_server.screen_width || y0 >= (int32_t)g_server.screen_height) {
        *out_x = 0;
        *out_y = 0;
        *out_w = 0;
        *out_h = 0;
        return;
    }

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)g_server.screen_width) x1 = (int32_t)g_server.screen_width;
    if (y1 > (int32_t)g_server.screen_height) y1 = (int32_t)g_server.screen_height;

    *out_x = x0;
    *out_y = y0;
    *out_w = x1 - x0;
    *out_h = y1 - y0;
}

static void mark_window_area(const fwm_window_t* win) {
    if (!win) return;
    mark_dirty_region(win->x, win->y, win->width, win->height);
}

static void mark_window_subrect(const fwm_window_t* win, int32_t rel_x, int32_t rel_y, uint32_t w, uint32_t h) {
    if (!win) return;
    int32_t abs_x = win->x + rel_x;
    int32_t abs_y = win->y + rel_y;
    mark_dirty_region(abs_x, abs_y, w, h);
}

static size_t cursor_buffer_size(void) {
    return (size_t)CURSOR_WIDTH * CURSOR_HEIGHT * g_server.fb_bytes_per_pixel;
}

//cursor backup canary helpers (detect overwrite bugs)
static void cursor_canary_set(void) {
    if (!g_server.cursor_backup) return;
    size_t cb = cursor_buffer_size();
    //reserve 16 bytes after logical buffer for canary
    memset(g_server.cursor_backup + cb, 0xA5, 16);
}
static int cursor_canary_ok(void) {
    if (!g_server.cursor_backup) return 1;
    size_t cb = cursor_buffer_size();
    for (int i = 0; i < 16; i++) {
        if (g_server.cursor_backup[cb + i] != (uint8_t)0xA5) return 0;
    }
    return 1;
}

//backbuffer canary helpers (detect compositor overrun)
static inline size_t backbuffer_bytes(void) {
    return g_server.framebuffer_size;
}
static void backbuffer_canary_set(void) {
    if (!g_server.backbuffer) return;
    size_t sz = backbuffer_bytes();
    memset(g_server.backbuffer + sz, 0x5A, 16);
}
static int backbuffer_canary_ok(void) {
    if (!g_server.backbuffer) return 1;
    size_t sz = backbuffer_bytes();
    for (int i = 0; i < 16; i++) {
        if (g_server.backbuffer[sz + i] != (uint8_t)0x5A) return 0;
    }
    return 1;
}

static void save_cursor_underlay(int32_t x, int32_t y) {
    if (!g_server.backbuffer || !g_server.cursor_backup) {
        g_cursor_backup_miss++;
        WM_DEBUG_LOG("FrostyWM: save_cursor_underlay skipped (backbuffer=%p cursor_backup=%p)\n",
                     (void*)g_server.backbuffer, (void*)g_server.cursor_backup);
        return;
    }

    int32_t clip_x, clip_y, clip_w, clip_h;
    get_cursor_visible_rect(x, y, &clip_x, &clip_y, &clip_w, &clip_h);
    if (clip_w <= 0 || clip_h <= 0) {
        g_server.cursor_backup_valid = 0;
        g_server.cursor_backup_w = 0;
        g_server.cursor_backup_h = 0;
        return;
    }

    size_t row_bytes = (size_t)clip_w * g_server.fb_bytes_per_pixel;
    for (int32_t row = 0; row < clip_h; row++) {
        size_t src_offset = (size_t)(clip_y + row) * g_server.fb_pitch_bytes +
                            (size_t)clip_x * g_server.fb_bytes_per_pixel;
        const uint8_t* src = g_server.backbuffer + src_offset;
        uint8_t* dst = g_server.cursor_backup + (size_t)row * row_bytes;
        memcpy(dst, src, row_bytes);
    }

    g_server.cursor_backup_x = clip_x;
    g_server.cursor_backup_y = clip_y;
    g_server.cursor_backup_w = (uint32_t)clip_w;
    g_server.cursor_backup_h = (uint32_t)clip_h;
    g_server.cursor_backup_valid = 1;
    if (!cursor_canary_ok()) {
        log_serial("FrostyWM: cursor backup canary corrupted after save\n");
        g_server.cursor_backup_valid = 0;
        cursor_canary_set();
    }
}
static void restore_cursor_underlay(void) {
    if (!g_server.backbuffer || !g_server.cursor_backup || !g_server.cursor_backup_valid) {
        g_cursor_restore_fail++;
        WM_DEBUG_LOG("FrostyWM: restore_cursor_underlay skipped (backbuffer=%p cursor_backup=%p valid=%d)\n",
                     (void*)g_server.backbuffer, (void*)g_server.cursor_backup, g_server.cursor_backup_valid);
        return;
    }

    int32_t x = g_server.cursor_backup_x;
    int32_t y = g_server.cursor_backup_y;
    uint32_t width = g_server.cursor_backup_w;
    uint32_t height = g_server.cursor_backup_h;

    if (width == 0 || height == 0) {
        g_server.cursor_backup_valid = 0;
        return;
    }

    if (x < 0 || y < 0 ||
        x + (int32_t)width > (int32_t)g_server.screen_width ||
        y + (int32_t)height > (int32_t)g_server.screen_height) {
        g_server.cursor_backup_valid = 0;
        g_server.cursor_backup_w = 0;
        g_server.cursor_backup_h = 0;
        return;
    }

    size_t row_bytes = (size_t)width * g_server.fb_bytes_per_pixel;
    for (uint32_t row = 0; row < height; row++) {
        size_t dst_offset = (size_t)(y + (int32_t)row) * g_server.fb_pitch_bytes +
                            (size_t)x * g_server.fb_bytes_per_pixel;
        uint8_t* dst = g_server.backbuffer + dst_offset;
        const uint8_t* src = g_server.cursor_backup + (size_t)row * row_bytes;
        memcpy(dst, src, row_bytes);
    }

    g_server.cursor_backup_valid = 0;
    g_server.cursor_backup_w = 0;
    g_server.cursor_backup_h = 0;
    if (!cursor_canary_ok()) {
        log_serial("FrostyWM: cursor backup canary corrupted after restore\n");
        cursor_canary_set();
    }
}

typedef struct {
    unsigned time_ms;
    short rel_x;
    short rel_y;
    unsigned char type;
    unsigned char button;
    unsigned short reserved;
} fwm_mouse_event_t;

static inline void write_pixel(uint8_t* dst, uint32_t color) {
    switch (g_server.fb_bytes_per_pixel) {
        case 4:
            *((uint32_t*)dst) = color;
            break;
        case 3:
            dst[0] = (uint8_t)(color & 0xFF);
            dst[1] = (uint8_t)((color >> 8) & 0xFF);
            dst[2] = (uint8_t)((color >> 16) & 0xFF);
            break;
        case 2: {
            uint32_t r = (color >> 16) & 0xFF;
            uint32_t g = (color >> 8) & 0xFF;
            uint32_t b = color & 0xFF;
            uint16_t packed = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            *((uint16_t*)dst) = packed;
            break;
        }
        default:
            dst[0] = (uint8_t)(color & 0xFF);
            break;
    }
}

static void draw_cursor_sprite(void) {
    if (!g_server.backbuffer) {
        g_cursor_draw_skip++;
        WM_DEBUG_LOG("FrostyWM: draw_cursor_sprite skipped (backbuffer=NULL)\n");
        return;
    }
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

    int base_x = g_server.mouse_x;
    int base_y = g_server.mouse_y;

    for (int dy = 0; dy < 18; dy++) {
        for (int dx = 0; dx < 14; dx++) {
            unsigned char pixel = cursor_data[dy][dx];
            if (pixel == 0) continue;

            int x = base_x + dx;
            int y = base_y + dy;

            if (x < 0 || y < 0 ||
                x >= (int)g_server.screen_width ||
                y >= (int)g_server.screen_height) {
                continue;
            }

            uint32_t color = (pixel == 1) ? 0xFF000000u : 0xFFFFFFFFu;
            uint8_t* dst = g_server.backbuffer + (size_t)y * g_server.fb_pitch_bytes + (size_t)x * g_server.fb_bytes_per_pixel;
            write_pixel(dst, color);
        }
    }
}

static void detect_framebuffer_geometry(uint32_t* out_w, uint32_t* out_h, uint32_t* out_pitch, uint32_t* out_bpp) {
    if (!out_w || !out_h || !out_pitch || !out_bpp) return;

    *out_w = 0;
    *out_h = 0;
    *out_pitch = 0;
    *out_bpp = 0;

    int fd = open("/proc/fb0", O_RDONLY);
    if (fd < 0) {
        return;
    }

    char buf[256];
    int r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (r <= 0) {
        return;
    }

    buf[r] = '\0';

    char* p = buf;
    while (*p) {
        if (strncmp(p, "width:", 6) == 0) {
            *out_w = (uint32_t)atoi(p + 6);
        } else if (strncmp(p, "height:", 7) == 0) {
            *out_h = (uint32_t)atoi(p + 7);
        } else if (strncmp(p, "pitch:", 6) == 0) {
            *out_pitch = (uint32_t)atoi(p + 6);
        } else if (strncmp(p, "bpp:", 4) == 0) {
            *out_bpp = (uint32_t)atoi(p + 4);
        }

        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

static void init_framebuffer() {
    WM_DEBUG_LOG("FrostyWM: init_framebuffer begin\n");
    g_server.fb_fd = open("/dev/fb0", O_RDWR);
    if (g_server.fb_fd < 0) {
        fprintf(stderr, "Failed to open framebuffer\n");
        exit(1);
    }

    uint32_t detected_w = 0, detected_h = 0;
    uint32_t detected_pitch = 0, detected_bpp = 0;
    detect_framebuffer_geometry(&detected_w, &detected_h, &detected_pitch, &detected_bpp);

    if (detected_w == 0 || detected_h == 0) {
        fprintf(stderr, "FrostyWM: Failed to read framebuffer size, falling back to 800x600\n");
        detected_w = 800;
        detected_h = 600;
    }

    if (detected_pitch == 0) {
        detected_pitch = detected_w * 4;
    }

    if (detected_bpp == 0) {
        detected_bpp = 32;
    }

    g_server.screen_width = detected_w;
    g_server.screen_height = detected_h;
    g_server.fb_pitch_bytes = detected_pitch;
    g_server.fb_bpp = detected_bpp;

    WM_DEBUG_LOG("FrostyWM: geometry %ux%u pitch=%u bpp=%u\n",
                 g_server.screen_width, g_server.screen_height,
                 g_server.fb_pitch_bytes, g_server.fb_bpp);

    uint32_t bytes_per_pixel = (g_server.fb_bpp >= 8) ? ((g_server.fb_bpp + 7) / 8) : 4;
    if (bytes_per_pixel == 0) bytes_per_pixel = 4;
    g_server.fb_bytes_per_pixel = bytes_per_pixel;
    g_server.fb_stride_pixels = g_server.fb_pitch_bytes / bytes_per_pixel;
    if (g_server.fb_stride_pixels == 0) {
        g_server.fb_stride_pixels = g_server.screen_width;
        g_server.fb_pitch_bytes = g_server.fb_stride_pixels * g_server.fb_bytes_per_pixel;
    }

    //map framebuffer
    g_server.framebuffer_size = (size_t)g_server.fb_pitch_bytes * (size_t)g_server.screen_height;

    g_server.fb = mmap_ex(NULL,
                          g_server.framebuffer_size,
                          PROT_READ | PROT_WRITE,
                          0,
                          g_server.fb_fd,
                          0);
    if (g_server.fb == (void*)-1) {
        g_server.fb = NULL;
        fprintf(stderr, "FrostyWM: Warning - framebuffer mmap failed, falling back to write() blits\n");
    }

    //allocate backbuffer for compositor (+16 for canary)
    g_server.backbuffer = (uint8_t*)malloc(g_server.framebuffer_size + 16);
    if (!g_server.backbuffer) {
        fprintf(stderr, "FrostyWM: Failed to allocate backbuffer\n");
        exit(1);
    }
    backbuffer_canary_set();

    g_server.cursor_backup = (uint8_t*)malloc(cursor_buffer_size() + 16);
    if (!g_server.cursor_backup) {
        fprintf(stderr, "FrostyWM: Warning - failed to allocate cursor backup buffer\n");
        g_server.cursor_backup_h = 0;
    }
    cursor_canary_set();
    WM_DEBUG_LOG("FrostyWM: buffers backbuffer=%p cursor_backup=%p fb=%p size=%zu cursor_bytes=%zu\n",
                 (void*)g_server.backbuffer,
                 (void*)g_server.cursor_backup,
                 (void*)g_server.fb,
                 g_server.framebuffer_size,
                 cursor_buffer_size());
    g_server.cursor_backup_valid = 0;

    //disable framebuffer console so the compositor owns the display
    int disable_console = 0;
    if (ioctl(g_server.fb_fd, FB_IOCTL_SET_CONSOLE, &disable_console) != 0) {
        fprintf(stderr, "FrostyWM: Warning - failed to disable framebuffer console\n");
    }

    //clear both backbuffer and actual framebuffer
    memset(g_server.backbuffer, 0, g_server.framebuffer_size);
    if (g_server.fb_fd >= 0) {
        write(g_server.fb_fd, g_server.backbuffer, g_server.framebuffer_size);
    }

    g_server.first_frame = 1;
    g_server.dirty_rect.valid = 0;
    mark_entire_screen();
}

static void init_mouse() {
    WM_DEBUG_LOG("FrostyWM: init_mouse begin\n");
    g_server.mouse_fd = open("/dev/input/mouse", O_RDONLY | O_NONBLOCK);
    if (g_server.mouse_fd < 0) {
        //try blocking mode as fallback
        g_server.mouse_fd = open("/dev/input/mouse", O_RDONLY);
    }
    if (g_server.mouse_fd < 0) {
        fprintf(stderr, "Warning: Failed to open mouse device\n");
    }
    g_server.mouse_x = g_server.screen_width / 2;
    g_server.mouse_y = g_server.screen_height / 2;

    WM_DEBUG_LOG("FrostyWM: mouse ready fd=%d start=(%d,%d)\n",
                 g_server.mouse_fd, g_server.mouse_x, g_server.mouse_y);
    mark_cursor_dirty_area(g_server.mouse_x, g_server.mouse_y);
}

static int create_listen_socket() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    
    //remove existing socket file
    unlink(FWM_SOCKET_PATH);
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, FWM_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    
    if (listen(fd, MAX_CLIENTS) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    
    return fd;
}

static fwm_window_t* find_window(uint32_t window_id) {
    for (int i = 0; i < g_server.num_windows; i++) {
        if (g_server.windows[i].id == window_id) {
            return &g_server.windows[i];
        }
    }
    return NULL;
}

static int send_message(int fd, const void* msg, size_t len) {
    wm_jitter(WM_JITTER_MIN_USEC, WM_JITTER_MAX_USEC);
    const uint8_t* p = (const uint8_t*)msg;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w < 0) {
            if (errno == EAGAIN) { 
                usleep(1000); 
                continue; 
            }
            return -1;
        }
        if (w == 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

static void handle_connect(fwm_client_t* client, const fwm_msg_connect_t* msg) {
    fwm_reply_connect_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.header.type = FWM_REPLY_CONNECT_OK;
    reply.header.length = sizeof(reply);
    reply.header.seq = msg->header.seq;
    reply.client_id = client->id;
    reply.screen_width = g_server.screen_width;
    reply.screen_height = g_server.screen_height;
    
    wm_jitter(WM_JITTER_MIN_USEC, WM_JITTER_MAX_USEC);
    send_message(client->fd, &reply, sizeof(reply));
    
    strncpy(client->app_name, msg->app_name, sizeof(client->app_name) - 1);
    client->app_name[sizeof(client->app_name) - 1] = '\0';
    printf("FrostyWM: Client connected: %s (id=%u)\n", client->app_name, client->id);
}

static void handle_create_window(fwm_client_t* client, const fwm_msg_create_window_t* msg) {
    if (g_server.num_windows >= MAX_WINDOWS) {
        fwm_msg_header_t reply;
        reply.type = FWM_REPLY_ERROR;
        reply.length = sizeof(reply);
        reply.seq = msg->header.seq;
        send_message(client->fd, &reply, sizeof(reply));
        return;
    }
    
    //validate requested size and prevent overflow
    uint32_t w = msg->width;
    uint32_t h = msg->height;
    if (w == 0 || h == 0 || w > MAX_WINDOW_DIM || h > MAX_WINDOW_DIM) {
        fwm_msg_header_t reply;
        reply.type = FWM_REPLY_ERROR;
        reply.length = sizeof(reply);
        reply.seq = msg->header.seq;
        send_message(client->fd, &reply, sizeof(reply));
        return;
    }
    //ensure w * h * 4 does not overflow 32-bit
    if (h != 0 && w > (UINT32_MAX / 4u) / h) {
        fwm_msg_header_t reply;
        reply.type = FWM_REPLY_ERROR;
        reply.length = sizeof(reply);
        reply.seq = msg->header.seq;
        send_message(client->fd, &reply, sizeof(reply));
        return;
    }

    //create shared memory for window buffer
    uint32_t shm_key = g_server.next_shm_key++;
    size_t buffer_size = (size_t)w * (size_t)h * 4u;
    int shm_id = shmget(shm_key, buffer_size, IPC_CREAT | 0666);
    if (shm_id < 0) {
        fwm_msg_header_t reply;
        reply.type = FWM_REPLY_ERROR;
        reply.length = sizeof(reply);
        reply.seq = msg->header.seq;
        send_message(client->fd, &reply, sizeof(reply));
        return;
    }
    
    void* buffer = shmat(shm_id, NULL, 0);
    if (buffer == (void*)-1) {
        shmctl(shm_id, IPC_RMID, NULL);
        fwm_msg_header_t reply;
        reply.type = FWM_REPLY_ERROR;
        reply.length = sizeof(reply);
        reply.seq = msg->header.seq;
        send_message(client->fd, &reply, sizeof(reply));
        return;
    }
    
    //timing jitter between SHM attach and buffer use
    wm_jitter(WM_JITTER_MIN_USEC, WM_JITTER_MAX_USEC);

    //clear buffer
    memset(buffer, 0xFF, buffer_size);
    
    //create window
    fwm_window_t* win = &g_server.windows[g_server.num_windows++];
    win->id = g_server.next_window_id++;
    win->client_id = client->id;
    win->x = msg->x;
    win->y = msg->y;
    win->width = w;
    win->height = h;
    strncpy(win->title, msg->title, sizeof(win->title) - 1);
    win->title[sizeof(win->title) - 1] = '\0';
    win->visible = 0;
    win->focused = 0;
    win->shm_key = shm_key;
    win->buffer = buffer;
    win->shm_id = shm_id;
    win->dirty = 1;

    mark_window_area(win);
    
    //send reply
    fwm_reply_window_created_t reply;
    memset(&reply, 0, sizeof(reply));
    reply.header.type = FWM_REPLY_WINDOW_CREATED;
    reply.header.length = sizeof(reply);
    reply.header.seq = msg->header.seq;
    reply.window_id = win->id;
    reply.shm_key = shm_key;
    
    send_message(client->fd, &reply, sizeof(reply));
    
    printf("FrostyWM: Window created: %s (%ux%u) id=%u\n", win->title, win->width, win->height, win->id);
}

static void handle_destroy_window(fwm_client_t* client, const fwm_msg_window_op_t* msg) {
    fwm_window_t* win = find_window(msg->window_id);
    if (!win || win->client_id != client->id) return;
    
    mark_window_area(win);

    //detach and remove shared memory
    if (win->buffer) {
        shmdt(win->buffer);
    }
    shmctl(win->shm_id, IPC_RMID, NULL);

    //clear fields so stale pointers are not observed during removal
    win->buffer = NULL;
    win->width = 0;
    win->height = 0;
    win->visible = 0;
    win->dirty = 0;
    
    if (g_server.focused_window == win) {
        g_server.focused_window = NULL;
    }
    
    //remove from array
    int idx = win - g_server.windows;
    int remaining = g_server.num_windows - idx - 1;
    if (remaining > 0) {
        memmove(&g_server.windows[idx], &g_server.windows[idx + 1],
                (size_t)remaining * sizeof(fwm_window_t));
    }
    g_server.num_windows--;

    //clear the now-unused tail slot to avoid dangling pointers
    memset(&g_server.windows[g_server.num_windows], 0, sizeof(fwm_window_t));
    
    printf("FrostyWM: Window destroyed: id=%u\n", msg->window_id);
}

static void handle_show_window(fwm_client_t* client, const fwm_msg_window_op_t* msg) {
    fwm_window_t* win = find_window(msg->window_id);
    if (!win || win->client_id != client->id) return;
    
    mark_window_area(win);
    win->visible = 1;
    win->dirty = 1;
}

static void handle_hide_window(fwm_client_t* client, const fwm_msg_window_op_t* msg) {
    fwm_window_t* win = find_window(msg->window_id);
    if (!win || win->client_id != client->id) return;
    
    mark_window_area(win);
    win->visible = 0;
    win->dirty = 1;
}

static void handle_move_window(fwm_client_t* client, const fwm_msg_move_window_t* msg) {
    fwm_window_t* win = find_window(msg->window_id);
    if (!win || win->client_id != client->id) return;
    
    mark_window_area(win);
    win->x = msg->x;
    win->y = msg->y;
    mark_window_area(win);
    win->dirty = 1;
}

static void handle_resize_window(fwm_client_t* client, const fwm_msg_resize_window_t* msg) {
    fwm_window_t* win = find_window(msg->window_id);
    if (!win || win->client_id != client->id) return;
    
    //Restrict resize to safe sizes; no SHM reallocation yet
    uint32_t req_w = msg->width;
    uint32_t req_h = msg->height;
    if (req_w == 0 || req_h == 0 || req_w > MAX_WINDOW_DIM || req_h > MAX_WINDOW_DIM) {
        WM_DEBUG_LOG("FrostyWM: reject invalid resize w=%u h=%u\n", req_w, req_h);
        return;
    }
    // Do not grow beyond current buffer (no reallocation supported)
    uint32_t new_w = (req_w <= win->width) ? req_w : win->width;
    uint32_t new_h = (req_h <= win->height) ? req_h : win->height;

    mark_window_area(win);
    win->width = new_w;
    win->height = new_h;
    mark_window_area(win);
    win->dirty = 1;
}

static void handle_set_title(fwm_client_t* client, const fwm_msg_set_title_t* msg) {
    fwm_window_t* win = find_window(msg->window_id);
    if (!win || win->client_id != client->id) return;
    
    strncpy(win->title, msg->title, sizeof(win->title) - 1);
    win->title[sizeof(win->title) - 1] = '\0';
    mark_window_area(win);
    win->dirty = 1;
}

static void handle_damage(fwm_client_t* client, const fwm_msg_damage_t* msg) {
    fwm_window_t* win = find_window(msg->window_id);
    if (!win || win->client_id != client->id) return;
    
    mark_window_subrect(win, msg->x, msg->y, msg->width, msg->height);
    win->dirty = 1;
}

static void handle_commit(fwm_client_t* client, const fwm_msg_window_op_t* msg) {
    fwm_window_t* win = find_window(msg->window_id);
    if (!win || win->client_id != client->id) return;

    mark_window_area(win);
    win->dirty = 1;
    mark_window_subrect(win, 0, 0, win->width, win->height);
}

static void handle_client_message(fwm_client_t* client) {
    fwm_msg_header_t header;
    size_t hdr_got = 0;
    while (hdr_got < sizeof(header)) {
        ssize_t n = read(client->fd, ((char*)&header) + hdr_got, sizeof(header) - hdr_got);
        if (n == 0) {
            client->active = 0;
            close(client->fd);
            client->fd = -1;
            printf("FrostyWM: Client disconnected cleanly: %s\n", client->app_name);
            return;
        }
        if (n < 0) {
            if (errno == EAGAIN) { usleep(1000); continue; }
            client->active = 0;
            close(client->fd);
            client->fd = -1;
            printf("FrostyWM: Client read error: %s\n", client->app_name);
            return;
        }
        hdr_got += (size_t)n;
        wm_jitter(WM_JITTER_MIN_USEC, WM_JITTER_MAX_USEC);
    }

    wm_jitter(WM_JITTER_MIN_USEC, WM_JITTER_MAX_USEC);

    if (header.length < sizeof(fwm_msg_header_t) || header.length > sizeof((char[4096]){0})) {
        //invalid total length
        client->active = 0;
        close(client->fd);
        client->fd = -1;
        printf("FrostyWM: Invalid message length from client %s: %u\n", client->app_name, header.length);
        return;
    }

    //read rest of message robustly (heap buffer to reduce stack usage)
    uint8_t* msg_buf = (uint8_t*)malloc(header.length);
    if (!msg_buf) {
        client->active = 0;
        close(client->fd);
        client->fd = -1;
        return;
    }
    memset(msg_buf, 0, header.length);
    memcpy(msg_buf, &header, sizeof(header));
    size_t remaining = header.length - sizeof(header);
    size_t off = 0;
    while (off < remaining) {
        ssize_t n = read(client->fd, (char*)msg_buf + sizeof(header) + off, remaining - off);
        if (n == 0) {
            client->active = 0;
            close(client->fd);
            client->fd = -1;
            free(msg_buf);
            return;
        }
        if (n < 0) {
            if (errno == EAGAIN) { usleep(1000); continue; }
            client->active = 0;
            close(client->fd);
            client->fd = -1;
            free(msg_buf);
            return;
        }
        off += (size_t)n;
        wm_jitter(WM_JITTER_MIN_USEC, WM_JITTER_MAX_USEC);
    }

    //dispatch with strict length validation per type
    wm_jitter(WM_JITTER_MIN_USEC, WM_JITTER_MAX_USEC);
    switch (header.type) {
        case FWM_MSG_CONNECT:
            if (header.length < sizeof(fwm_msg_connect_t)) goto bad_msg;
            handle_connect(client, (fwm_msg_connect_t*)msg_buf);
            break;
        case FWM_MSG_CREATE_WINDOW:
            if (header.length < sizeof(fwm_msg_create_window_t)) goto bad_msg;
            handle_create_window(client, (fwm_msg_create_window_t*)msg_buf);
            break;
        case FWM_MSG_DESTROY_WINDOW:
            if (header.length < sizeof(fwm_msg_window_op_t)) goto bad_msg;
            handle_destroy_window(client, (fwm_msg_window_op_t*)msg_buf);
            break;
        case FWM_MSG_SHOW_WINDOW:
            if (header.length < sizeof(fwm_msg_window_op_t)) goto bad_msg;
            handle_show_window(client, (fwm_msg_window_op_t*)msg_buf);
            break;
        case FWM_MSG_HIDE_WINDOW:
            if (header.length < sizeof(fwm_msg_window_op_t)) goto bad_msg;
            handle_hide_window(client, (fwm_msg_window_op_t*)msg_buf);
            break;
        case FWM_MSG_MOVE_WINDOW:
            if (header.length < sizeof(fwm_msg_move_window_t)) goto bad_msg;
            handle_move_window(client, (fwm_msg_move_window_t*)msg_buf);
            break;
        case FWM_MSG_RESIZE_WINDOW:
            if (header.length < sizeof(fwm_msg_resize_window_t)) goto bad_msg;
            handle_resize_window(client, (fwm_msg_resize_window_t*)msg_buf);
            break;
        case FWM_MSG_SET_TITLE:
            if (header.length < sizeof(fwm_msg_set_title_t)) goto bad_msg;
            handle_set_title(client, (fwm_msg_set_title_t*)msg_buf);
            break;
        case FWM_MSG_DAMAGE:
            if (header.length < sizeof(fwm_msg_damage_t)) goto bad_msg;
            handle_damage(client, (fwm_msg_damage_t*)msg_buf);
            break;
        case FWM_MSG_COMMIT:
            if (header.length < sizeof(fwm_msg_window_op_t)) goto bad_msg;
            handle_commit(client, (fwm_msg_window_op_t*)msg_buf);
            break;
        case FWM_MSG_POLL_EVENT: {
            if (header.length < sizeof(fwm_msg_header_t)) goto bad_msg;
            fwm_msg_header_t reply;
            reply.type = FWM_REPLY_NO_EVENT;
            reply.length = sizeof(reply);
            reply.seq = header.seq;
            send_message(client->fd, &reply, sizeof(reply));
            break;
        }
        case FWM_MSG_DISCONNECT:
            client->active = 0;
            close(client->fd);
            client->fd = -1;
            break;
        default:
            goto bad_msg;
    }
    free(msg_buf);
    return;

bad_msg:
    client->active = 0;
    close(client->fd);
    client->fd = -1;
    free(msg_buf);
    printf("FrostyWM: Invalid or malformed message from client %s (type=%u len=%u)\n",
           client->app_name, header.type, header.length);
}

static void accept_new_client() {
    if (g_server.num_clients >= MAX_CLIENTS) return;
    
    //try to accept a client with retries for blocking sockets
    int client_fd = -1;
    int retries = 10; //try up to 10 times with small delays
    
    while (retries > 0) {
        client_fd = accept(g_server.listen_fd, NULL, NULL);
        if (client_fd >= 0) {
            break; //success
        }
        
        if (errno == EAGAIN) {
            //would block - wait a bit and retry
            usleep(10000); //10ms
            retries--;
            continue;
        }
        
        //other error - give up
        return;
    }
    
    if (client_fd < 0) {
        return; //failed to accept after retries
    }

    fwm_client_t* client = &g_server.clients[g_server.num_clients++];
    wm_jitter(WM_JITTER_MIN_USEC, WM_JITTER_MAX_USEC);
    client->id = g_server.next_client_id++;
    client->fd = client_fd;
    //set non-blocking mode for robustness with select-driven loop
    int fl = fcntl(client->fd, F_GETFL, 0);
    if (fl >= 0) {
        fcntl(client->fd, F_SETFL, fl | O_NONBLOCK);
    }
    client->active = 1;
    strcpy(client->app_name, "Unknown");
}

static void composite_windows() {
    int windows_dirty = g_server.first_frame;
    for (int i = 0; i < g_server.num_windows; i++) {
        if (g_server.windows[i].dirty) {
            windows_dirty = 1;
            break;
        }
    }

    if (!windows_dirty && !g_server.dirty_rect.valid) {
        return;
    }

    if (windows_dirty) {
        if (!g_server.dirty_rect.valid) {
            mark_entire_screen();
        }

        if (!g_server.backbuffer) {
            WM_DEBUG_LOG("FrostyWM: composite_windows abort - backbuffer=NULL\n");
            //verify backbuffer canary before pushing to fb
        if (!backbuffer_canary_ok()) {
            log_serial("FrostyWM: backbuffer canary corrupted before blit\n");
            backbuffer_canary_set();
        }
        g_server.dirty_rect.valid = 0;
            return;
        }

        int32_t region_x1 = g_server.dirty_rect.x1;
        int32_t region_y1 = g_server.dirty_rect.y1;
        int32_t region_x2 = g_server.dirty_rect.x2;
        int32_t region_y2 = g_server.dirty_rect.y2;

        if (region_x1 < 0) region_x1 = 0;
        if (region_y1 < 0) region_y1 = 0;
        if (region_x2 > (int32_t)g_server.screen_width) region_x2 = (int32_t)g_server.screen_width;
        if (region_y2 > (int32_t)g_server.screen_height) region_y2 = (int32_t)g_server.screen_height;

        int32_t width = region_x2 - region_x1;
        int32_t height = region_y2 - region_y1;
        if (width <= 0 || height <= 0) {
            g_server.dirty_rect.valid = 0;
            return;
        }

        for (int32_t y = region_y1; y < region_y2; y++) {
            uint8_t* row = g_server.backbuffer + (size_t)y * g_server.fb_pitch_bytes;
            for (int32_t x = region_x1; x < region_x2; x++) {
                write_pixel(row + (size_t)x * g_server.fb_bytes_per_pixel, 0xFF303030);
            }
        }

        for (int i = 0; i < g_server.num_windows; i++) {
            fwm_window_t* win = &g_server.windows[i];
            if (!win->visible) continue;
            WM_DEBUG_LOG("FrostyWM: composite win=%u visible=%d buffer=%p dirty=%d pos=(%d,%d) size=%ux%u\n",
                         win->id, win->visible, win->buffer, win->dirty, win->x, win->y,
                         win->width, win->height);
            if (!win->buffer) {
                if (win->dirty) {
                    printf("FrostyWM: warning - window %u has no buffer; marking clean\n", win->id);
                    win->dirty = 0;
                }
                continue;
            }

            uint32_t* src = (uint32_t*)win->buffer;
            int32_t win_x1 = win->x;
            int32_t win_y1 = win->y;
            int32_t win_x2 = win->x + (int32_t)win->width;
            int32_t win_y2 = win->y + (int32_t)win->height;

            if (win_x1 >= region_x2 || win_x2 <= region_x1 ||
                win_y1 >= region_y2 || win_y2 <= region_y1) {
                continue;
            }

            int32_t copy_x1 = win_x1 < region_x1 ? region_x1 : win_x1;
            int32_t copy_y1 = win_y1 < region_y1 ? region_y1 : win_y1;
            int32_t copy_x2 = win_x2 > region_x2 ? region_x2 : win_x2;
            int32_t copy_y2 = win_y2 > region_y2 ? region_y2 : win_y2;

            for (int32_t y = copy_y1; y < copy_y2; y++) {
                int32_t src_y = y - win_y1;
                if (src_y < 0 || src_y >= (int32_t)win->height) continue;

                uint8_t* dst_row = g_server.backbuffer + (size_t)y * g_server.fb_pitch_bytes;
                for (int32_t x = copy_x1; x < copy_x2; x++) {
                    int32_t src_x = x - win_x1;
                    if (src_x < 0 || src_x >= (int32_t)win->width) continue;
                    uint8_t* dst = dst_row + (size_t)x * g_server.fb_bytes_per_pixel;
                    write_pixel(dst, src[src_y * win->width + src_x]);
                }
            }

            win->dirty = 0;
        }

        g_server.dirty_rect.valid = 0;
        g_server.first_frame = 0;

        WM_DEBUG_LOG("FrostyWM: saving cursor underlay at (%d,%d)\n", g_server.mouse_x, g_server.mouse_y);
        save_cursor_underlay(g_server.mouse_x, g_server.mouse_y);
        WM_DEBUG_LOG("FrostyWM: drawing cursor sprite\n");
        draw_cursor_sprite();

        if (g_server.fb_fd > 0) {
            fb_blit_args_t blit = {
                .x = (uint32_t)region_x1,
                .y = (uint32_t)region_y1,
                .w = (uint32_t)width,
                .h = (uint32_t)height,
                .src_pitch = g_server.fb_pitch_bytes,
                .flags = 0,
                .src = g_server.backbuffer + (size_t)region_y1 * g_server.fb_pitch_bytes + (size_t)region_x1 * g_server.fb_bytes_per_pixel
            };
            if (ioctl(g_server.fb_fd, FB_IOCTL_BLIT, &blit) != 0) {
                write(g_server.fb_fd, g_server.backbuffer, g_server.framebuffer_size);
            }
        }
    } else {
        int32_t old_x = g_server.cursor_backup_valid ? g_server.cursor_backup_x : g_server.mouse_x;
        int32_t old_y = g_server.cursor_backup_valid ? g_server.cursor_backup_y : g_server.mouse_y;

        WM_DEBUG_LOG("FrostyWM: fast cursor path restore (%d,%d) -> (%d,%d)\n",
                     old_x, old_y, g_server.mouse_x, g_server.mouse_y);
        restore_cursor_underlay();
        save_cursor_underlay(g_server.mouse_x, g_server.mouse_y);
        draw_cursor_sprite();
        if (!backbuffer_canary_ok()) {
            log_serial("FrostyWM: backbuffer canary corrupted in fast cursor path\n");
            backbuffer_canary_set();
        }

        int32_t new_x = g_server.mouse_x;
        int32_t new_y = g_server.mouse_y;

        int32_t blit_x1 = old_x < new_x ? old_x : new_x;
        int32_t blit_y1 = old_y < new_y ? old_y : new_y;
        int32_t blit_x2 = (old_x + CURSOR_WIDTH) > (new_x + CURSOR_WIDTH) ? (old_x + CURSOR_WIDTH) : (new_x + CURSOR_WIDTH);
        int32_t blit_y2 = (old_y + CURSOR_HEIGHT) > (new_y + CURSOR_HEIGHT) ? (old_y + CURSOR_HEIGHT) : (new_y + CURSOR_HEIGHT);

        if (blit_x1 < 0) blit_x1 = 0;
        if (blit_y1 < 0) blit_y1 = 0;
        if (blit_x2 > (int32_t)g_server.screen_width) blit_x2 = (int32_t)g_server.screen_width;
        if (blit_y2 > (int32_t)g_server.screen_height) blit_y2 = (int32_t)g_server.screen_height;

        int32_t width = blit_x2 - blit_x1;
        int32_t height = blit_y2 - blit_y1;
        if (width <= 0 || height <= 0) {
            return;
        }

        if (g_server.fb_fd > 0) {
            fb_blit_args_t blit = {
                .x = (uint32_t)blit_x1,
                .y = (uint32_t)blit_y1,
                .w = (uint32_t)width,
                .h = (uint32_t)height,
                .src_pitch = g_server.fb_pitch_bytes,
                .flags = 0,
                .src = g_server.backbuffer + (size_t)blit_y1 * g_server.fb_pitch_bytes + (size_t)blit_x1 * g_server.fb_bytes_per_pixel
            };
            if (ioctl(g_server.fb_fd, FB_IOCTL_BLIT, &blit) != 0) {
                write(g_server.fb_fd, g_server.backbuffer, g_server.framebuffer_size);
            }
        }
    }
}

static void process_mouse_events() {
    if (g_server.mouse_fd < 0) return;

    fwm_mouse_event_t event;
    while (1) {
        ssize_t n = read(g_server.mouse_fd, &event, sizeof(event));
        if (n != sizeof(event)) {
            break;
        }

        WM_DEBUG_LOG("FrostyWM: mouse event type=%u button=%u rel=(%d,%d) pos=(%d,%d)\n",
                     event.type, event.button, event.rel_x, event.rel_y,
                     g_server.mouse_x, g_server.mouse_y);

        if (event.type == 2) { //motion
            int32_t old_x = g_server.mouse_x;
            int32_t old_y = g_server.mouse_y;
            g_server.mouse_x += event.rel_x;
            g_server.mouse_y -= event.rel_y; //invert Y

            if (g_server.mouse_x < 0) g_server.mouse_x = 0;
            if (g_server.mouse_x >= (int32_t)g_server.screen_width)
                g_server.mouse_x = g_server.screen_width - 1;
            if (g_server.mouse_y < 0) g_server.mouse_y = 0;
            if (g_server.mouse_y >= (int32_t)g_server.screen_height)
                g_server.mouse_y = g_server.screen_height - 1;

            mark_cursor_dirty_area(old_x, old_y);
            mark_cursor_dirty_area(g_server.mouse_x, g_server.mouse_y);
        } else if (event.type == 1) { //button press
            g_server.mouse_buttons |= event.button;
            WM_DEBUG_LOG("FrostyWM: button press -> buttons=%u\n", g_server.mouse_buttons);
        } else if (event.type == 0) { //button release
            g_server.mouse_buttons &= ~event.button;
            WM_DEBUG_LOG("FrostyWM: button release -> buttons=%u\n", g_server.mouse_buttons);
        }

        if (g_server.mouse_x < 0 || g_server.mouse_y < 0 ||
            g_server.mouse_x >= (int32_t)g_server.screen_width ||
            g_server.mouse_y >= (int32_t)g_server.screen_height) {
            log_serial("FrostyWM: cursor position out of bounds (%d,%d)\n",
                       g_server.mouse_x, g_server.mouse_y);
        }
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    printf("FrostyWM: Starting display server\n");
    
    memset(&g_server, 0, sizeof(g_server));
    
    //initialize all client FDs to -1
    for (int i = 0; i < MAX_CLIENTS; i++) {
        g_server.clients[i].fd = -1;
        g_server.clients[i].active = 0;
    }
    
    g_server.next_client_id = 1;
    g_server.next_window_id = 1;
    g_server.next_shm_key = 1000;
    
    init_framebuffer();
    init_mouse();
    
    g_server.listen_fd = create_listen_socket();
    if (g_server.listen_fd < 0) {
        fprintf(stderr, "Failed to create listen socket\n");
        return 1;
    }
    
    printf("FrostyWM: Listening on %s\n", FWM_SOCKET_PATH);
    
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        
        int max_fd = g_server.listen_fd;
        FD_SET(g_server.listen_fd, &read_fds);
        
        //add all active client FDs to the set
        for (int i = 0; i < g_server.num_clients; i++) {
            if (g_server.clients[i].active && g_server.clients[i].fd >= 0) {
                FD_SET(g_server.clients[i].fd, &read_fds);
                if (g_server.clients[i].fd > max_fd) {
                    max_fd = g_server.clients[i].fd;
                }
            }
        }
        
        //add mouse FD if available
        if (g_server.mouse_fd >= 0) {
            FD_SET(g_server.mouse_fd, &read_fds);
            if (g_server.mouse_fd > max_fd) {
                max_fd = g_server.mouse_fd;
            }
        }
        
        struct timeval timeout;
        timeout.tv_sec = 0;
        if (g_server.dirty_rect.valid) {
            timeout.tv_usec = 0;
        } else {
            timeout.tv_usec = 4000;  //~250 FPS fallback when idle
        }

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (ready < 0) {
            //select error
            continue;
        }        
        if (ready > 0) {
            //check for new connections
            if (FD_ISSET(g_server.listen_fd, &read_fds)) {
                accept_new_client();
            }
            
            //process client messages for ready FDs only
            for (int i = 0; i < g_server.num_clients; i++) {
                if (g_server.clients[i].active && 
                    g_server.clients[i].fd >= 0 &&
                    FD_ISSET(g_server.clients[i].fd, &read_fds)) {
                    handle_client_message(&g_server.clients[i]);
                }
            }
            
            //process mouse events if ready
            if (g_server.mouse_fd >= 0 && FD_ISSET(g_server.mouse_fd, &read_fds)) {
                process_mouse_events();
            }
        }
        
        //composite and render
        composite_windows();
    }
    
    return 0;
}
