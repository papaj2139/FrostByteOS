#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include "../device_manager.h"

//initialize PS/2 mouse in streaming mode (poll-based)
void mouse_init(void);

//poll for a full 3-byte PS/2 packet returns 1 if packet ready and copies into out_bytes[3]
int mouse_poll_packet(int8_t out_bytes[3]);

//mouse input event for /dev/input/mouse
//type: 1=button press 0=button release 2=motion
//button: bit 0=left bit 1=right bit 2=middle (for press/release events)
//rel_x rel_y: relative motion (for motion events)
//time_ms: milliseconds since boot (approx)
typedef struct {
    uint32_t time_ms;
    int16_t rel_x;
    int16_t rel_y;
    uint8_t type;     //0=release 1=press 2=motion
    uint8_t button;   //button bits for press/release
    uint16_t reserved;
} mouse_input_event_t;

//fill buffer with up to max_events events if blocking!=0 block until at least one event
//returns number of events copied
int mouse_input_read_events(mouse_input_event_t* out, uint32_t max_events, int blocking);
int mouse_input_has_events(void);

//device manager integration
device_t* mouse_create_device(void);
int mouse_device_init(device_t* device);
int mouse_register_device(void);
int mouse_device_read(device_t* device, uint32_t offset, void* buffer, uint32_t size);
int mouse_device_write(device_t* device, uint32_t offset, const void* buffer, uint32_t size);
int mouse_device_ioctl(device_t* device, uint32_t cmd, void* arg);
void mouse_device_cleanup(device_t* device);

#endif
