#ifndef CGA_H
#define CGA_H

#include <stdint.h>

//80x25 text mode
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

//alias for VGA text buffer
#define VID_MEM ((unsigned char*)0xB8000)

//shared cursor state
extern uint8_t cursor_x;
extern uint8_t cursor_y;

//basic text console API
void kclear(void);
void print(char* msg, unsigned char colour);
void enable_cursor(uint8_t start, uint8_t end);
void move_cursor(uint16_t row, uint16_t col);
int putchar_term(char c, unsigned char colour);
void put_char_at(char c, uint8_t attr, int x, int y);
uint16_t get_line_length(uint16_t row);

//helpers for text placement and screen fill
void cga_print_at(const char* str, unsigned char attr, unsigned int x, unsigned int y);
void cga_clear_with_attr(unsigned char attr);

#endif
