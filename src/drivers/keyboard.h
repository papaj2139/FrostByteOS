#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../device_manager.h"

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
//non-blocking event poll returns 0 if no event available ASCII in low byte or 0xE0xx for extended
unsigned short kbd_poll_event(void);
void keyboard_init(void);
//clear any pending keyboard events and reset repeat state
void kbd_flush(void);

//device manager integration
device_t* keyboard_create_device(void);
int keyboard_device_init(device_t* device);
int keyboard_register_device(void);
int keyboard_device_read(device_t* device, uint32_t offset, void* buffer, uint32_t size);
int keyboard_device_write(device_t* device, uint32_t offset, const void* buffer, uint32_t size);
int keyboard_device_ioctl(device_t* device, uint32_t cmd, void* arg);
void keyboard_device_cleanup(device_t* device);

//input event for /dev/input/kbd0
//type: 1=press, 0=release, 2=repeat
//code: ASCII in low 8-bit for printable keys extended keys as 0xE0xx
//time_ms: milliseconds since boot (approx)
typedef struct {
    uint32_t time_ms;
    uint16_t code;
    uint8_t type;
    uint8_t reserved;
} kbd_input_event_t;

//fill buffer with up to max_events events if blocking!=0 block until at least one event
//returns number of events copied
int kbd_input_read_events(kbd_input_event_t* out, uint32_t max_events, int blocking);
int kbd_input_has_events(void);

#endif
