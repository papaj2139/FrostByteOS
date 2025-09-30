#include "tty.h"
#include "keyboard.h"
#include "../device_manager.h"
#include "../drivers/serial.h"
#include <string.h>
#include "../kernel/cga.h"
#include "../kernel/klog.h"
#include "../drivers/fbcon.h"
#include "../process.h"
#include "../kernel/signal.h"

static device_t g_tty_dev;
static uint32_t g_tty_mode = (TTY_MODE_CANON | TTY_MODE_ECHO);
static volatile int g_tty_reading = 0;

//ANSI escape sequence parser state
typedef enum {
    ANSI_STATE_NORMAL,
    ANSI_STATE_ESC,
    ANSI_STATE_CSI,
    ANSI_STATE_CSI_PARAM
} ansi_state_t;

static ansi_state_t ansi_state = ANSI_STATE_NORMAL;
static int ansi_params[8];
static int ansi_param_count = 0;
static uint8_t current_attr = 0x0F; //default white on black

int tty_read_mode(char* buf, uint32_t size, uint32_t mode) {
    if (!buf || size == 0) return 0;
    uint32_t pos = 0;
    char one[2] = {0, 0};
    g_tty_reading = 1;

    if (mode & TTY_MODE_CANON) {
        //canonical mode line buffering with backspace editing optional echo
        for (;;) {
            unsigned short ev = kbd_getevent(); //blocks and uses HLT 
            if ((ev & 0xFF00u) == 0xE000u) {
                //gnore extended keys for now
                continue;
            }
            char c = (char)(ev & 0xFF);
            if (c == '\r') c = '\n';
            if (c == 3) { //ctrl-C (ETX)
                if (mode & TTY_MODE_ECHO) {
                    char cc[] = "^C\n";
                    print(cc, 0x0F);
                }
                g_tty_reading = 0;
                return 0; //interrupt the read without signal (shell stays alive)
            }
            if (c == 4) { //Ctrl-D (EOT)
                int r = (int)pos;
                g_tty_reading = 0;
                return r;
            }

            if (c == '\b') {
                if (pos > 0) {
                    pos--;
                    if (mode & TTY_MODE_ECHO) {
                        one[0] = '\b';
                        //use force version to bypass quiet flag
                        if (fbcon_available()) {
                            fbcon_putchar('\b', 0x0F);
                        } else {
                            putchar_term_force('\b', 0x0F);
                        }
                    }
                }
                continue;
            }

            if ((unsigned char)c >= 32 || c == '\n' || c == '\t') {
                if (pos < size) {
                    buf[pos++] = c;
                    if (mode & TTY_MODE_ECHO) {
                        one[0] = c;
                        //use force version to bypass quiet flag
                        if (fbcon_available()) {
                            fbcon_putchar(c, 0x0F);
                        } else {
                            putchar_term_force(c, 0x0F);
                        }
                    }
                }
            }

            if (c == '\n' || pos >= size) {
                g_tty_reading = 0;
                return (int)pos;
            }
        }
    } else {
        //raw mode return as soon as we have at least one byte no line discipline
        //block for the first byte
        for (;;) {
            unsigned short ev = kbd_getevent();
            if ((ev & 0xFF00u) == 0xE000u) {
                //convert extended keys (arrows) to ANSI escape sequences
                uint8_t sc = (uint8_t)(ev & 0xFF);
                if (sc == 0x48 && pos + 3 <= size) { //Up
                    buf[pos++] = '\033';
                    buf[pos++] = '[';
                    buf[pos++] = 'A';
                    break;
                } else if (sc == 0x50 && pos + 3 <= size) { //Down
                    buf[pos++] = '\033';
                    buf[pos++] = '[';
                    buf[pos++] = 'B';
                    break;
                } else if (sc == 0x4D && pos + 3 <= size) { //Right
                    buf[pos++] = '\033';
                    buf[pos++] = '[';
                    buf[pos++] = 'C';
                    break;
                } else if (sc == 0x4B && pos + 3 <= size) { //Left
                    buf[pos++] = '\033';
                    buf[pos++] = '[';
                    buf[pos++] = 'D';
                    break;
                } else {
                    continue; //skip other extended keys
                }
            }
            char c = (char)(ev & 0xFF);
            if (c == '\r') c = '\n';
            if (c == 3) {
                if (mode & TTY_MODE_ECHO) {
                    char cc[] = "^C\n";
                    print(cc, 0x0F);
                }
                g_tty_reading = 0;
                return (int)pos;
            }
            if (c == 4) { //ctrl-D
                int r = (int)pos;
                g_tty_reading = 0;
                return r;
            }
            buf[pos++] = c;
            if (mode & TTY_MODE_ECHO) {
                one[0] = c;
                print(one, 0x0F);
            }
            break;
        }
        //drain any immediately available bytes without blocking further
        while (pos < size) {
            unsigned short e = kbd_poll_event();
            if (!e) break;
            if ((e & 0xFF00u) == 0xE000u) {
                //convert extended keys (arrows) to ANSI escape sequences
                uint8_t sc = (uint8_t)(e & 0xFF);
                if (sc == 0x48 && pos + 3 <= size) { //Up
                    buf[pos++] = '\033';
                    buf[pos++] = '[';
                    buf[pos++] = 'A';
                } else if (sc == 0x50 && pos + 3 <= size) { //Down
                    buf[pos++] = '\033';
                    buf[pos++] = '[';
                    buf[pos++] = 'B';
                } else if (sc == 0x4D && pos + 3 <= size) { //Right
                    buf[pos++] = '\033';
                    buf[pos++] = '[';
                    buf[pos++] = 'C';
                } else if (sc == 0x4B && pos + 3 <= size) { //Left
                    buf[pos++] = '\033';
                    buf[pos++] = '[';
                    buf[pos++] = 'D';
                }
                continue;
            }
            char c = (char)(e & 0xFF);
            if (c == '\r') c = '\n';
            if (c == 3) {
                if (mode & TTY_MODE_ECHO) {
                    char cc[] = "^C\n";
                    print(cc, 0x0F);
                }
                g_tty_reading = 0;
                return (int)pos; //return what we have so far (possibly 0)
            }
            if (c == 4) { //ctrl-D
                int r = (int)pos;
                g_tty_reading = 0;
                return r;
            }
            buf[pos++] = c;
            if (mode & TTY_MODE_ECHO) {
                one[0] = c;
                print(one, 0x0F);
            }
        }
        g_tty_reading = 0;
        return (int)pos;
    }
}

