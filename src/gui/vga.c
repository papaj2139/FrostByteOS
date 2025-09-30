#include <stdint.h>
#include "vga.h"
#include "../font.h"
#include "../io.h"

uint8_t* const VGA = (uint8_t*)VGA_ADDRESS;
static uint8_t* g_draw_surface = (uint8_t*)VGA_ADDRESS; //defaults to vram
static int g_vsync_enabled = 1; //runtime toggle
static vga_mode_t g_mode = VGA_MODE_13H;
static int g_w = VGA_WIDTH;
static int g_h = VGA_HEIGHT;
static int g_programmed = 0; //whether hardware has been programmed at least once

//TODO: for some reason swtiching to shell from desktop 12h on real hardware you cant type in the shell


void vga_set_mode(vga_mode_t mode) {
    //always program hardware on first call after that skip if already in the same mode
    if (g_programmed && mode == g_mode) return;
    switch (mode) {
        case VGA_MODE_13H:
            vga_set_mode_13h();
            break;
        case VGA_MODE_12H:
            vga_set_mode_12h();
            break;
        case VGA_MODE_TEXT:
            vga_set_text_mode();
            break;
    }
    g_programmed = 1;
}

void vga_present_rect(int x, int y, int w, int h, const uint8_t* surface){
    if (!surface) surface = g_draw_surface;
    if (surface == VGA) return;
    if (g_mode == VGA_MODE_TEXT) return; //no graphics upload in text mode

    //clip to bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= g_w || y >= g_h || w <= 0 || h <= 0) return;
    if (x + w > g_w) w = g_w - x;
    if (y + h > g_h) h = g_h - y;

    if (g_vsync_enabled) vga_wait_vsync();

    if (g_mode == VGA_MODE_13H) {
        for (int j = 0; j < h; ++j) {
            const uint8_t* src = surface + (unsigned)(y + j) * (unsigned)g_w + x;
            uint8_t* dst = VGA    + (unsigned)(y + j) * (unsigned)g_w + x;
            for (int i = 0; i < w; ++i) dst[i] = src[i];
        }
    } else {
        //640x480x16 planar write selected byte range per scanline per plane
        const int bytes_per_scan = g_w / 8; //80
        const int bx_start = x / 8;
        const int bx_end   = (x + w + 7) / 8; //exclusive
        for (int plane = 0; plane < 4; ++plane) {
            outb(0x3C4, 0x02); outb(0x3C5, (uint8_t)(1 << plane));
            for (int j = 0; j < h; ++j) {
                int yy = y + j;
                const uint8_t* src_row = surface + (unsigned)yy * (unsigned)g_w;
                uint8_t* dst_row = VGA + (unsigned)yy * (unsigned)bytes_per_scan;
                for (int bx = bx_start; bx < bx_end; ++bx) {
                    uint8_t packed = 0;
                    for (int bit = 0; bit < 8; ++bit) {
                        int xx = bx * 8 + bit;
                        uint8_t c = src_row[xx] & 0x0F;
                        if (c & (1u << plane)) packed |= (uint8_t)(0x80u >> bit);
                    }
                    dst_row[bx] = packed;
                }
            }
        }
        outb(0x3C4, 0x02); outb(0x3C5, 0x0F);
    }
}

vga_mode_t vga_get_mode(void) { return g_mode; }
int vga_width(void) { return g_w; }
int vga_height(void) { return g_h; }

void putpx(int x, int y, uint8_t color) {
    if ((unsigned)x < (unsigned)g_w && (unsigned)y < (unsigned)g_h)
        g_draw_surface[y * g_w + x] = color;
}

uint8_t getpx(int x, int y) {
    if ((unsigned)x < (unsigned)g_w && (unsigned)y < (unsigned)g_h)
        return g_draw_surface[y * g_w + x];
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
    if (x_end > g_w) x_end = g_w;
    if (y_end > g_h) y_end = g_h;

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

    //update internal mode state
    g_mode = VGA_MODE_13H;
    g_w = 320;
    g_h = 200;
}

