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

char* strcpy(char* dest, const char* src) {
    char* orig_dest = dest;
    while ((*dest++ = *src++));
    return orig_dest;
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

            //handle padding (like %02x)
            int pad_width = 0;
            char pad_char = ' ';
            if (*fmt == '0') {
                pad_char = '0';
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                pad_width = pad_width * 10 + (*fmt - '0');
                fmt++;
            }

            if (*fmt == 'u') {
                unsigned int num = va_arg(args, unsigned int);
                char numbuf[32];
                itoa_unsigned(num, numbuf, 10);

                //apply padding
                int num_len = strlen(numbuf);
                while (pad_width > num_len && pos + 1 < size) {
                    out[pos++] = pad_char;
                    pad_width--;
                }

                for (char *p = numbuf; *p && pos + 1 < size; p++)
                    out[pos++] = *p;
            } else if (*fmt == 'x') {
                unsigned int num = va_arg(args, unsigned int);
                char numbuf[32];
                itoa_unsigned(num, numbuf, 16);

                //apply padding
                int num_len = strlen(numbuf);
                while (pad_width > num_len && pos + 1 < size) {
                    out[pos++] = pad_char;
                    pad_width--;
                }

                for (char *p = numbuf; *p && pos + 1 < size; p++)
                    out[pos++] = *p;
            } else if (*fmt == 's') {
                char *str = va_arg(args, char*);
                while (*str && pos + 1 < size)
                    out[pos++] = *str++;
            } else if (*fmt == 'd') {
                int num = va_arg(args, int);
                char numbuf[32];

                //handle negative numbers
                if (num < 0) {
                    if (pos + 1 < size) out[pos++] = '-';
                    num = -num;
                }

                itoa_unsigned((unsigned int)num, numbuf, 10);

                //apply padding
                int num_len = strlen(numbuf);
                while (pad_width > num_len && pos + 1 < size) {
                    out[pos++] = pad_char;
                    pad_width--;
                }

                for (char *p = numbuf; *p && pos + 1 < size; p++)
                    out[pos++] = *p;
            }

            else if (*fmt == '%') {
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

char tolower_char(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

int strncasecmp_custom(const char *a, const char *b, size_t n) {
    size_t i = 0;
    while (i < n && a[i] && b[i]) {
        char ca = tolower_char(a[i]);
        char cb = tolower_char(b[i]);
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        i++;
    }
    if (i == n) return 0;
    return (int)(unsigned char)tolower_char(a[i]) - (int)(unsigned char)tolower_char(b[i]);
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++; n++;
        }
        if (!*n) return (char*)haystack;
    }
    return 0;
}

void itoa(int n, char s[]) {
    int i, sign;
    if ((sign = n) < 0) {
        n = -n;
    }
    i = 0;
    do {
        s[i++] = (char)(n % 10 + '0');
    } while ((n /= 10) > 0);

    if (sign < 0) {
        s[i++] = '-';
    }
    s[i] = '\0';

    //reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = s[start];
        s[start] = s[end];
        s[end] = temp;
        start++;
        end--;
    }
}

char* strcat(char* dest, const char* src) {
    strcpy(dest + strlen(dest), src);
    return dest;
}

void reverse(char s[]) {
    int i, j;
    char c;
    for (i = 0, j = strlen(s)-1; i<j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

char *strchr(const char *s, int c) {
    while (*s != '\0') {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    return 0;
}

char *strrchr(const char *s, int c) {
    char *last = 0;
    while (*s != '\0') {
        if (*s == (char)c) {
            last = (char *)s;
        }
        s++;
    }
    return last;
}

int toupper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A');
    }
    return c;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *original_dest = dest;

    //find the end of dest
    while (*dest != '\0') {
        dest++;
    }

    //copy up to n characters from src
    while (n > 0 && *src != '\0') {
        *dest++ = *src++;
        n--;
    }

    //null terminate
    *dest = '\0';

    return original_dest;
}
