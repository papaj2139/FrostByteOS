#include "tty.h"
#include "keyboard.h"
#include "../device_manager.h"
#include "../drivers/serial.h"
#include <string.h>
#include "../kernel/cga.h"
#include "../process.h"
#include "../kernel/signal.h"

static device_t g_tty_dev;
static uint32_t g_tty_mode = (TTY_MODE_CANON | TTY_MODE_ECHO);
static volatile int g_tty_reading = 0;

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
                        print(one, 0x0F);
                    }
                }
                continue;
            }

            if ((unsigned char)c >= 32 || c == '\n' || c == '\t') {
                if (pos < size) {
                    buf[pos++] = c;
                    if (mode & TTY_MODE_ECHO) {
                        one[0] = c;
                        print(one, 0x0F);
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
                continue; //skip extended keys in raw for now
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
            if ((e & 0xFF00u) == 0xE000u) continue;
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

//write bytes to the text console
int tty_write(const char* buf, uint32_t size) {
    if (!buf || size == 0) return 0;
    char tmp[256];
    uint32_t outp = 0;
    uint32_t total = 0;
    for (uint32_t i = 0; i < size; i++) {
        char c = buf[i];
        if (c == '\n') {
            //flush any pending printable chars
            if (outp > 0) {
                tmp[outp] = '\0';
                print(tmp, 0x0F);
                total += outp;
                outp = 0;
            }
            //newline
            print("\n", 0x0F);
            total++;
        } else if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
            //buffer printable ASCII
            tmp[outp++] = c;
            if (outp == sizeof(tmp) - 1) {
                tmp[outp] = '\0';
                print(tmp, 0x0F);
                total += outp;
                outp = 0;
            }
        } else {
            //skip non-printable (including NUL) to avoid control garbage on screen
            //still count as consumed
            total++;
        }
    }
    //flush any remaining buffered printable characters
    if (outp > 0) {
        tmp[outp] = '\0';
        print(tmp, 0x0F);
        total += outp;
    }
    return (int)total;
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
