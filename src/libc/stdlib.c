#include "stdlib.h"

int parse_u32(const char* s, uint32_t* out) {
    uint32_t val = 0;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    while (*s) {
        char c = *s;
        if (base == 16 && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            val = val * 16 + (c >= 'a' ? c - 'a' + 10 : (c >= 'A' ? c - 'A' + 10 : c - '0'));
        } else if (base == 10 && c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
        } else {
            return 0; //invalid character
        }
        s++;
    }
    *out = val;
    return 1;
}

int parse_u8(const char* s, unsigned char* out){
    if(!s) return 0;
    while(*s == ' ') s++;
    if(!*s) return 0;
    unsigned int val = 0;
    if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')){
        s += 2;
        if(!*s) return 0;
        while(*s){
            char c = *s;
            unsigned int d;
            if(c >= '0' && c <= '9') d = (unsigned int)(c - '0');
            else if(c >= 'a' && c <= 'f') d = 10u + (unsigned int)(c - 'a');
            else if(c >= 'A' && c <= 'F') d = 10u + (unsigned int)(c - 'A');
            else break;
            val = (val << 4) | d;
            if(val > 255u) { val = 255u; break; }
            s++;
        }
    } else {
        while(*s >= '0' && *s <= '9'){
            val = val * 10u + (unsigned int)(*s - '0');
            if(val > 255u) { val = 255u; break; }
            s++;
        }
    }
    *out = (unsigned char)val;
    return 1;
}
