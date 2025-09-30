#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static int parse_int(const char* s) {
    int v = 0;
    if (!s) return 0;
    while (*s >= '0' && *s <= '9') {
        v = v*10 + (*s - '0');
        s++;
    }
    return v;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        dprintf(2, "Usage: sbplay <file.raw> [rate]\n" \
                   "  file must be unsigned 8-bit PCM mono\n");
        return 1;
    }
    const char* path = argv[1];
    int rate = (argc >= 3) ? parse_int(argv[2]) : 22050;
    if (rate < 4000) rate = 4000;
    if (rate > 48000) rate = 48000;

    int afd = open("/dev/sb16", 1);
    if (afd < 0) {
        dprintf(2, "sbplay: cannot open /dev/sb16 (errno=%d)\n", errno);
        return 1;
    }
    //configure via /proc/sb16
    int pfd = open("/proc/sb16", 2);
    if (pfd >= 0) {
        char cfg[32];
        //build "rate <num>\n"
        int n = 0;
        const char* pre = "rate ";
        for (const char* q = pre; *q && n < (int)sizeof(cfg) - 1; ++q) cfg[n++] = *q;
        //write decimal
        int v = rate; if (v < 0) v = 0;
        char tmp[16]; int tp = 0;
        if (v == 0) {
            tmp[tp++] = '0';
        }
        else {
            while (v > 0 && tp < (int)sizeof(tmp)) {
                tmp[tp++] = (char)('0' + (v % 10));
                v /= 10;
            }
        }
        while (tp > 0 && n < (int)sizeof(cfg) - 1) cfg[n++] = tmp[--tp];
        if (n < (int)sizeof(cfg) - 1) cfg[n++] = '\n';
        (void)write(pfd, cfg, (size_t)n);
        (void)write(pfd, "speaker on\n", 12);
    }

    int fd = open(path, 0);
    if (fd < 0) {
        dprintf(2, "sbplay: cannot open %s\n", path);
        close(afd);
        return 1;
    }

    //stream in chunks
    static unsigned char buf[4096];
    for (;;) {
        int r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            dprintf(2, "sbplay: read error\n");
            break;
        }
        if (r == 0) break;
        int off = 0;
        while (off < r) {
            int w = write(afd, buf + off, (size_t)(r - off));
            if (w <= 0) {
                dprintf(2, "sbplay: write error\n");
                close(fd);
                close(afd);
                return 1;
            }
            off += w;
        }
    }

    if (pfd >= 0) {
        (void)write(pfd, "speaker off\n", 13);
        close(pfd);
    }
    close(fd);
    close(afd);
    return 0;
}
