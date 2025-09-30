#include <unistd.h>
#include <stdio.h>
#include <string.h>

static int parse_kv(const char* buf, const char* key) {
    const char* p = strstr(buf, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ') p++;
    int v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    return v;
}

int main(void) {
    //preferred ask kernel console to clear and home cursor
    int pc = open("/proc/console", 1);
    if (pc >= 0) {
        int wr = write(pc, "clear", 5);
        close(pc);
        if (wr > 0) return 0;
        //falls through to framebuffer clear on failure
    }
    //fallback to framebuffer clear
    int p = open("/proc/fb0", 0);
    if (p >= 0) {
        char ibuf[256];
        int r = read(p, ibuf, sizeof(ibuf)-1);
        close(p);
        if (r > 0) {
            ibuf[r] = '\0';
            if (!strstr(ibuf, "unavailable")) {
                int w = parse_kv(ibuf, "width:");
                int h = parse_kv(ibuf, "height:");
                int pitch = parse_kv(ibuf, "pitch:");
                if (w && h && pitch) {
                    unsigned fb_size = (unsigned)pitch * (unsigned)h;
                    int fd = open("/dev/fb0", 1);
                    if (fd >= 0) {
                        static unsigned char z[65536];
                        memset(z, 0, sizeof(z));
                        unsigned left = fb_size;
                        while (left > 0) {
                            unsigned chunk = left < sizeof(z) ? left : (unsigned)sizeof(z);
                            int wr = write(fd, z, (int)chunk);
                            if (wr <= 0) break;
                            left -= (unsigned)wr;
                        }
                        close(fd);
                        return 0;
                    }
                }
            }
        }
    }
    //last resort scroll text TTY by printing newlines
    for (int i = 0; i < 40; i++) {
        write(1, "\n", 1);
    }
    return 0;
}
