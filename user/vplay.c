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

    //prefer framebuffer when available (VESA) read fb info first
    int use_fb = 0;
    unsigned fb_w=0, fb_h=0, fb_bpp=0, fb_pitch=0;
    int vfd = -1;
    int pfb = open("/proc/fb0", 0);
    if (pfb >= 0) {
        char ibuf[128]; int r = read(pfb, ibuf, sizeof(ibuf)-1);
        if (r > 0) {
            ibuf[r] = '\0';
            //naive parse width/height/bpp/pitch lines
            unsigned vals[4] = {0,0,0,0};
            const char* keys[4] = {"width:", "height:", "bpp:", "pitch:"};
            for (int k = 0; k < 4; k++) {
                const char* p = strstr(ibuf, keys[k]);
                if (p) {
                    p += strlen(keys[k]);
                    while (*p==' ') p++;
                    unsigned v=0;
                    while (*p>='0'&&*p<='9'){
                        v=v*10+(*p-'0');
                        p++;
                    }
                    vals[k]=v;
                }
            }
            fb_w=vals[0];
            fb_h=vals[1];
            fb_bpp=vals[2];
            fb_pitch=vals[3];
            if (fb_w && fb_h && (fb_bpp==16 || fb_bpp==24 || fb_bpp==32) && fb_pitch) use_fb = 1;
        }
        close(pfb);
    }

    if (use_fb) {
        vfd = open("/dev/fb0", 1);
        if (vfd < 0) use_fb = 0;
    }

    //if using framebuffer map it for direct access else fall back to ioctl/write paths
    unsigned fb_size = fb_pitch * fb_h;
    unsigned char* fbmap = 0;
    if (use_fb) {
        void* mp = mmap_ex(0, fb_size, PROT_READ|PROT_WRITE, 0, vfd, 0);
        if (mp != (void*)-1) {
            fbmap = (unsigned char*)mp;
            //pre-clear once
            memset(fbmap, 0, fb_size);
        }
    }

    if (!use_fb) {
        //switch to 13h if not already (player assumes chunky 1 byte per pixel)
        int pv = open("/proc/vga", 1);
        if (pv >= 0) {
            write(pv, "13h", 3);
            close(pv);
        }
        vfd = open("/dev/vga0", 1);
        if (vfd < 0) {
            fprintf(2, "vplay: cannot open /dev/vga0\n");
            close(fd);
            return 1;
        }
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
            //expand to 0/255 (for framebuffer path this maps to white for VGA 13h we'll treat 255 as bright)
            int pi = 0;
            for (int b = 0; b < nbytes; b++) {
                unsigned char v = bitbuf[b];
                for (int bit = 0; bit < 8 && pi < need; bit++) {
                    frame[pi++] = (v & (1u << bit)) ? 255 : 0;
                }
            }
        }

        if (!use_fb) {
            // VGA path: write paletted bytes
            int wr = write(vfd, frame, (unsigned)need);
            if (wr < 0) { fprintf(2, "vplay: write error\n"); break; }
        } else {
            unsigned eff_w = (w < fb_w) ? w : fb_w;
            unsigned eff_h = (h < fb_h) ? h : fb_h;
            if (fbmap) {
                //direct draw into mapped FB convert gray8 to native per row
                for (unsigned y = 0; y < eff_h; y++) {
                    unsigned char* dst = fbmap + y * fb_pitch;
                    if (fb_bpp == 32) {
                        uint32_t* p32 = (uint32_t*)dst;
                        for (unsigned x = 0; x < eff_w; x++) {
                            unsigned v = frame[y*w + x];
                            uint32_t c = (v << 16) | (v << 8) | v;
                            p32[x] = c;
                        }
                    } else if (fb_bpp == 24) {
                        for (unsigned x = 0; x < eff_w; x++) {
                            unsigned v = frame[y*w + x];
                            unsigned off = x*3; dst[off+0] = v; dst[off+1] = v; dst[off+2] = v;
                        }
                    } else if (fb_bpp == 16) {
                        uint16_t* p16 = (uint16_t*)dst;
                        for (unsigned x = 0; x < eff_w; x++) {
                            unsigned v = frame[y*w + x];
                            uint16_t c = (uint16_t)(((v>>3)<<11)|((v>>2)<<5)|((v>>3)<<0));
                            p16[x] = c;
                        }
                    }
                }
            } else {
                //fallback FB ioctl blit
                struct fb_blit_args {
                    unsigned x,y,w,h,src_pitch,flags;
                    const void* src;
                } a;
                a.x = 0;
                a.y = 0;
                a.w = eff_w;
                a.h = eff_h;
                a.src_pitch = w;
                a.flags = 1;
                a.src = frame;
                int rc = ioctl(vfd, 1, &a);
                if (rc < 0) {
                    fprintf(2, "vplay: fb ioctl blit failed\n");
                    break;
                }
            }
        }
        total++;
        nanosleep(&ts, 0);
    }

    if (vfd >= 0) close(vfd);
    close(fd);

    //do not force text mode leave mode as-is

    return 0;
}
