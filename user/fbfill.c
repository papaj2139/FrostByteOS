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

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    //read framebuffer info from /proc/fb0
    int p = open("/proc/fb0", 0);
    if (p < 0) {
        fprintf(2, "fbfill: /proc/fb0 not found (procfs mounted?)\n");
        return 1;
    }
    char ibuf[256];
    int r = read(p, ibuf, sizeof(ibuf)-1);
    close(p);
    if (r <= 0) {
        fprintf(2, "fbfill: failed to read /proc/fb0\n");
        return 1;
    }
    ibuf[r] = '\0';
    if (strstr(ibuf, "unavailable")) {
        fprintf(2, "fbfill: framebuffer unavailable (boot via VESA entry)\n");
        return 1;
    }
    int w = parse_kv(ibuf, "width:");
    int h = parse_kv(ibuf, "height:");
    int bpp = parse_kv(ibuf, "bpp:");
    int pitch = parse_kv(ibuf, "pitch:");
    if (!(w && h && bpp && pitch)) {
        fprintf(2, "fbfill: invalid /proc/fb0 info\n");
        return 1;
    }
    unsigned fb_size = (unsigned)pitch * (unsigned)h;
    static unsigned char buf[1920*1080*4];
    if (fb_size > sizeof(buf)) {
        fprintf(2, "fbfill: fb too big (need %u)\n", fb_size);
        return 1;
    }

    //test pattern vertical gradient with color stripes
    for (int y = 0; y < h; y++) {
        unsigned char* row = buf + (unsigned)y * (unsigned)pitch;
        for (int x = 0; x < w; x++) {
            unsigned v = (unsigned)((y * 255) / (h ? h : 1));
            unsigned char r8 = (unsigned char)v;
            unsigned char g8 = (unsigned char)((x % 256));
            unsigned char b8 = (unsigned char)(((x / 16) % 2) ? 0xFF : 0x00);
            if (bpp == 32) {
                unsigned off = (unsigned)x * 4;
                row[off+0] = b8; row[off+1] = g8; row[off+2] = r8; row[off+3] = 0x00;
            } else if (bpp == 24) {
                unsigned off = (unsigned)x * 3;
                row[off+0] = b8; row[off+1] = g8; row[off+2] = r8;
            } else if (bpp == 16) {
                unsigned short R = (unsigned short)(r8 >> 3);
                unsigned short G = (unsigned short)(g8 >> 2);
                unsigned short B = (unsigned short)(b8 >> 3);
                unsigned short pix = (unsigned short)((R<<11) | (G<<5) | (B));
                ((unsigned short*)row)[x] = pix;
            } else {
                //unsupported format - just zero
                break;
            }
        }
    }

    int fd = open("/dev/fb0", 1);
    if (fd < 0) {
        fprintf(2, "fbfill: open /dev/fb0 failed (devfs mounted? fb0 registered?)\n");
        return 1;
    }
    int wr = write(fd, buf, fb_size);
    close(fd);
    if (wr < 0) {
        fprintf(2, "fbfill: write failed\n");
        return 1;
    }
    return 0;
}
