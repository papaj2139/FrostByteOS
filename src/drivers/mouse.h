#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include "../device_manager.h"

//initialize PS/2 mouse in streaming mode (poll-based)
void mouse_init(void);

//poll for a full 3-byte PS/2 packet returns 1 if packet ready and copies into out_bytes[3]
int mouse_poll_packet(int8_t out_bytes[3]);

//device manager integration
device_t* mouse_create_device(void);
int mouse_device_init(device_t* device);
int mouse_register_device(void);
int mouse_device_read(device_t* device, uint32_t offset, void* buffer, uint32_t size);
int mouse_device_write(device_t* device, uint32_t offset, const void* buffer, uint32_t size);
int mouse_device_ioctl(device_t* device, uint32_t cmd, void* arg);
void mouse_device_cleanup(device_t* device);


#endif