#include <unistd.h>
#include <stdio.h>

static int is_space(char c) {
    return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v';
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(2, "Usage: wc <file>\n");
        return 1;
    }
    const char* path = argv[1];
    int fd = open(path, 0);
    if (fd < 0) {
        fprintf(2, "wc: cannot open %s\n", path);
        return 1;
    }
    char buf[256];
    unsigned long lines=0, words=0, bytes=0;
    int in_word = 0;
    for (;;) {
        int r = read(fd, buf, sizeof(buf));
        if (r < 0) {
            close(fd);
            return 1;
        }
        if (r == 0) break;
        bytes += (unsigned long)r;
        for (int i = 0; i < r; i++) {
            char c = buf[i];
            if (c == '\n') lines++;
            if (is_space(c)) {
                if (in_word) in_word = 0;
            }
            else {
                if (!in_word) {
                    words++;
                    in_word = 1;
                }
            }
        }
    }
    close(fd);
    printf("%lu %lu %lu %s\n", lines, words, bytes, path);
    return 0;
}
