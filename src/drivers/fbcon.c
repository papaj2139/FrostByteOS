#include "fbcon.h"
#include "fb.h"
#include "../font.h"
#include "../mm/heap.h"
#include "../drivers/timer.h"
#include "../fs/vfs.h"
#include <string.h>

static uint8_t* fb = 0;
static uint32_t fb_w=0, fb_h=0, fb_bpp=0, fb_pitch=0;
static int ready = 0;
static int cur_x = 0, cur_y = 0; //character coords
static int ch_w = 8;
static int ch_h = 16; //default 8x16

//PSF font data
static uint8_t* psf_glyphs = 0;
static int psf_w = 8;
static int psf_h = 0; //0 - unused
static int psf_stride = 1; //bytes per row of glyph

//cursor state
static volatile int cursor_enabled = 1;
static volatile int cursor_visible = 0;
static int cursor_px = 0, cursor_py = 0; //pixel pos of current cell
static uint32_t blink_div = 0; //ticks threshold
//ANSI escape sequence parser for fbcon
typedef enum {
    FBCON_ANSI_NORMAL,
    FBCON_ANSI_ESC,
    FBCON_ANSI_CSI,
    FBCON_ANSI_CSI_PARAM
} fbcon_ansi_state_t;

static fbcon_ansi_state_t ansi_state = FBCON_ANSI_NORMAL;
static int ansi_params[8];
static int ansi_param_count = 0;
static unsigned char current_attr = 0x0F;

static inline uint32_t rgb_from_attr(unsigned char attr) {
    //VGA attr: low nibble = FG high nibble = BG (blink ignored)
    //map 0..15 CGA palette to RGB approximate
    static const uint32_t pal[16] = {
        0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
        0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF
    };
    uint32_t fg = pal[attr & 0x0F];
    return fg;
}

static void draw_glyph(int px, int py, char ch, unsigned char attr) {
    if ((unsigned char)ch >= 128) ch = '?';
    uint32_t fg = rgb_from_attr(attr);
    uint32_t bg = 0x000000;
    int max_x = (int)fb_w - 1;
    int max_y = (int)fb_h - 1;

    if (psf_h > 0) {
        //draw from psf_glyphs
        int index = (int)(unsigned char)ch;
        const uint8_t* glyph = psf_glyphs + index * (psf_h * psf_stride);
        for (int row = 0; row < psf_h; row++) {
            const uint8_t* gr = glyph + row * psf_stride;
            int y = py + row;
            if (y > max_y) continue;
            uint8_t* dst = fb + (uint32_t)y * fb_pitch + (uint32_t)px * (fb_bpp/8);
            for (int col = 0; col < psf_w; col++) {
                //MSB-first bit order within each byte
                int byte_index = col / 8;
                int bit_index = 7 - (col % 8);
                uint8_t bits = gr[byte_index];
                uint32_t color = (bits & (1 << bit_index)) ? fg : bg;
                int x = px + col;
                if (x > max_x) break;
                if (fb_bpp == 32) {
                    ((uint32_t*)dst)[col] = color;
                } else if (fb_bpp == 24) {
                    uint8_t* p = dst + col*3;
                    p[0] = (uint8_t)(color & 0xFF);
                    p[1] = (uint8_t)((color >> 8) & 0xFF);
                    p[2] = (uint8_t)((color >> 16) & 0xFF);
                } else if (fb_bpp == 16) {
                    uint8_t r = (color >> 16) & 0xFF;
                    uint8_t g = (color >> 8) & 0xFF;
                    uint8_t b = color & 0xFF;
                    uint16_t v = (uint16_t)(((r>>3)<<11) | ((g>>2)<<5) | (b>>3));
                    ((uint16_t*)dst)[col] = v;
                }
            }
        }
    } else {
        //fallback: 8x16 derived from 8x8
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font8x8[(int)(unsigned char)ch][row];
            for (int rep = 0; rep < 2; rep++) {
                int y = py + row*2 + rep;
                if (y > max_y) continue;
                uint8_t* dst = fb + (uint32_t)y * fb_pitch + (uint32_t)px * (fb_bpp/8);
                for (int col = 0; col < 8; col++) {
                    uint32_t color = (bits & (1 << col)) ? fg : bg;
                    int x = px + col;
                    if (x > max_x) break;
                    if (fb_bpp == 32) {
                        ((uint32_t*)dst)[col] = color;
                    } else if (fb_bpp == 24) {
                        uint8_t* p = dst + col*3;
                        p[0] = (uint8_t)(color & 0xFF);
                        p[1] = (uint8_t)((color >> 8) & 0xFF);
                        p[2] = (uint8_t)((color >> 16) & 0xFF);
                    } else if (fb_bpp == 16) {
                        uint8_t r = (color >> 16) & 0xFF;
                        uint8_t g = (color >> 8) & 0xFF;
                        uint8_t b = color & 0xFF;
                        uint16_t v = (uint16_t)(((r>>3)<<11) | ((g>>2)<<5) | (b>>3));
                        ((uint16_t*)dst)[col] = v;
                    }
                }
            }
        }
    }
}