int tty_read(char* buf, uint32_t size) {
    return tty_read_mode(buf, size, g_tty_mode);
}

//ANSI color mapping (simplified)
static uint8_t ansi_to_vga_color(int ansi_color) {
    //map ANSI color codes to VGA attributes
    if (ansi_color == 0) return 0x00; //black
    if (ansi_color == 30) return 0x00; //black fg
    if (ansi_color == 31) return 0x04; //red fg
    if (ansi_color == 32) return 0x02; //green fg
    if (ansi_color == 33) return 0x06; //yellow fg
    if (ansi_color == 34) return 0x01; //blue fg
    if (ansi_color == 35) return 0x05; //magenta fg
    if (ansi_color == 36) return 0x03; //cyan fg
    if (ansi_color == 37) return 0x07; //white fg
    if (ansi_color == 90) return 0x08; //bright black (gray) fg
    if (ansi_color == 91) return 0x0C; //bright red fg
    if (ansi_color == 92) return 0x0A; //bright green fg
    if (ansi_color == 93) return 0x0E; //bright yellow fg
    if (ansi_color == 94) return 0x09; //bright blue fg
    if (ansi_color == 95) return 0x0D; //bright magenta fg
    if (ansi_color == 96) return 0x0B; //bright cyan fg
    if (ansi_color == 97) return 0x0F; //bright white fg
    return current_attr & 0x0F; //keep current fg
}

