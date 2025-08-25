#ifndef KEYBOARD_H
#define KEYBOARD_H

#define kbd_data_port 0x60
#define kbd_status_port 0x64

extern int shift_pressed;

#define K_ARROW_LEFT  0xE04B
#define K_ARROW_RIGHT 0xE04D
#define K_ARROW_UP    0xE048
#define K_ARROW_DOWN  0xE050

static const char scancode_map[128] = {
 0,27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
 '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
 'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z','x',
 'c','v','b','n','m',',','.','/',0,'*',0,' '
};

static const char scancode_map_shift[128] = {
 0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
 '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
 'A','S','D','F','G','H','J','K','L',':','"','~',0,'|','Z','X',
 'C','V','B','N','M','<','>','?',0,'*',0,' '
};

//read next ASCII character ignores special keys
char getkey();
//non-blocking ASCII poll returns 0 if none
char kb_poll();
//read next key event  returns ASCII in low byte for printable keys
//or one of the K_* special codes for extended keys
unsigned short kbd_getevent(void);
void keyboard_init(void);
//clear any pending keyboard events and reset repeat state
void kbd_flush(void);

#endif