static void cursor_invert_underline(int px, int py) {
    if (!cursor_enabled) return;
    int y0 = py + ch_h - 2;
    if (y0 < py) y0 = py;
    int y1 = py + ch_h - 1;
    if (y1 >= (int)fb_h) y1 = (int)fb_h - 1;
    int x0 = px; int x1 = px + ch_w - 1;
    if (x1 >= (int)fb_w) x1 = (int)fb_w - 1;
    for (int y = y0; y <= y1; y++) {
        uint8_t* dst = fb + (uint32_t)y * fb_pitch + (uint32_t)x0 * (fb_bpp/8);
        for (int x = x0; x <= x1; x++) {
            if (fb_bpp == 32) {
                uint32_t* p = (uint32_t*)dst;
                *p ^= 0x00FFFFFFu;
                dst += 4;
            } else if (fb_bpp == 24) {
                dst[0] ^= 0xFF;
                dst[1] ^= 0xFF;
                dst[2] ^= 0xFF;
                dst += 3;
            } else if (fb_bpp == 16) {
                uint16_t* p = (uint16_t*)dst; *p ^= 0xFFFFu; dst += 2;
            }
        }
    }
}

static void fbcon_cursor_tick_irq(void) {
    static uint32_t cnt = 0;
    if (!ready || !cursor_enabled) return;
    if (++cnt >= blink_div) {
        cnt = 0;
        //toggle at current position
        cursor_invert_underline(cursor_px, cursor_py);
        cursor_visible = !cursor_visible;
    }
}

static void fbcon_cursor_erase_if_drawn(void) {
    if (cursor_visible) {
        cursor_invert_underline(cursor_px, cursor_py);
        cursor_visible = 0;
    }
}

static void fbcon_newline(void) {
    fbcon_cursor_erase_if_drawn();
    cur_x = 0;
    cur_y++;
    //scroll if needed
    int rows = (int)(fb_h / ch_h);
    if (cur_y >= rows) {
        //move framebuffer up by one character row (ch_h pixels)
        uint32_t move_bytes = (uint32_t)(fb_h - ch_h) * fb_pitch;
        memmove(fb, fb + ch_h * fb_pitch, move_bytes);
        //clear bottom area
        memset(fb + move_bytes, 0x00, ch_h * fb_pitch);
        cur_y = rows - 1;
    }
}