void vga_set_mode_12h(void) {
    //miscellaneous register
    outb(0x3C2, 0xE3);

    //sequencer registers
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x03);
    outb(0x3C4, 0x01);
    outb(0x3C5, 0x01);
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x0F);
    outb(0x3C4, 0x03);
    outb(0x3C5, 0x00);
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x06);

    //CRTC for 640x480
    static const uint8_t crtc[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E,
        0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xEA, 0x0C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3,
        0xFF
    };

    //unlock CRTC registers
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & ~0x80);

    //set CRTC registers
    for (uint8_t i = 0; i < sizeof(crtc); i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
    }
    // ensure start address = 0 for text buffer
    outb(0x3D4, 0x0C); outb(0x3D5, 0x00);
    outb(0x3D4, 0x0D); outb(0x3D5, 0x00);
    // ensure start address = 0
    outb(0x3D4, 0x0C); outb(0x3D5, 0x00);
    outb(0x3D4, 0x0D); outb(0x3D5, 0x00);

    //graphics controller registers
    outb(0x3CE, 0x00); outb(0x3CF, 0x00);
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    outb(0x3CE, 0x02); outb(0x3CF, 0x00);
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x00);
    outb(0x3CE, 0x06); outb(0x3CF, 0x05);
    outb(0x3CE, 0x07); outb(0x3CF, 0x0F);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);

    //attribute controller registers
    for (uint8_t i = 0; i < 16; i++) {
        (void)inb(0x3DA);  //reset flip-flop
        outb(0x3C0, i);
        outb(0x3C0, i);    //standard palette mapping
    }

    (void)inb(0x3DA); outb(0x3C0, 0x10); outb(0x3C0, 0x01); //mode control
    (void)inb(0x3DA); outb(0x3C0, 0x11); outb(0x3C0, 0x00); //overscan color
    (void)inb(0x3DA); outb(0x3C0, 0x12); outb(0x3C0, 0x0F); //color plane enable
    (void)inb(0x3DA); outb(0x3C0, 0x13); outb(0x3C0, 0x00); //horizontal pixel panning
    (void)inb(0x3DA); outb(0x3C0, 0x14); outb(0x3C0, 0x00); //color select

    //enable video
    (void)inb(0x3DA);
    outb(0x3C0, 0x20);

    //update internal mode state
    g_mode = VGA_MODE_12H;
    g_w = 640;
    g_h = 480;
}

