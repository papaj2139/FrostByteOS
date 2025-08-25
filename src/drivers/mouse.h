#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

//initialize PS/2 mouse in streaming mode (poll-based)
void mouse_init(void);

//poll for a full 3-byte PS/2 packet returns 1 if packet ready and copies into out_bytes[3]
int mouse_poll_packet(int8_t out_bytes[3]);

#endif
