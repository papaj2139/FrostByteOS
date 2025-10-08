#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

static int wwrite(int fd, const char* s, int n) {
    int off = 0;
    while (off < n) {
        int w = write(fd, s + off, (size_t)(n - off));
        if (w <= 0) return -1;
        off += w;
    }
    return n;
}

int fputc(int fd, int c) {
    char ch = (char)c;
    return wwrite(fd, &ch, 1) == 1 ? (unsigned char)ch : -1;
}

int putchar(int c) {
    return fputc(1, c);
}

int fputs(int fd, const char* s) {
    if (!s) s = "(null)";
    int n = (int)strlen(s);
    return wwrite(fd, s, n) < 0 ? -1 : n;
}

int puts(const char* s) {
    if (fputs(1, s) < 0) return -1;
    return fputc(1, '\n');
}

char* fgets(int fd, char* buf, int size) {
    if (!buf || size <= 0) return NULL;
    
    int pos = 0;
    while (pos < size - 1) {
        char c;
        int r = read(fd, &c, 1);
        if (r <= 0) {
            //EOF or error
            if (pos == 0) return NULL;
            break;
        }
        
        buf[pos++] = c;
        
        //stop at newline
        if (c == '\n') {
            break;
        }
    }
    
    buf[pos] = '\0';
    return buf;
}

static int utoa_dec(unsigned v, char* buf) {
    char tmp[16]; int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (v % 10));
        v /= 10;
    }
    int n = 0;
    while (t > 0) buf[n++] = tmp[--t];
    buf[n] = 0;
    return n;
}

static int itoa_dec(int v, char* buf) {
    unsigned u = (v < 0) ? (unsigned)(-v) : (unsigned)v;
    int n = 0;
    if (v < 0) buf[n++] = '-';
    n += utoa_dec(u, buf + n);
    return n;
}

static int utoa_hex(unsigned v, char* buf, int uppercase) {
    char tmp[16]; int t = 0;
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) tmp[t++] = '0';
    while (v > 0 && t < (int)sizeof(tmp)) {
        unsigned nyb = v & 0xF;
        tmp[t++] = digits[nyb];
        v >>= 4;
    }
    int n = 0;
    while (t > 0) buf[n++] = tmp[--t];
    buf[n] = 0;
    return n;
}

static int vfprintf_inner(int fd, const char* fmt, va_list ap) {
    int written = 0;
    for (const char* p = fmt; *p; ++p) {
        if (*p != '%') {
            if (fputc(fd, *p) < 0) return -1;
            written++;
            continue;
        }
        p++;
        if (*p == '%') {
            if (fputc(fd, '%') < 0) return -1;
            written++;
            continue;
        }

        //parse flags
        int left_align = 0;
        int pad_zero = 0;
        if (*p == '-') {
            left_align = 1;
            p++;
        } else if (*p == '0') {
            pad_zero = 1;
            p++;
        }

        //parse width
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        int is_long = 0;
        if (*p == 'l') { //support %ld %lu %lx minimally (32-bit so same size)
            is_long = 1;
            p++;
        }

        char buf[32];
        int n = 0;
        switch (*p) {
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                n = (int)strlen(s);
                //handle padding
                if (!left_align) {
                    for (int i = n; i < width; i++) {
                        if (fputc(fd, ' ') < 0) return -1;
                        written++;
                    }
                }
                if (wwrite(fd, s, n) < 0) return -1;
                written += n;
                if (left_align) {
                    for (int i = n; i < width; i++) {
                        if (fputc(fd, ' ') < 0) return -1;
                        written++;
                    }
                }
            } break;
            case 'c': {
                int c = va_arg(ap, int);
                //handle padding
                if (!left_align) {
                    for (int i = 1; i < width; i++) {
                        if (fputc(fd, ' ') < 0) return -1;
                        written++;
                    }
                }
                if (fputc(fd, c) < 0) return -1;
                written++;
                if (left_align) {
                    for (int i = 1; i < width; i++) {
                        if (fputc(fd, ' ') < 0) return -1;
                        written++;
                    }
                }
            } break;
            case 'd': {
                int v = is_long ? (int)va_arg(ap, long) : va_arg(ap, int);
                n = itoa_dec(v, buf);
                //handle padding
                char pad_char = pad_zero ? '0' : ' ';
                if (!left_align) {
                    for (int i = n; i < width; i++) {
                        if (fputc(fd, pad_char) < 0) return -1;
                        written++;
                    }
                }
                if (wwrite(fd, buf, n) < 0) return -1;
                written += n;
                if (left_align) {
                    for (int i = n; i < width; i++) {
                        if (fputc(fd, ' ') < 0) return -1;
                        written++;
                    }
                }
            } break;
            case 'u': {
                unsigned v = is_long ? (unsigned)va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                n = utoa_dec(v, buf);
                //handle padding
                char pad_char = pad_zero ? '0' : ' ';
                if (!left_align) {
                    for (int i = n; i < width; i++) {
                        if (fputc(fd, pad_char) < 0) return -1;
                        written++;
                    }
                }
                if (wwrite(fd, buf, n) < 0) return -1;
                written += n;
                if (left_align) {
                    for (int i = n; i < width; i++) {
                        if (fputc(fd, ' ') < 0) return -1;
                        written++;
                    }
                }
            } break;
            case 'x':
            case 'X': {
                unsigned v = is_long ? (unsigned)va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                n = utoa_hex(v, buf, (*p == 'X' ? 1 : 0));
                //handle padding
                char pad_char = pad_zero ? '0' : ' ';
                if (!left_align) {
                    for (int i = n; i < width; i++) {
                        if (fputc(fd, pad_char) < 0) return -1;
                        written++;
                    }
                }
                if (wwrite(fd, buf, n) < 0) return -1;
                written += n;
                if (left_align) {
                    for (int i = n; i < width; i++) {
                        if (fputc(fd, ' ') < 0) return -1;
                        written++;
                    }
                }
            } break;
            case 'p': {
                //pointer format: 0xXXXXXXXX
                void* ptr = va_arg(ap, void*);
                unsigned v = (unsigned)(uintptr_t)ptr;
                if (wwrite(fd, "0x", 2) < 0) return -1;
                written += 2;
                n = utoa_hex(v, buf, 0);
                //pad to 8 hex digits for 32-bit pointers
                for (int i = n; i < 8; i++) {
                    if (fputc(fd, '0') < 0) return -1;
                    written++;
                }
                if (wwrite(fd, buf, n) < 0) return -1;
                written += n;
            } break;
            default:
                //unknown specifier: print literally
                if (fputc(fd, '%') < 0 || fputc(fd, *p) < 0) return -1;
                written += 2;
                break;
        }
    }
    return written;
}

