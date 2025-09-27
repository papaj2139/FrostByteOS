#include <unistd.h>
#include <string.h>
#include <stdio.h>

static int read_file(const char* path, char* buf, int bufsz) {
    int fd = open(path, 0);
    if (fd < 0) return -1;
    int off = 0;
    for (;;) {
        int r = read(fd, buf + off, bufsz - 1 - off);
        if (r <= 0) break;
        off += r;
        if (off >= bufsz - 1) break;
    }
    close(fd);
    buf[off] = '\0';
    return off;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    char buf[64];
    if (read_file("/proc/uptime", buf, sizeof(buf)) < 0) {
        fprintf(2, "uptime: cannot read /proc/uptime\n");
        return 1;
    }
    //passthrough
    fputs(1, buf);
    return 0;
}
