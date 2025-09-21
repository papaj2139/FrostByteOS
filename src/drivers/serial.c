#include "serial.h"
#include "../io.h"
#include "../device_manager.h"
#include <string.h>
#include <stdarg.h>
#include "../kernel/klog.h"

static uint16_t serial_port = SERIAL_COM1_BASE;
static device_t g_serial_dev;

//forward declarations for device ops
static int serial_dev_init(struct device* d);
static int serial_dev_read(struct device* d, uint32_t off, void* buf, uint32_t sz);
static int serial_dev_write(struct device* d, uint32_t off, const void* buf, uint32_t sz);
static int serial_dev_ioctl(struct device* d, uint32_t cmd, void* arg);
static void serial_dev_cleanup(struct device* d);

static const device_ops_t serial_ops = {
    .init = serial_dev_init,
    .read = serial_dev_read,
    .write = serial_dev_write,
    .ioctl = serial_dev_ioctl,
    .cleanup = serial_dev_cleanup,
};


int serial_init(void) {
    //disable all interrupts
    outb(serial_port + 1, 0x00);
    
    //enable DLAB (set baud rate divisor)
    outb(SERIAL_LINE_COMMAND_PORT(serial_port), SERIAL_LINE_ENABLE_DLAB);
    
    //set divisor to 3 (lo byte) 38400 baud
    outb(SERIAL_DATA_PORT(serial_port), 0x03);
    outb(serial_port + 1, 0x00);                    
    
    //8 bits, no parity, one stop bit
    outb(SERIAL_LINE_COMMAND_PORT(serial_port), 0x03);
    
    //enable FIFO, clear them, with 14-byte threshold
    outb(SERIAL_FIFO_COMMAND_PORT(serial_port), 0xC7);
    
    //IRQs enabled, RTS/DSR set (preperation for adding interrupts later)
    outb(SERIAL_MODEM_COMMAND_PORT(serial_port), 0x0B);
    
    //set in loopback mode, test the serial chip
    outb(SERIAL_MODEM_COMMAND_PORT(serial_port), 0x1E);
    
    //test serial chip (send byte 0xAE and check if serial returns same byte)
    outb(SERIAL_DATA_PORT(serial_port), 0xAE);
    
    //check if serial is faulty
    if(inb(SERIAL_DATA_PORT(serial_port)) != 0xAE) {
        return 1; //faulty
    }
    
    //if serial is not faulty set it in normal operation mode
    outb(SERIAL_MODEM_COMMAND_PORT(serial_port), 0x0F);
    return 0; //serial working
}

int serial_is_transmit_fifo_empty(uint16_t com) {
    //0x20 = transmitter holding register empty
    return inb(SERIAL_LINE_STATUS_PORT(com)) & 0x20;
}

static inline int serial_is_receive_ready(uint16_t com) {
    //LSR bit 0 = data ready (at least one byte in RX buffer)
    return inb(SERIAL_LINE_STATUS_PORT(com)) & 0x01;
}

void serial_write_char(char c) {
    //wait for transmit to be ready
    while (serial_is_transmit_fifo_empty(serial_port) == 0);
    
    //send the character
    outb(SERIAL_DATA_PORT(serial_port), c);
}

void serial_write_string(const char* str) {
    if (!str) return;
    const char* p = str;
    size_t n = strlen(str);
    for (size_t i = 0; i < n; ++i) {
        serial_write_char(p[i]);
    }
    //mirror into kernel log
    klog_write(p, n);
}

void serial_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    char buffer[1024];
    int pos = 0;
    
    while (*format && pos < (int)(sizeof(buffer) - 1)) {
        if (*format == '%' && *(format + 1)) {
            format++; //skip %
            
            switch (*format) {
                case 'd': {
                    int val = va_arg(args, int);
                    char num_str[32];
                    int num_pos = 0;
                    
                    if (val < 0) {
                        buffer[pos++] = '-';
                        val = -val;
                    }
                    
                    if (val == 0) {
                        buffer[pos++] = '0';
                    } else {
                        //convert to string (reverse)
                        while (val > 0 && num_pos < 31) {
                            num_str[num_pos++] = '0' + (val % 10);
                            val /= 10;
                        }
                        //reverse and copy
                        for (int i = num_pos - 1; i >= 0 && pos < (int)(sizeof(buffer) - 1); i--) {
                            buffer[pos++] = num_str[i];
                        }
                    }
                    break;
                }
                case 'x': {
                    unsigned int val = va_arg(args, unsigned int);
                    char hex_chars[] = "0123456789abcdef";
                    char hex_str[16];
                    int hex_pos = 0;
                    
                    if (val == 0) {
                        buffer[pos++] = '0';
                    } else {
                        while (val > 0 && hex_pos < 15) {
                            hex_str[hex_pos++] = hex_chars[val % 16];
                            val /= 16;
                        }
                        for (int i = hex_pos - 1; i >= 0 && pos < (int)(sizeof(buffer) - 1); i--) {
                            buffer[pos++] = hex_str[i];
                        }
                    }
                    break;
                }
                case 's': {
                    const char* str = va_arg(args, const char*);
                    if (str) {
                        while (*str && pos < (int)(sizeof(buffer) - 1)) {
                            buffer[pos++] = *str++;
                        }
                    }
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    buffer[pos++] = c;
                    break;
                }
                case '%':
                    buffer[pos++] = '%';
                    break;
                default:
                    buffer[pos++] = '%';
                    buffer[pos++] = *format;
                    break;
            }
        } else {
            buffer[pos++] = *format;
        }
        format++;
    }
    
    buffer[pos] = '\0';
    serial_write_string(buffer);
    
    va_end(args);
}

//device manager integration for /dev/serial0
static int serial_dev_init(struct device* d) {
    (void)d; return serial_init();
}
static int serial_dev_read(struct device* d, uint32_t off, void* buf, uint32_t sz) {
    (void)d; (void)off;
    if (!buf || sz == 0) return 0;
    char* out = (char*)buf;
    uint32_t read = 0;
    //non-blocking poll read up to sz bytes available in UART RX buffer
    while (read < sz && serial_is_receive_ready(serial_port)) {
        out[read++] = (char)inb(SERIAL_DATA_PORT(serial_port));
    }
    return (int)read; //may be 0 if no data available
}
static int serial_dev_write(struct device* d, uint32_t off, const void* buf, uint32_t sz) {
    (void)d; (void)off;
    const char* p = (const char*)buf;
    for (uint32_t i = 0; i < sz; i++) serial_write_char(p[i]);
    return (int)sz;
}
static int serial_dev_ioctl(struct device* d, uint32_t cmd, void* arg) { 
    (void)d; (void)cmd; (void)arg; 
    return -1; 
}
static void serial_dev_cleanup(struct device* d) { (void)d; }

int serial_register_device(void) {
    memset(&g_serial_dev, 0, sizeof(g_serial_dev));
    strcpy(g_serial_dev.name, "serial0");
    g_serial_dev.type = DEVICE_TYPE_OUTPUT;
    g_serial_dev.subtype = DEVICE_SUBTYPE_GENERIC;
    g_serial_dev.status = DEVICE_STATUS_UNINITIALIZED;
    g_serial_dev.ops = &serial_ops;
    if (device_register(&g_serial_dev) != 0) return -1;
    if (device_init(&g_serial_dev) != 0) { 
        device_unregister(g_serial_dev.device_id); 
        return -1; 
    }
    g_serial_dev.status = DEVICE_STATUS_READY;
    return 0;
}
