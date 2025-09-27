#include <unistd.h>
#include <stdio.h>
#include <string.h>

static int parse_n(const char* s) {
    int v = 0; if (!s || !*s) return -1;
    for (const char* p = s; *p; ++p) {
        if (*p < '0' || *p > '9') return -1;
        v = v*10 + (*p - '0');
    }
    return v;
}

int main(int argc, char** argv) {
    int n = 10; int ai = 1;
    if (ai < argc && argv[ai] && strcmp(argv[ai], "-n") == 0) {
        if (ai + 1 >= argc) { fprintf(2, "Usage: head [-n N] <file>\n"); return 1; }
        int t = parse_n(argv[ai+1]);
        if (t <= 0) {
            fprintf(2, "head: invalid N\n");
            return 1;
        }
        n = t; ai += 2;
    }
    if (argc - ai < 1) {
        fprintf(2, "Usage: head [-n N] <file>\n");
        return 1;
    }
    const char* path = argv[ai];
    int fd = open(path, 0);
    if (fd < 0) {
        fprintf(2, "head: cannot open %s\n", path);
        return 1;
    }
    char buf[256];
    int lines = 0;
    for (;;) {
        int r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            close(fd);
            return 1;
        }
        if (r == 0) break;
        for (int i = 0; i < r; i++) {
            fputc(1, buf[i]);
            if (buf[i] == '\n') {
                lines++;
                if (lines >= n) {
                    close(fd);
                    return 0;
                }
            }
        }
    }
    close(fd);
    return 0;
}
