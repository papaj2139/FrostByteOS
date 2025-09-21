#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

//serial port definitions
#define SERIAL_COM1_BASE    0x3F8
#define SERIAL_COM2_BASE    0x2F8
#define SERIAL_COM3_BASE    0x3E8
#define SERIAL_COM4_BASE    0x2E8

//serial port registers (offset from base)
#define SERIAL_DATA_PORT(base)          (base)
#define SERIAL_FIFO_COMMAND_PORT(base)  (base + 2)
#define SERIAL_LINE_COMMAND_PORT(base)  (base + 3)
#define SERIAL_MODEM_COMMAND_PORT(base) (base + 4)
#define SERIAL_LINE_STATUS_PORT(base)   (base + 5)

//serial config
#define SERIAL_LINE_ENABLE_DLAB         0x80

//function declarations
int serial_init(void);
int serial_is_transmit_fifo_empty(uint16_t com);
void serial_write_char(char c);
void serial_write_string(const char* str);
void serial_printf(const char* format, ...);

//register a serial dvice with device manager returns 0 on success
int serial_register_device(void);

//macros
#define DEBUG_PRINT(str) serial_write_string("[DEBUG] " str "\n")
#define DEBUG_PRINTF(fmt, ...) serial_printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)

#endif
