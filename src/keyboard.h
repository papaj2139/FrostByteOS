#ifndef KEYBOARD_H
#define KEYBOARD_H

#define kbd_data_port 0x60
#define kbd_status_port 0x64

extern int shift_pressed;

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

char getkey();
char kb_poll();

#endif