//process ANSI CSI sequence
static void process_ansi_csi(char final_char) {
    if (ansi_param_count == 0) {
        ansi_params[0] = 0;
        ansi_param_count = 1;
    }
    
    //cursor movement: ESC[<row>;<col>H or ESC[<row>;<col>f
    if (final_char == 'H' || final_char == 'f') {
        if (ansi_param_count >= 2) {
            int row = ansi_params[0] - 1; //ANSI is 1-indexed
            int col = ansi_params[1] - 1;
            if (row < 0) row = 0;
            if (col < 0) col = 0;
            if (row >= SCREEN_HEIGHT) row = SCREEN_HEIGHT - 1;
            if (col >= SCREEN_WIDTH) col = SCREEN_WIDTH - 1;
            move_cursor(row, col);
        }
        return;
    }
    
    if (final_char == 'J') {
        //clear screen
        int param = ansi_params[0];
        if (param == 2) {
            kclear();
        }
        return;
    }
    
    if (final_char == 'K') {
        //clear line (to end)
        for (int x = cursor_x; x < SCREEN_WIDTH; x++) {
            put_char_at(' ', current_attr, x, cursor_y);
        }
        return;
    }
    
    if (final_char == 'm') {
        //SGR (Select Graphic Rendition) - colors and attributes
        for (int i = 0; i < ansi_param_count; i++) {
            int param = ansi_params[i];
            if (param == 0) {
                //reset to default
                current_attr = 0x0F;
            } else if (param == 7) {
                //inverse video
                current_attr = 0x70; //white bg, black fg
            } else if ((param >= 30 && param <= 37) || (param >= 90 && param <= 97)) {
                //foreground color
                uint8_t fg = ansi_to_vga_color(param);
                current_attr = (current_attr & 0xF0) | fg;
            }
        }
    }
}

//write bytes to the text console with ANSI escape sequence support
int tty_write(const char* buf, uint32_t size) {
    if (!buf || size == 0) return 0;
    
    //always mirror to klog
    klog_write(buf, size);
    
    //use fbcon if available, otherwise use CGA text mode with ANSI parsing
    if (fbcon_available()) {
        return fbcon_write(buf, size);
    } else {
        //use CGA ANSI parser in TTY layer for text mode
        char tmp[256];
        uint32_t outp = 0;
        uint32_t total = 0;
        
        for (uint32_t i = 0; i < size; i++) {
            char c = buf[i];
            
            switch (ansi_state) {
                case ANSI_STATE_NORMAL:
                    if (c == '\033' || c == '\x1B') { //ESC
                        //flush any buffered text before processing escape
                        if (outp > 0) {
                            tmp[outp] = '\0';
                            print(tmp, current_attr);
                            outp = 0;
                        }
                        ansi_state = ANSI_STATE_ESC;
                    } else if (c == '\n') {
                        //flush buffered text
                        if (outp > 0) {
                            tmp[outp] = '\0';
                            print(tmp, current_attr);
                            outp = 0;
                        }
                        //print newline
                        print("\n", current_attr);
                    } else if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
                        //buffer printable characters
                        tmp[outp++] = c;
                        if (outp == sizeof(tmp) - 1) {
                            tmp[outp] = '\0';
                            print(tmp, current_attr);
                            outp = 0;
                        }
                    } else if (c == '\b') {
                        //flush and print backspace
                        if (outp > 0) {
                            tmp[outp] = '\0';
                            print(tmp, current_attr);
                            outp = 0;
                        }
                        char bs[2] = {c, 0};
                        print(bs, current_attr);
                    }
                    //ignore other control chars
                    total++;
                    break;
                    
                case ANSI_STATE_ESC:
                    if (c == '[') {
                        ansi_state = ANSI_STATE_CSI;
                        ansi_param_count = 0;
                        for (int j = 0; j < 8; j++) ansi_params[j] = 0;
                    } else if (c == '?') {
                        //CSI ? sequences - just consume for now
                        ansi_state = ANSI_STATE_CSI;
                        ansi_param_count = 0;
                    } else {
                        //unknown escape - go back to normal
                        ansi_state = ANSI_STATE_NORMAL;
                    }
                    total++;
                    break;
                    
                case ANSI_STATE_CSI:
                    if (c >= '0' && c <= '9') {
                        if (ansi_param_count == 0) ansi_param_count = 1;
                        ansi_params[ansi_param_count - 1] = ansi_params[ansi_param_count - 1] * 10 + (c - '0');
                        ansi_state = ANSI_STATE_CSI_PARAM;
                    } else if (c == ';') {
                        if (ansi_param_count < 8) ansi_param_count++;
                    } else if (c == 'H' || c == 'f' || c == 'J' || c == 'K' || c == 'm' ||
                               c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'h' || c == 'l') {
                        //process CSI with final character
                        process_ansi_csi(c);
                        ansi_state = ANSI_STATE_NORMAL;
                    } else {
                        //unknown CSI - back to normal
                        ansi_state = ANSI_STATE_NORMAL;
                    }
                    total++;
                    break;
                    
                case ANSI_STATE_CSI_PARAM:
                    if (c >= '0' && c <= '9') {
                        ansi_params[ansi_param_count - 1] = ansi_params[ansi_param_count - 1] * 10 + (c - '0');
                    } else if (c == ';') {
                        if (ansi_param_count < 8) ansi_param_count++;
                        ansi_state = ANSI_STATE_CSI;
                    } else if (c == 'H' || c == 'f' || c == 'J' || c == 'K' || c == 'm' ||
                               c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'h' || c == 'l') {
                        //process CSI with final character
                        process_ansi_csi(c);
                        ansi_state = ANSI_STATE_NORMAL;
                    } else {
                        //unknown - back to normal
                        ansi_state = ANSI_STATE_NORMAL;
                    }
                    total++;
                    break;
            }
        }
        
        //flush any remaining buffered text
        if (outp > 0) {
            tmp[outp] = '\0';
            print(tmp, current_attr);
        }
        
        return (int)total;
    }
}