static void try_load_psf_font(void) {
    vfs_node_t* n = vfs_open("/etc/font.psf", VFS_FLAG_READ);
    if (!n) return;
    int size = vfs_get_size(n);
    if (size <= 0 || size > (1<<20)) { vfs_close(n); return; }
    uint8_t* buf = (uint8_t*)kmalloc((uint32_t)size);
    if (!buf) { vfs_close(n); return; }
    if (vfs_read(n, 0, (uint32_t)size, (char*)buf) != size) {
        vfs_close(n);
        kfree(buf);
        return;
    }
    vfs_close(n);
    //detect PSF2
    if (size >= 32 && buf[0] == 0x72 && buf[1] == 0xB5 && buf[2] == 0x4A && buf[3] == 0x86) {
        uint32_t headersize = *(uint32_t*)(buf + 8);
        uint32_t flags = *(uint32_t*)(buf + 12);
        (void)flags;
        uint32_t glyphs = *(uint32_t*)(buf + 16);
        uint32_t bytesperglyph = *(uint32_t*)(buf + 20);
        uint32_t w = *(uint32_t*)(buf + 24);
        uint32_t h = *(uint32_t*)(buf + 28);
        if (glyphs >= 256 && w >= 8 && w <= 16 && h >= 8 && h <= 32 && bytesperglyph == ((w+7)/8)*h) {
            psf_w = (int)w;
            psf_h = (int)h;
            psf_stride = (int)((w+7)/8);
            psf_glyphs = (uint8_t*)kmalloc(bytesperglyph * 256);
            if (psf_glyphs) memcpy(psf_glyphs, buf + headersize, bytesperglyph * 256);
        }
    } else if (size >= 4 && buf[0] == 0x36 && buf[1] == 0x04) {
        //PSF1
        uint8_t mode = buf[2];
        uint8_t charsize = buf[3];
        int glyphs = (mode & 0x01) ? 512 : 256;
        int bytesperglyph = charsize;
        psf_w = 8; psf_h = charsize; psf_stride = 1;
        if (glyphs >= 256) {
            psf_glyphs = (uint8_t*)kmalloc(bytesperglyph * 256);
            if (psf_glyphs) memcpy(psf_glyphs, buf + 4, bytesperglyph * 256);
        }
    }
    kfree(buf);
    if (psf_glyphs) {
        ch_w = psf_w;
        ch_h = psf_h;
    }
}

int fbcon_reload_font(void) {
    if (!ready) return -1;
    if (psf_glyphs) { kfree(psf_glyphs); psf_glyphs = 0; }
    //reset defaults
    psf_w = 8; psf_h = 0; psf_stride = 1;
    ch_w = 8; ch_h = 16;
    try_load_psf_font();
    return psf_glyphs ? 0 : -1;
}

int fbcon_init(void) {
    if (fb_get_info(&fb, &fb_w, &fb_h, &fb_bpp, &fb_pitch) != 0) {
        ready = 0; return -1;
    }
    //try to load a PSF font if provided in initramfs
    try_load_psf_font();
    ready = 1; cur_x = cur_y = 0;
    //setup cursor blink rate ~2Hz
    uint32_t hz = timer_get_frequency();
    blink_div = (hz >= 2) ? (hz / 2) : 50;
    cursor_px = 0; cursor_py = 0; cursor_visible = 0;
    timer_register_callback(fbcon_cursor_tick_irq);
    return 0;
}

int fbcon_available(void) {
    return ready;
}

int fbcon_set_cursor_enabled(int enable) {
    if (!ready) return -1;
    if (enable) {
        cursor_enabled = 1;
        //ensure not visible until next blink tick draws it
        cursor_visible = 0;
        return 0;
    } else {
        //erase if currently visible, then disable
        fbcon_cursor_erase_if_drawn();
        cursor_enabled = 0;
        return 0;
    }
}

void fbcon_clear_with_attr(unsigned char attr) {
    (void)attr; //currently ignored bg
    if (!ready) return;
    fbcon_cursor_erase_if_drawn();
    memset(fb, 0x00, fb_pitch * fb_h);
    cur_x = cur_y = 0;
    cursor_px = 0;
    cursor_py = 0;
}

