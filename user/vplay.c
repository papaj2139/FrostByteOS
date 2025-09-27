#include <unistd.h>
#include <string.h>
#include <stdio.h>

//simple as sfuck vga player
//file formats:
//  1.VGA13H: header magic "VGA13H" + u16 width + u16 height + u16 fps + u32 frames (LE)
//     then frames: width*height bytes per frame (palette indices)
//  2.VGA1B0: header magic "VGA1B0" + u16 width + u16 height + u16 fps + u32 frames (LE)
//     then frames: ceil(width*height/8) bytes per frame, 1 bit per pixel (0=black,1=white)
//pixels should be 0..255 for monochrome use 0 (black) and 15 (white)

static int read_n(int fd, void* buf, int n) {
    char* p = (char*)buf; int got = 0;
    while (got < n) {
        int r = read(fd, p + got, n - got);
        if (r <= 0) return r; //EOF or error
        got += r;
    }
    return got;
}

static void le16(const unsigned char* b, unsigned* out) {
    *out = (unsigned)b[0] | ((unsigned)b[1] << 8);
}

static void le32(const unsigned char* b, unsigned* out) {
    *out = (unsigned)b[0] | ((unsigned)b[1] << 8) | ((unsigned)b[2] << 16) | ((unsigned)b[3] << 24);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(2, "Usage: vplay <file.vga>\n");
        return 1;
    }
    const char* path = argv[1];
    int fd = open(path, 0);
    if (fd < 0) {
        fprintf(2, "vplay: cannot open %s\n", path);
        return 1;
    }

    unsigned char hdr[6];
    if (read_n(fd, hdr, 6) != 6) {
        fprintf(2, "vplay: short read header\n");
        close(fd);
        return 1;
    }
    int fmt_bpp1 = 0;
    if (hdr[0]=='V' && hdr[1]=='G' && hdr[2]=='A' && hdr[3]=='1' && hdr[4]=='3' && hdr[5]=='H') {
        fmt_bpp1 = 0;
    } else if (hdr[0]=='V' && hdr[1]=='G' && hdr[2]=='A' && hdr[3]=='1' && hdr[4]=='B' && hdr[5]=='0') {
        fmt_bpp1 = 1;
    } else {
        fprintf(2, "vplay: bad magic (expect VGA13H or VGA1B0)\n");
        close(fd);
        return 1;
    }
    unsigned char b2[2]; unsigned char b4[4];
    unsigned w=0,h=0,fps=0,frames=0;
    if (read_n(fd, b2, 2) != 2) {
        fprintf(2, "vplay: bad w\n");
        close(fd);
        return 1;
    }
    le16(b2, &w);
    if (read_n(fd, b2, 2) != 2) {
        fprintf(2, "vplay: bad h\n");
        close(fd);
        return 1;
    }
    le16(b2, &h);
    if (read_n(fd, b2, 2) != 2) {
        fprintf(2, "vplay: bad fps\n");
        close(fd);
        return 1;
    }
    le16(b2, &fps);
    if (read_n(fd, b4, 4) != 4) {
        fprintf(2, "vplay: bad frames\n");
        close(fd);
        return 1;
    }
    le32(b4, &frames);

    if (w == 0 || h == 0 || w > 640 || h > 480 || fps == 0) {
        fprintf(2, "vplay: unsupported header (w=%u h=%u fps=%u)\n", w, h, fps);
        close(fd); return 1;
    }

    //switch to 13h if not already (player assumes chunky 1 byte per pixel)
    int pv = open("/proc/vga", 1);
    if (pv >= 0) {
        fputs(pv, "13h");
        close(pv);
    }

    int vfd = open("/dev/vga0", 1);
    if (vfd < 0) {
        fprintf(2, "vplay: cannot open /dev/vga0\n");
        close(fd);
        return 1;
    }

    static unsigned char frame[640*480];

    //frame period
    unsigned ns_per = (fps != 0) ? (1000000000u / fps) : 33333333u;
    timespec_t ts; ts.tv_sec = (int)(ns_per / 1000000000u); ts.tv_nsec = (int)(ns_per % 1000000000u);

    unsigned long total = 0;
    for (unsigned i = 0; (frames == 0) || (i < frames); i++) {
        int need = (int)(w * h);
        if (!fmt_bpp1) {
            int r = read_n(fd, frame, need);
            if (r <= 0) break; //EOF
            if (r < need) {
                for (int k = r; k < need; k++) frame[k] = 0;
            }
        } else {
            int bits = need;
            int nbytes = (bits + 7) / 8;
            static unsigned char bitbuf[(640*480+7)/8];
            int r = read_n(fd, bitbuf, nbytes);
            if (r <= 0) break;
            if (r < nbytes) {
                for (int k = r; k < nbytes; k++) bitbuf[k] = 0;
            }
            //expand to 0/15
            int pi = 0;
            for (int b = 0; b < nbytes; b++) {
                unsigned char v = bitbuf[b];
                for (int bit = 0; bit < 8 && pi < need; bit++) {
                    frame[pi++] = (v & (1u << bit)) ? 15 : 0;
                }
            }
        }

        //write full frame driver fast-path ignores offset for full-frame writes
        int wr = write(vfd, frame, (unsigned)need);
        if (wr < 0) {
            fprintf(2, "vplay: write error\n");
            break;
        }
        total++;
        nanosleep(&ts, 0);
    }

    if (vfd >= 0) close(vfd);
    close(fd);

    //return to text mode for convenience
    pv = open("/proc/vga", 1);
    if (pv >= 0) {
        fputs(pv, "text");
        close(pv);
    }

    return 0;
}
