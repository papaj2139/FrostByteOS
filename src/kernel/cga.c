#include "cga.h"
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include "../io.h"
#include "../drivers/fbcon.h"
#include "../kernel/klog.h"
extern int g_console_quiet;

#define VID_MEM ((unsigned char*)0xB8000)

uint8_t cursor_x = 0;
uint8_t cursor_y = 0;

static void update_cursor(void){
    unsigned short pos = (unsigned short)(cursor_y * SCREEN_WIDTH + cursor_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll_if_needed(void){
    if (cursor_y >= SCREEN_HEIGHT) {
        for (int y = 1; y < SCREEN_HEIGHT; ++y) {
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                VID_MEM[((y - 1) * SCREEN_WIDTH + x) * 2] = VID_MEM[(y * SCREEN_WIDTH + x) * 2];
                VID_MEM[((y - 1) * SCREEN_WIDTH + x) * 2 + 1] = VID_MEM[(y * SCREEN_WIDTH + x) * 2 + 1];
            }
        }
        int last = (SCREEN_HEIGHT - 1) * SCREEN_WIDTH;
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            VID_MEM[(last + x) * 2] = ' ';
            VID_MEM[(last + x) * 2 + 1] = 0x0F;
        }
        cursor_y = SCREEN_HEIGHT - 1;
    }
}

void cga_clear_with_attr(unsigned char attr){
    if (fbcon_available()) { fbcon_clear_with_attr(attr); return; }
    for (unsigned int j = 0; j < SCREEN_WIDTH * SCREEN_HEIGHT; j++) {
        VID_MEM[j * 2] = ' ';
        VID_MEM[j * 2 + 1] = attr;
    }
    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

void kclear(void){
    cga_clear_with_attr(0x0F);
}

void cga_print_at(const char* str, unsigned char attr, unsigned int x, unsigned int y) {
    unsigned int i = 0;
    unsigned int pos = (y * SCREEN_WIDTH + x) * 2;
    while (str[i]) {
        VID_MEM[pos] = str[i];
        VID_MEM[pos + 1] = attr;
        i++;
        pos += 2;
    }
}

int putchar_term(char c, unsigned char colour) {
    if (fbcon_available()) {
        return fbcon_putchar(c, colour);
    }
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = ' ';
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = SCREEN_WIDTH - 1;
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = ' ';
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
        } else {
            return 0;
        }
    } else {
        VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = c;
        VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
        cursor_x++;
        if (cursor_x >= SCREEN_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    scroll_if_needed();
    update_cursor();
    return 1;
}

//put single char bypassing quiet flag (for TTY echo)
int putchar_term_force(char c, unsigned char colour) {
    if (fbcon_available()) {
        return fbcon_putchar(c, colour);
    }
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = ' ';
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = SCREEN_WIDTH - 1;
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = ' ';
            VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
        } else {
            return 0;
        }
    } else {
        VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2] = c;
        VID_MEM[(cursor_y * SCREEN_WIDTH + cursor_x) * 2 + 1] = colour;
        cursor_x++;
        if (cursor_x >= SCREEN_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    scroll_if_needed();
    update_cursor();
    return 1;
}

void print(char* msg, unsigned char colour){
    if (!msg) return;
    //always mirror to klog for later retrieval via /dev/kmsg
    klog_write(msg, strlen(msg));
    if (g_console_quiet) return; //suppress on-screen if quiet
    int i = 0;
    while(msg[i] != '\0'){
        char c = msg[i++];
        if (!putchar_term(c, colour)) break;
    }
}

void enable_cursor(uint8_t start, uint8_t end){
    outb(0x3D4,0x0A);
    outb(0x3D5,(inb(0x3D5)&0xC0)|start);
    outb(0x3D4,0x0B);
    outb(0x3D5,(inb(0x3D5)&0xE0)|end);
}

void move_cursor(uint16_t row, uint16_t col){
    uint16_t pos = (uint16_t)(row * SCREEN_WIDTH + col);
    outb(0x3D4,0x0F);
    outb(0x3D5,(uint8_t)(pos & 0xFF));
    outb(0x3D4,0x0E);
    outb(0x3D5,(uint8_t)((pos >> 8) & 0xFF));
    cursor_y = (uint8_t)row;
    cursor_x = (uint8_t)col;
}

void disable_cursor(void){
    // Disable text mode cursor by setting bit 5 of Cursor Start register
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

uint16_t get_line_length(uint16_t row){
    if(row >= SCREEN_HEIGHT) return 0;
    uint16_t len = SCREEN_WIDTH;
    while(len > 0){
        char c = VID_MEM[(row * SCREEN_WIDTH + (len - 1)) * 2];
        if(c != ' ') break;
        len--;
    }
    return len;
}

void put_char_at(char c, uint8_t attr, int x, int y) {
    int offset = (y * SCREEN_WIDTH + x) * 2;
    VID_MEM[offset] = c;
    VID_MEM[offset + 1] = attr;
}

//CGA write function that just calls print for now
int cga_write(const char* buf, uint32_t size) {
    if (!buf || size == 0) return 0;
    char tmp[256];
    uint32_t written = 0;

    for (uint32_t i = 0; i < size; i++) {
        char c = buf[i];
        if (c == '\n') {
            if (written > 0) {
                tmp[written] = '\0';
                print(tmp, 0x0F);
                written = 0;
            }
            print("\n", 0x0F);
        } else if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
            tmp[written++] = c;
            if (written >= sizeof(tmp) - 1) {
                tmp[written] = '\0';
                print(tmp, 0x0F);
                written = 0;
            }
        }
    }

    if (written > 0) {
        tmp[written] = '\0';
        print(tmp, 0x0F);
    }

    return (int)size;
}