int fbcon_putchar(char c, unsigned char attr) {
    if (!ready) return 0;
    //erase old cursor if visible
    fbcon_cursor_erase_if_drawn();
    if (c == '\n') {
        fbcon_newline();
        cursor_px = cur_x * ch_w;
        cursor_py = cur_y * ch_h;
        return 1;
    }
    if (c == '\b') {
        if (cur_x > 0) cur_x--; else if (cur_y > 0) {
            cur_y--;
            cur_x = (int)(fb_w / ch_w) - 1;
        }
        //draw space over previous char
        int px = cur_x * ch_w;
        int py = cur_y * ch_h;
        //clear cell
        for (int y = 0; y < ch_h; y++) memset(fb + (py + y) * fb_pitch + px * (fb_bpp/8), 0x00, ch_w * (fb_bpp/8));
        cursor_px = cur_x * ch_w;
        cursor_py = cur_y * ch_h;
        return 1;
    }
    int cols = (int)(fb_w / ch_w);
    int px = cur_x * ch_w;
    int py = cur_y * ch_h;
    draw_glyph(px, py, c, attr);
    cur_x++;
    if (cur_x >= cols) fbcon_newline();
    cursor_px = cur_x * ch_w;
    cursor_py = cur_y * ch_h;
    return 1;
}

void fbcon_get_cursor(int* x, int* y) {
    if (x) *x = cur_x;
    if (y) *y = cur_y;
}

void fbcon_set_cursor(int x, int y) {
    if (!ready) return;
    fbcon_cursor_erase_if_drawn();
    int cols = (int)(fb_w / ch_w);
    int rows = (int)(fb_h / ch_h);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= cols) x = cols - 1;
    if (y >= rows) y = rows - 1;
    cur_x = x;
    cur_y = y;
    cursor_px = cur_x * ch_w;
    cursor_py = cur_y * ch_h;
}

static unsigned char ansi_to_vga_attr(int ansi_code) {
    if (ansi_code == 0) return 0x0F; //reset
    if (ansi_code == 7) return 0x70; //inverse
    if (ansi_code == 30) return (current_attr & 0xF0) | 0x00; //black fg
    if (ansi_code == 31) return (current_attr & 0xF0) | 0x04; //red fg
    if (ansi_code == 32) return (current_attr & 0xF0) | 0x02; //green fg
    if (ansi_code == 33) return (current_attr & 0xF0) | 0x06; //yellow fg
    if (ansi_code == 34) return (current_attr & 0xF0) | 0x01; //blue fg
    if (ansi_code == 35) return (current_attr & 0xF0) | 0x05; //magenta fg
    if (ansi_code == 36) return (current_attr & 0xF0) | 0x03; //cyan fg
    if (ansi_code == 37) return (current_attr & 0xF0) | 0x07; //white fg
    if (ansi_code == 90) return (current_attr & 0xF0) | 0x08; //gray fg
    if (ansi_code == 91) return (current_attr & 0xF0) | 0x0C; //bright red fg
    if (ansi_code == 92) return (current_attr & 0xF0) | 0x0A; //bright green fg
    if (ansi_code == 93) return (current_attr & 0xF0) | 0x0E; //bright yellow fg
    if (ansi_code == 94) return (current_attr & 0xF0) | 0x09; //bright blue fg
    if (ansi_code == 95) return (current_attr & 0xF0) | 0x0D; //bright magenta fg
    if (ansi_code == 96) return (current_attr & 0xF0) | 0x0B; //bright cyan fg
    if (ansi_code == 97) return (current_attr & 0xF0) | 0x0F; //bright white fg
    return current_attr;
}

