#include "keyboard.h"
#include "../io.h"
#include "../interrupts/irq.h"
#include "../interrupts/pic.h"
#include "timer.h"
#include "../device_manager.h"
#include <stdint.h>
#include <string.h>
#include "../kernel/signal.h"
#include "../process.h"

int shift_pressed = 0;
static int ctrl_pressed = 0;

//ing buffer for ASCII character
static volatile char keybuf[64];
static volatile uint8_t key_head = 0, key_tail = 0;
//ring buffer for key events (ASCII or special like arrows)
static volatile unsigned short evbuf[64];
static volatile uint8_t ev_head = 0, ev_tail = 0;

static volatile uint8_t key_state[128];      //pressed state per scancode (0x00-0x7F)
static volatile uint8_t ext_key_state[128];  //pressed state for extended scancodes (0xE0-prefixed)
static volatile uint8_t e0_pending = 0;      //whether next scancode is extended

//key repeat state
#define KBD_REPEAT_DELAY_TICKS  20  //initial delay before repeating (~200ms if 100Hz)
#define KBD_REPEAT_RATE_TICKS    3  //repeat rate (~33Hz if 100Hz)
static volatile uint8_t  repeat_active = 0;
static volatile uint8_t  repeat_is_ext = 0;
static volatile uint8_t  repeat_scancode = 0; //base scancode (without 0xE0/0x80)
static volatile uint32_t repeat_next_tick = 0;
//keyboard device instance
static device_t keyboard_device;
//device operations for keyboard
static const device_ops_t keyboard_ops = {
    .init = keyboard_device_init,
    .read = keyboard_device_read,
    .write = keyboard_device_write,
    .ioctl = keyboard_device_ioctl,
    .cleanup = keyboard_device_cleanup
};


static inline int keybuf_empty(void) { 
    return key_head == key_tail; 
}
static inline void keybuf_push(char c) {
    uint8_t next = (uint8_t)((key_head + 1) & 63);
    if (next != key_tail) { keybuf[key_head] = c; key_head = next; }
}

static inline int evbuf_empty(void) { 
    return ev_head == ev_tail; 
}
static inline void evbuf_push(unsigned short e) {
    uint8_t next = (uint8_t)((ev_head + 1) & 63);
    if (next != ev_tail) { evbuf[ev_head] = e; ev_head = next; }
}

static inline char keybuf_pop(void) {
    if (keybuf_empty()) return 0;
    char c = keybuf[key_tail];
    key_tail = (uint8_t)((key_tail + 1) & 63);
    return c;
}

static inline unsigned short evbuf_pop(void) {
    if (evbuf_empty()) return 0;
    unsigned short e = evbuf[ev_tail];
    ev_tail = (uint8_t)((ev_tail + 1) & 63);
    return e;
}

static char sc_to_ascii(uint8_t sc){
    if(sc == 0x2A || sc == 0x36){ 
        shift_pressed = 1; 
        return 0; 
    }
    if(sc == 0xAA || sc == 0xB6){ 
        shift_pressed = 0; 
        return 0; 
    }
    if (sc == 0x1D) { //left Ctrl down
        ctrl_pressed = 1; return 0;
    }
    if (sc == 0x9D) { //left Ctrl up
        ctrl_pressed = 0; return 0;
    }
    char ch = 0;
    if(sc > 0 && sc < 128) ch = shift_pressed ? scancode_map_shift[sc] : scancode_map[sc];
    if (!ch) return 0;
    if (ctrl_pressed) {
        //map letters to control codes (A..Z -> 1..26)
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 1);
        else if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 1);
    }
    return ch;
}

