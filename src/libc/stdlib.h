#ifndef STDLIB_H
#define STDLIB_H

#include <stdint.h>

int parse_u8(const char* s, unsigned char* out);
int parse_u32(const char* s, uint32_t* out);

#endif