//mode control
void tty_set_mode(uint32_t mode) { 
    g_tty_mode = mode; 
}

uint32_t tty_get_mode(void) { 
    return g_tty_mode; 
}

int tty_is_reading(void) { 
    return g_tty_reading; 
}

int tty_ioctl(uint32_t cmd, void* arg) {
    switch (cmd) {
        case TTY_IOCTL_SET_MODE:
            if (!arg) return -1;
            tty_set_mode(*(uint32_t*)arg);
            return 0;
        case TTY_IOCTL_GET_MODE:
            if (!arg) return -1;
            *(uint32_t*)arg = tty_get_mode();
            return 0;
        default:
            return -1;
    }
}

//device ops forwarders
static int tty_dev_init(device_t* d) {
    (void)d;
    return 0;
}
static int tty_dev_read(device_t* d, uint32_t off, void* buffer, uint32_t size) {
    (void)d; (void)off;
    return tty_read((char*)buffer, size);
}
static int tty_dev_write(device_t* d, uint32_t off, const void* buffer, uint32_t size) {
    (void)d; (void)off;
    return tty_write((const char*)buffer, size);
}
static int tty_dev_ioctl(device_t* d, uint32_t cmd, void* arg) {
    (void)d; 
    return tty_ioctl(cmd, arg);
}
static void tty_dev_cleanup(device_t* d) { (void)d; }

static const device_ops_t tty_ops = {
    .init = tty_dev_init,
    .read = tty_dev_read,
    .write = tty_dev_write,
    .ioctl = tty_dev_ioctl,
    .cleanup = tty_dev_cleanup
};

int tty_register_device(void) {
    memset(&g_tty_dev, 0, sizeof(g_tty_dev));
    strcpy(g_tty_dev.name, "tty0");
    g_tty_dev.type = DEVICE_TYPE_OUTPUT; //mixed but good enough for listing
    g_tty_dev.subtype = DEVICE_SUBTYPE_DISPLAY;
    g_tty_dev.status = DEVICE_STATUS_UNINITIALIZED;
    g_tty_dev.ops = &tty_ops;

    if (device_register(&g_tty_dev) != 0) return -1;
    if (device_init(&g_tty_dev) != 0) {
        device_unregister(g_tty_dev.device_id);
        return -1;
    }
    g_tty_dev.status = DEVICE_STATUS_READY;
    #if DEBUG_ENABLED
    serial_write_string("TTY device registered as tty0\n");
    #endif
    return 0;
}