void vga_set_text_mode(void) {
    //force complete VGA reset disable display and reset all sequencer
    outb(0x3C4, 0x00); outb(0x3C5, 0x01); //synchronous reset
    
    //disable video output completely during transition
    inb(0x3DA);
    outb(0x3C0, 0x00);
    
    //small delay for hardware to settle
    for (volatile int i = 0; i < 1000; i++);

    //miscellaneous register
    outb(0x3C2, 0x67);  //standard text mode timing

    //sequencer registers - complete reset and proper text mode setup
    outb(0x3C4, 0x00); outb(0x3C5, 0x01); //keep in reset during programming
    outb(0x3C4, 0x01); outb(0x3C5, 0x00); //clocking mode 8-dot chars normal timing
    outb(0x3C4, 0x02); outb(0x3C5, 0x03); //map mask enable planes 0 and 1
    outb(0x3C4, 0x03); outb(0x3C5, 0x00); //character map select
    outb(0x3C4, 0x04); outb(0x3C5, 0x02); //memory mode odd/even NO chain-4
    
    //release sequencer reset
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);

    //CRTC registers standard 80x25 text mode values
    static const uint8_t crtc_text[] = {
        //horizontal
        0x5F,  //horizontal total
        0x4F,  //horizontal display end
        0x50,  //start horizontal blanking
        0x82,  //end horizontal blanking
        0x55,  //start horizontal sync
        0x81,  //end horizontal sync
        //vertical
        0xBF,  //vertical total
        0x1F,  //overflow register
        0x00,  //preset row scan
        0x4F,  //maximum scan line (15 = 16-line chars)
        0x0D,  //cursor start
        0x0E,  //cursor end
        //start address
        0x00,  //start address high
        0x00,  //start address low
        0x00,  //cursor location high
        0x00,  //cursor location low
        //more vertical
        0x9C,  //vertical sync start
        0x0E,  //vertical sync end (also unlock bit clear)
        0x8F,  //vertical display end
        0x28,  //offset (40 words per line)
        0x1F,  //underline location
        0x96,  //start vertical blank
        0xB9,  //end vertical blank
        0xA3,  //mode control
        0xFF   //line compare
    };

    //set CRTC registers with proper unlocking
    for (uint8_t i = 0; i < sizeof(crtc_text); i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc_text[i]);
    }

    //graphics controller complete reset for text mode
    outb(0x3CE, 0x00); outb(0x3CF, 0x00); //set/reset
    outb(0x3CE, 0x01); outb(0x3CF, 0x00); //set/reset enable  
    outb(0x3CE, 0x02); outb(0x3CF, 0x00); //color compare
    outb(0x3CE, 0x03); outb(0x3CF, 0x00); //data rotate
    outb(0x3CE, 0x04); outb(0x3CF, 0x00); //read map select
    outb(0x3CE, 0x05); outb(0x3CF, 0x10); //graphics mode = 0x10 (text mode)
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E); //memory map = 0x0E (0xB8000 text)
    outb(0x3CE, 0x07); outb(0x3CF, 0x00); //color don't care
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF); //bit mask

    //attribute controller full reset and proper text mode setup
    //first reset all attribute registers to known state
    for (uint8_t i = 0; i < 16; i++) {
        inb(0x3DA); //reset flip-flop
        outb(0x3C0, i);
        outb(0x3C0, i); //identity mapping for colors 0-15
    }

    //attribute controller mode control registers
    inb(0x3DA); outb(0x3C0, 0x10); outb(0x3C0, 0x0C); //mode: text, no blink, line graphics
    inb(0x3DA); outb(0x3C0, 0x11); outb(0x3C0, 0x00); //overscan color (black)
    inb(0x3DA); outb(0x3C0, 0x12); outb(0x3C0, 0x0F); //color plane enable (all planes)
    inb(0x3DA); outb(0x3C0, 0x13); outb(0x3C0, 0x08); //horiz panning
    inb(0x3DA); outb(0x3C0, 0x14); outb(0x3C0, 0x00); //color select

    //load an 8x16 font by synthesizing from 8x8 font for ASCII 0-127
    //VGA font plane uses 32 bytes per character slot regardless of height
    //so each glyph starts at offset (char_index * 32) fill the first 16 bytes
    //set up font loading to plane 2
    outb(0x3C4, 0x02); outb(0x3C5, 0x04); //write to plane 2 only (font plane)
    outb(0x3C4, 0x04); outb(0x3C5, 0x07); //sequential addressing
    outb(0x3CE, 0x05); outb(0x3CF, 0x00); //write mode 0, read mode 0
    outb(0x3CE, 0x06); outb(0x3CF, 0x05); //map to 0xA0000-0xAFFFF

    volatile uint8_t* font_mem = (volatile uint8_t*)0xA0000;

    //clear font memory completely (256 chars * 32 bytes per char)
    for (int i = 0; i < 256 * 32; i++) font_mem[i] = 0;

    //for ASCII 0-127 duplicate each 8x8 scanline to make 8x16
    //reverse bit order per byte because VGA expects bit7 as leftmost pixel
    for (int ch = 0; ch < 256; ++ch) {
        int base = ch * 32;
        if (ch < 128) {
            for (int row = 0; row < 8; ++row) {
                uint8_t b = font8x8[ch][row];
                uint8_t rb = 0;
                for (int k = 0; k < 8; ++k) if (b & (1u << k)) rb |= (uint8_t)(0x80u >> k);
                font_mem[base + (row * 2 + 0)] = rb;
                font_mem[base + (row * 2 + 1)] = rb;
            }
            //bytes base+16..base+31 remain 0
        } else {
            //leave as 0 for now
        }
    }

    //reset memory mapping back to text mode
    outb(0x3C4, 0x02); outb(0x3C5, 0x03); //enable planes 0,1 for text display
    outb(0x3C4, 0x04); outb(0x3C5, 0x02); //odd/even addressing
    outb(0x3CE, 0x05); outb(0x3CF, 0x10); //graphics mode register = text mode  
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E); //memory map back to 0xB8000 text buffer

    //standard VGA DAC palette setup - critical for proper colors
    outb(0x3C6, 0xFF); //PEL mask
    outb(0x3C8, 0x00); //start at color 0
    
    //load the standard VGA text mode palette
    static const uint8_t standard_palette[16][3] = {
        {0x00, 0x00, 0x00}, //black
        {0x00, 0x00, 0x2A}, //blue  
        {0x00, 0x2A, 0x00}, //green
        {0x00, 0x2A, 0x2A}, //cyan
        {0x2A, 0x00, 0x00}, //red
        {0x2A, 0x00, 0x2A}, //magenta
        {0x2A, 0x15, 0x00}, //brown
        {0x2A, 0x2A, 0x2A}, //light gray
        {0x15, 0x15, 0x15}, //dark gray
        {0x15, 0x15, 0x3F}, //light blue
        {0x15, 0x3F, 0x15}, //light green
        {0x15, 0x3F, 0x3F}, //light cyan
        {0x3F, 0x15, 0x15}, //light red
        {0x3F, 0x15, 0x3F}, //light magenta
        {0x3F, 0x3F, 0x15}, //yellow
        {0x3F, 0x3F, 0x3F}  //white
    };

    for (int i = 0; i < 16; i++) {
        outb(0x3C9, standard_palette[i][0]);
        outb(0x3C9, standard_palette[i][1]);
        outb(0x3C9, standard_palette[i][2]);
    }

    //enable video output
    inb(0x3DA);
    outb(0x3C0, 0x20);

    //update internal mode state
    g_mode = VGA_MODE_TEXT;
    g_w = 640;  
    g_h = 400;  

    //clear text buffer with proper endianness
    volatile uint16_t* textbuf = (volatile uint16_t*)0xB8000;
    
    //clear with white-on-black spaces, ensuring proper byte order
    for (int i = 0; i < 80 * 25; i++) {
        textbuf[i] = 0x0720; //0x07 = white on black, 0x20 = space
    }

    //reset cursor position registers
    outb(0x3D4, 0x0E); outb(0x3D5, 0x00); //cursor high
    outb(0x3D4, 0x0F); outb(0x3D5, 0x00); //cursor low
    
    //enable cursor
    outb(0x3D4, 0x0A); outb(0x3D5, 0x0E); //cursor start line
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0F); //cursor end line
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
    if (g_mode == VGA_MODE_TEXT) return; //no graphics upload in text mode
    if (g_vsync_enabled) vga_wait_vsync();
    const uint8_t* src8 = surface;
    uint8_t* dst8 = VGA;
    unsigned count = (unsigned)(g_w * g_h);

    if (g_mode == VGA_MODE_13H) {
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
                "cld\n\trep movsl"
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
    } else {
        //mode 12h planar upload (4bpp)
        //optimize by sweeping one plane at a time to minimize I/O
        const int bytes_per_scan = g_w / 8; //640/8 = 80
        volatile uint8_t* vram = VGA;
        for (int plane = 0; plane < 4; ++plane) {
            //select target plane once for the whole frame
            outb(0x3C4, 0x02);
            outb(0x3C5, (uint8_t)(1 << plane));
            for (int y = 0; y < g_h; ++y) {
                const uint8_t* src_row = surface + (unsigned)y * (unsigned)g_w;
                uint8_t* dst_row = (uint8_t*)vram + (unsigned)y * (unsigned)bytes_per_scan;
                for (int bx = 0; bx < bytes_per_scan; ++bx) {
                    uint8_t packed = 0;
                    //pack 8 pixels bit for this plane into one byte
                    for (int bit = 0; bit < 8; ++bit) {
                        uint8_t c = src_row[bx * 8 + bit] & 0x0F;
                        if (c & (1u << plane)) packed |= (uint8_t)(0x80u >> bit);
                    }
                    dst_row[bx] = packed;
                }
            }
        }
        //restore map mask to all planes selected (for safety)
        outb(0x3C4, 0x02); outb(0x3C5, 0x0F);
    }
}
