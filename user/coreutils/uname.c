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
    char ver[128];
    if (read_file("/proc/version", ver, sizeof(ver)) < 0) {
        printf("FrostByteOS\n");
        return 0;
    }
    //tokens: e.x "FrostByteOS version 0.0.5 (DATE TIME)"
    char name[32] = {0};
    char rel[32] = {0};
    //parse first token as name
    int i = 0, j = 0;
    while (ver[i] && ver[i] <= ' ') i++;
    while (ver[i] && ver[i] > ' ' && j < (int)sizeof(name)-1) name[j++] = ver[i++];
    name[j] = '\0';
    //find "version" then the next token as release
    while (ver[i] && (ver[i] == ' ' || ver[i] == '\t')) i++;
    int saw_version = 0;
    while (ver[i]) {
        //skip spaces
        while (ver[i] == ' ' || ver[i] == '\t') i++;
        if (!ver[i]) break;
        const char* tok = &ver[i];
        while (ver[i] && ver[i] > ' ') i++;
        int len = (int)(&ver[i] - tok);
        if (!saw_version && len == 7 && strncmp(tok, "version", 7) == 0) {
            saw_version = 1;
            continue;
        }
        if (saw_version && rel[0] == '\0') {
            int k = 0;
            while (k < len && k < (int)sizeof(rel)-1) {
                rel[k] = tok[k];
                k++;
            }
            rel[k] = '\0';
            break;
        }
    }

    //support: uname [-s|-r|-a]
    int print_s = 1, print_r = 0;
    if (argc > 1 && argv[1]) {
        if (strcmp(argv[1], "-s") == 0) { print_s = 1; print_r = 0; }
        else if (strcmp(argv[1], "-r") == 0) {
            print_s = 0;
            print_r = 1;
        }
        else if (strcmp(argv[1], "-a") == 0) {
            print_s = 1;
            print_r = 1;
        }
    }
    if (print_s) {
        printf("%s", name[0] ? name : "FrostByteOS");
        if (print_r) printf(" "); else printf("\n");
    }
    if (print_r) {
        printf("%s\n", rel[0] ? rel : "0.0.0");
    }
    return 0;
}