static void keyboard_irq_handler(void) {
    uint8_t scancode = inb(kbd_data_port);
    if (scancode == 0xE0) { 
        e0_pending = 1; 
        return; 
    }
    if (scancode & 0x80) {
        //key release
        uint8_t code = (uint8_t)(scancode & 0x7F);
        if (e0_pending) {
            ext_key_state[code] = 0;
            e0_pending = 0;
            //stop repeating if this was the repeating extended key
            if (repeat_active && repeat_is_ext && repeat_scancode == code) {
                repeat_active = 0;
            }
            //right ctrl release
            if (code == 0x1D) {
                ctrl_pressed = 0;
            }
        } else {
            key_state[code] = 0;
            if (scancode == 0xAA || scancode == 0xB6) shift_pressed = 0; //shift up
            //stop repeating if this was the repeating normal key
            if (repeat_active && !repeat_is_ext && repeat_scancode == code) {
                repeat_active = 0;
            }
            //left ctrl release
            if (code == 0x1D) {
                ctrl_pressed = 0;
            }
        }
        return;
    } else {
        //key press
        if (e0_pending) {
            e0_pending = 0;
            //arrows we care about
            if (scancode == 0x4B || scancode == 0x4D || scancode == 0x48 || scancode == 0x50) {
                if (ext_key_state[scancode]) return; //ignore hardware auto-repeat we will do software repeat
                ext_key_state[scancode] = 1;
                unsigned short ev = (unsigned short)(0xE000u | scancode);
                evbuf_push(ev);
                //configure repeat for extended key
                repeat_is_ext = 1;
                repeat_scancode = scancode;
                repeat_active = 1;
                repeat_next_tick = timer_get_ticks() + KBD_REPEAT_DELAY_TICKS;
                return;
            }
            //right ctrl (E0 1D / E0 9D)
            if (scancode == 0x1D) { 
                ctrl_pressed = 1; 
                return; 
            }
            if (scancode == 0x9D) { 
                ctrl_pressed = 0; 
                return; 
            }
            //ignore other extended keys for now
            return;
        }
        if (scancode == 0x2A || scancode == 0x36) { 
            shift_pressed = 1; 
            return;
         } //shift down
        if (scancode == 0x1D) { 
            ctrl_pressed = 1;
            return; 
            } //ctrl down
        if (key_state[scancode]) return; //ignore hardware auto-repeat while held software repeat will handle
        key_state[scancode] = 1;
        char ch = sc_to_ascii(scancode);
        if (ch) {
            keybuf_push(ch);
            evbuf_push((unsigned short)(unsigned char)ch);
            if (ch == 3) {
                process_t* cur = process_get_current();
                if (cur) signal_raise(cur, SIGINT);
            }
            //set up repeat for this normal key
            repeat_is_ext = 0;
            repeat_scancode = scancode;
            repeat_active = 1;
            repeat_next_tick = timer_get_ticks() + KBD_REPEAT_DELAY_TICKS;
        }
    }
}

char kb_poll(void) {
    //prefer IRQ-buffered input
    char c = keybuf_pop();
    if (c) return c;
    //fallback to hardware poll (e.x if interrupts are disabled for some reason)
    uint8_t status = inb(kbd_status_port);
    if (status & 1) { //output buffer full
        if (status & 0x20) {
            //AUX bit set equals mouse data present do not consume here let mouse driver handle it
            return 0;
        }
        uint8_t scancode = inb(kbd_data_port);
        if (scancode == 0xE0) { 
            e0_pending = 1; 
            return 0; 
        }
        if (scancode & 0x80) {
            uint8_t code = (uint8_t)(scancode & 0x7F);
            if (e0_pending) { 
                ext_key_state[code] = 0; 
                e0_pending = 0; 
                if (code == 0x1D) ctrl_pressed = 0; 
            }
            else { 
                key_state[code] = 0; 
                if (scancode == 0xAA || scancode == 0xB6) shift_pressed = 0; 
                if (code == 0x1D) ctrl_pressed = 0; 
            }
            return 0;
        } else {
            if (e0_pending) {
                e0_pending = 0;
                if (scancode == 0x4B || scancode == 0x4D || scancode == 0x48 || scancode == 0x50) {
                    if (ext_key_state[scancode]) return 0;
                    ext_key_state[scancode] = 1;
                    unsigned short ev = (unsigned short)(0xE000u | scancode);
                    evbuf_push(ev);
                    return 0;
                }
                if (scancode == 0x1D) { 
                    ctrl_pressed = 1; 
                    return 0; 
                }
                if (scancode == 0x9D) { 
                    ctrl_pressed = 0; 
                    return 0; 
                }
                return 0;
            }
            if (scancode == 0x2A || scancode == 0x36) { 
                shift_pressed = 1; 
                return 0; 
            }
            if (scancode == 0x1D) { 
                ctrl_pressed = 1; 
                return 0; 
            }
            if (key_state[scancode]) return 0; //ignore auto-repeat
            key_state[scancode] = 1;
            char ch = sc_to_ascii(scancode);
            if (ch) { 
                keybuf_push(ch); 
                evbuf_push((unsigned short)(unsigned char)ch);
                if (ch == 3) {
                    process_t* cur = process_get_current();
                    if (cur) signal_raise(cur, SIGINT);
                }
            }
            return ch;
        }
    }
    return 0; //no key pressed
}

char getkey(void){
    for (;;) {
        //first consume buffered input
        char c = kb_poll();
        if (c) return c;

        //software key repeat for ASCII keys only
        if (repeat_active && !repeat_is_ext) {
            uint32_t now = timer_get_ticks();
            if ((int32_t)(now - repeat_next_tick) >= 0) {
                char rc = sc_to_ascii(repeat_scancode);
                if (rc) {
                    //schedule next repeat and return
                    repeat_next_tick = now + KBD_REPEAT_RATE_TICKS;
                    return rc;
                } else {
                    //no valid ascii from scancode stop repeating
                    repeat_active = 0;
                }
            }
        }
        __asm__ volatile ("hlt"); //wait for next IRQ to save CPU
    }
}