static void fbcon_process_csi(char final_char) {
    if (!ready) return;

    if (ansi_param_count == 0) {
        ansi_params[0] = 0;
        ansi_param_count = 1;
    }

    int cols = (int)(fb_w / ch_w);
    int rows = (int)(fb_h / ch_h);

    //cursor movement: ESC[<row>;<col>H or ESC[<row>;<col>f
    if (final_char == 'H' || final_char == 'f') {
        if (ansi_param_count >= 2) {
            int row = ansi_params[0] - 1; //ANSI is 1-indexed
            int col = ansi_params[1] - 1;
            fbcon_set_cursor(col, row);
        }
        return;
    }

    if (final_char == 'J') {
        //clear screen
        if (ansi_params[0] == 2) {
            fbcon_clear_with_attr(current_attr);
        }
        return;
    }

    if (final_char == 'K') {
        //clear line to end
        fbcon_cursor_erase_if_drawn();
        int px = cur_x * ch_w;
        int py = cur_y * ch_h;
        for (int x = cur_x; x < cols; x++) {
            int xp = x * ch_w;
            for (int y = 0; y < ch_h; y++) {
                memset(fb + (py + y) * fb_pitch + xp * (fb_bpp/8), 0x00, ch_w * (fb_bpp/8));
            }
        }
        cursor_px = cur_x * ch_w;
        cursor_py = cur_y * ch_h;
        return;
    }

    if (final_char == 'm') {
        //SGR - set graphic rendition (colors/attributes)
        for (int i = 0; i < ansi_param_count; i++) {
            current_attr = ansi_to_vga_attr(ansi_params[i]);
        }
        return;
    }
}

int fbcon_write(const char* buf, uint32_t size) {
    if (!ready || !buf || size == 0) return 0;

    for (uint32_t i = 0; i < size; i++) {
        char c = buf[i];

        switch (ansi_state) {
            case FBCON_ANSI_NORMAL:
                if (c == '\033' || c == '\x1B') { //ESC
                    ansi_state = FBCON_ANSI_ESC;
                } else {
                    fbcon_putchar(c, current_attr);
                }
                break;

            case FBCON_ANSI_ESC:
                if (c == '[') {
                    ansi_state = FBCON_ANSI_CSI;
                    ansi_param_count = 0;
                    for (int j = 0; j < 8; j++) ansi_params[j] = 0;
                } else if (c == '?') {
                    //CSI ? sequences - consume
                    ansi_state = FBCON_ANSI_CSI;
                    ansi_param_count = 0;
                } else {
                    //unknown escape - back to normal
                    ansi_state = FBCON_ANSI_NORMAL;
                }
                break;

            case FBCON_ANSI_CSI:
                if (c >= '0' && c <= '9') {
                    if (ansi_param_count == 0) ansi_param_count = 1;
                    ansi_params[ansi_param_count - 1] = ansi_params[ansi_param_count - 1] * 10 + (c - '0');
                    ansi_state = FBCON_ANSI_CSI_PARAM;
                } else if (c == ';') {
                    if (ansi_param_count < 8) ansi_param_count++;
                } else if (c == 'H' || c == 'f' || c == 'J' || c == 'K' || c == 'm' ||
                           c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'h' || c == 'l') {
                    fbcon_process_csi(c);
                    ansi_state = FBCON_ANSI_NORMAL;
                } else {
                    //unknown - back to normal
                    ansi_state = FBCON_ANSI_NORMAL;
                }
                break;

            case FBCON_ANSI_CSI_PARAM:
                if (c >= '0' && c <= '9') {
                    ansi_params[ansi_param_count - 1] = ansi_params[ansi_param_count - 1] * 10 + (c - '0');
                } else if (c == ';') {
                    if (ansi_param_count < 8) ansi_param_count++;
                    ansi_state = FBCON_ANSI_CSI;
                } else if (c == 'H' || c == 'f' || c == 'J' || c == 'K' || c == 'm' ||
                           c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'h' || c == 'l') {
                    fbcon_process_csi(c);
                    ansi_state = FBCON_ANSI_NORMAL;
                } else {
                    //unknown - back to normal
                    ansi_state = FBCON_ANSI_NORMAL;
                }
                break;
        }
    }

    return (int)size;
}
