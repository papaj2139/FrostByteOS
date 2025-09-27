#include <unistd.h>
#include <stdio.h>
#include <string.h>

static void print_line(unsigned off, const unsigned char* data, int n) {
    //offset
    dprintf(1, "%08x  ", off);
    //hex
    for (int i = 0; i < 16; i++) {
        if (i < n) dprintf(1, "%02x ", data[i]);
        else fputs(1, "   ");
        if (i == 7) fputc(1, ' ');
    }
    fputs(1, " |");
    for (int i = 0; i < n; i++) {
        unsigned char c = data[i];
        if (c < 32 || c > 126) c = '.';
        fputc(1, c);
    }
    for (int i = n; i < 16; i++) fputc(1, ' ');
    fputs(1, "|\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(2, "Usage: hd <file>\n");
        return 1;
    }
    const char* path = argv[1];
    int fd = open(path, 0);
    if (fd < 0) {
        fprintf(2, "hd: cannot open %s\n", path);
        return 1;
    }
    unsigned char buf[16];
    unsigned off = 0;
    for (;;) {
        int r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            close(fd);
            return 1;
        }
        if (r == 0) break;
        print_line(off, buf, r);
        off += (unsigned)r;
    }
    close(fd);
    return 0;
}