uint16_t kbd_getevent(void){
    for(;;){
        //buffered events first
        unsigned short e = evbuf_pop();
        if (e) return e;

        //also poll to convert pending scan codes into events in fallback
        (void)kb_poll();

        //software repeat for both ascii and extended keys
        if (repeat_active) {
            uint32_t now = timer_get_ticks();
            if ((int32_t)(now - repeat_next_tick) >= 0) {
                repeat_next_tick = now + KBD_REPEAT_RATE_TICKS;
                if (repeat_is_ext) {
                    return (unsigned short)(0xE000u | repeat_scancode);
                } else {
                    char rc = sc_to_ascii(repeat_scancode);
                    if (rc) return (unsigned short)(unsigned char)rc;
                    //invalid ascii -> stop repeating
                    repeat_active = 0;
                }
            }
        }
        __asm__ volatile ("hlt");
    }
}

//non-blocking event poll for GUI loop (does not steal mouse bytes)
uint16_t kbd_poll_event(void) {
    //try buffered first
    unsigned short e = evbuf_pop();
    if (e) return e;

    //allow fallback conversion without blocking
    (void)kb_poll();

    e = evbuf_pop();
    if (e) return e;

    //software repeat without blocking
    if (repeat_active) {
        uint32_t now = timer_get_ticks();
        if ((int32_t)(now - repeat_next_tick) >= 0) {
            repeat_next_tick = now + KBD_REPEAT_RATE_TICKS;
            if (repeat_is_ext) {
                return (unsigned short)(0xE000u | repeat_scancode);
            } else {
                char rc = sc_to_ascii(repeat_scancode);
                if (rc) return (unsigned short)(unsigned char)rc;
                repeat_active = 0;
            }
        }
    }
    return 0;
}

void keyboard_init(void) {
    irq_install_handler(1, keyboard_irq_handler);
    pic_clear_mask(1); //unmask IRQ1
}

void kbd_flush(void) {
    __asm__ volatile ("cli");
    key_head = key_tail = 0;
    ev_head = ev_tail = 0;
    e0_pending = 0;
    for (int i = 0; i < 128; ++i) { 
        key_state[i] = 0; 
        ext_key_state[i] = 0; 
    }
    repeat_active = 0;
    repeat_is_ext = 0;
    repeat_scancode = 0;
    repeat_next_tick = 0;
    __asm__ volatile ("sti");
}

device_t* keyboard_create_device(void) {
    //initialize device structure
    strcpy(keyboard_device.name, "ps2kbd0");
    keyboard_device.type = DEVICE_TYPE_INPUT;
    keyboard_device.subtype = DEVICE_SUBTYPE_KEYBOARD;
    keyboard_device.status = DEVICE_STATUS_UNINITIALIZED;
    keyboard_device.device_id = 0; //will be assigned by device manager
    keyboard_device.private_data = NULL;
    keyboard_device.ops = &keyboard_ops;
    keyboard_device.next = NULL;
    
    return &keyboard_device;
}

int keyboard_device_init(device_t* device) {
    (void)device; //unused for now
    
    //keyboard is already initialized by keyboard_init()
    //just verify it's working
    return 0; //success
}

int keyboard_device_read(device_t* device, uint32_t offset, void* buffer, uint32_t size) {
    (void)device; //unused
    (void)offset; //unused for keyboard
    
    if (!buffer || size == 0) {
        return -1;
    }
    
    char* char_buffer = (char*)buffer;
    uint32_t bytes_read = 0;
    
    //read available characters
    while (bytes_read < size) {
        char c = kb_poll(); //non-blocking poll
        if (c == 0) {
            break; //no more characters available
        }
        char_buffer[bytes_read] = c;
        bytes_read++;
    }
    
    return bytes_read;
}

int keyboard_device_write(device_t* device, uint32_t offset, const void* buffer, uint32_t size) {
    (void)device;
    (void)offset;
    (void)buffer;
    (void)size;
    
    //keyboard is input-only device
    return -1;
}

int keyboard_device_ioctl(device_t* device, uint32_t cmd, void* arg) {
    (void)device;
    (void)cmd;
    (void)arg;
    
    //no ioctl commands implemented yet
    return -1;
}

void keyboard_device_cleanup(device_t* device) {
    (void)device;
    //nothing to cleanup for keyboard
}

int keyboard_register_device(void) {
    //create keyboard device
    device_t* kbd_device = keyboard_create_device();
    if (!kbd_device) {
        return -1;
    }
    
    //register with device manager first
    if (device_register(kbd_device) != 0) {
        keyboard_device_cleanup(kbd_device);
        return -1;
    }
    
    //then initialize through device manager
    if (device_init(kbd_device) != 0) {
        //cleanup on failure
        device_unregister(kbd_device->device_id);
        return -1;
    }
    
    return 0;
}