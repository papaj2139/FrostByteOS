#include "string.h"

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *a = s1;
    const unsigned char *b = s2;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return a[i] - b[i];
    }
    return 0;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dest;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)c;
    return s;
}

int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

unsigned int strlen(const char *str) {
    unsigned int len = 0;
    while (str[len] != '\0') len++;
    return len;
}

void strncpy(char* dest, const char* src, unsigned int n) {
    unsigned int i = 0;
    while (i < n && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    if (i < n) dest[i] = '\0';
}

void itoa_unsigned(unsigned int value, char *buf, int base) {
    char tmp[32];
    int i = 0;
    do {
        int digit = value % base;
        tmp[i++] = (digit < 10) ? '0' + digit : 'a' + (digit - 10);
        value /= base;
    } while (value > 0);
    for (int j = 0; j < i; j++)
        buf[j] = tmp[i - j - 1];
    buf[i] = '\0';
}

int ksnprintf(char *out, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    size_t pos = 0;
    while (*fmt && pos + 1 < size) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'u') {
                unsigned int num = va_arg(args, unsigned int);
                char numbuf[32];
                itoa_unsigned(num, numbuf, 10);
                for (char *p = numbuf; *p && pos + 1 < size; p++)
                    out[pos++] = *p;
            } else if (*fmt == 'x') {
                unsigned int num = va_arg(args, unsigned int);
                char numbuf[32];
                itoa_unsigned(num, numbuf, 16);
                for (char *p = numbuf; *p && pos + 1 < size; p++)
                    out[pos++] = *p;
            } else if (*fmt == 's') {
                char *str = va_arg(args, char*);
                while (*str && pos + 1 < size)
                    out[pos++] = *str++;
            } else if (*fmt == '%') {
                out[pos++] = '%';
            }
        } else {
            out[pos++] = *fmt;
        }
        fmt++;
    }
    out[pos] = '\0';
    va_end(args);
    return pos;
}