int fprintf(int fd, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf_inner(fd, fmt, ap);
    va_end(ap);
    return r;
}

int dprintf(int fd, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf_inner(fd, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf_inner(1, fmt, ap);
    va_end(ap);
    return r;
}

static int vsnprintf_inner(char* buf, unsigned long size, const char* fmt, va_list ap) {
    if (!buf || size == 0) return 0;

    unsigned long pos = 0;
    for (const char* p = fmt; *p && pos < size - 1; ++p) {
        if (*p != '%') {
            buf[pos++] = *p;
            continue;
        }
        p++;
        if (*p == '%') {
            buf[pos++] = '%';
            continue;
        }

        //parse flags
        int left_align = 0;
        int pad_zero = 0;
        if (*p == '-') {
            left_align = 1;
            p++;
        } else if (*p == '0') {
            pad_zero = 1;
            p++;
        }
        
        //parse width
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        int is_long = 0;
        if (*p == 'l') {
            is_long = 1;
            p++;
        }

        char tmp[32];
        int n = 0;
        switch (*p) {
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                n = 0;
                while (s[n]) n++;
                if (!left_align) {
                    for (int i = n; i < width && pos < size - 1; i++) buf[pos++] = ' ';
                }
                for (int i = 0; i < n && pos < size - 1; i++) buf[pos++] = s[i];
                if (left_align) {
                    for (int i = n; i < width && pos < size - 1; i++) buf[pos++] = ' ';
                }
            } break;
            case 'c': {
                int c = va_arg(ap, int);
                if (!left_align) {
                    for (int i = 1; i < width && pos < size - 1; i++) buf[pos++] = ' ';
                }
                if (pos < size - 1) buf[pos++] = (char)c;
                if (left_align) {
                    for (int i = 1; i < width && pos < size - 1; i++) buf[pos++] = ' ';
                }
            } break;
            case 'd': {
                int v = is_long ? (int)va_arg(ap, long) : va_arg(ap, int);
                n = itoa_dec(v, tmp);
                char pad_char = pad_zero ? '0' : ' ';
                if (!left_align) {
                    for (int i = n; i < width && pos < size - 1; i++) buf[pos++] = pad_char;
                }
                for (int i = 0; i < n && pos < size - 1; i++) buf[pos++] = tmp[i];
                if (left_align) {
                    for (int i = n; i < width && pos < size - 1; i++) buf[pos++] = ' ';
                }
            } break;
            case 'u': {
                unsigned v = is_long ? (unsigned)va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                n = utoa_dec(v, tmp);
                char pad_char = pad_zero ? '0' : ' ';
                if (!left_align) {
                    for (int i = n; i < width && pos < size - 1; i++) buf[pos++] = pad_char;
                }
                for (int i = 0; i < n && pos < size - 1; i++) buf[pos++] = tmp[i];
                if (left_align) {
                    for (int i = n; i < width && pos < size - 1; i++) buf[pos++] = ' ';
                }
            } break;
            case 'x':
            case 'X': {
                unsigned v = is_long ? (unsigned)va_arg(ap, unsigned long) : va_arg(ap, unsigned);
                n = utoa_hex(v, tmp, (*p == 'X' ? 1 : 0));
                char pad_char = pad_zero ? '0' : ' ';
                if (!left_align) {
                    for (int i = n; i < width && pos < size - 1; i++) buf[pos++] = pad_char;
                }
                for (int i = 0; i < n && pos < size - 1; i++) buf[pos++] = tmp[i];
                if (left_align) {
                    for (int i = n; i < width && pos < size - 1; i++) buf[pos++] = ' ';
                }
            } break;
            case 'p': {
                //pointer format: 0xXXXXXXXX
                void* ptr = va_arg(ap, void*);
                unsigned v = (unsigned)(uintptr_t)ptr;
                if (pos < size - 1) buf[pos++] = '0';
                if (pos < size - 1) buf[pos++] = 'x';
                n = utoa_hex(v, tmp, 0);
                //pad to 8 hex digits for 32-bit pointers
                for (int i = n; i < 8 && pos < size - 1; i++) buf[pos++] = '0';
                for (int i = 0; i < n && pos < size - 1; i++) buf[pos++] = tmp[i];
            } break;
            default:
                if (pos < size - 2) {
                    buf[pos++] = '%';
                    buf[pos++] = *p;
                }
                break;
        }
    }
    buf[pos] = '\0';
    return (int)pos;
}

int snprintf(char* buf, unsigned long size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf_inner(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int vsnprintf(char* buf, unsigned long size, const char* fmt, va_list ap) {
    return vsnprintf_inner(buf, size, fmt, ap);
}
