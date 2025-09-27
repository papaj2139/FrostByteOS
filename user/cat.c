#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv, char** envp) {
    (void)envp;
    int number = 0;
    int ai = 1;
    if (ai < argc && argv[ai] && strcmp(argv[ai], "-n") == 0) { number = 1; ai++; }
    if (argc - ai < 1) {
        fprintf(2, "Usage: cat [-n] <file>\n");
        return 1;
    }
    const char* path = argv[ai];
    int fd = open(path, 0);
    if (fd < 0) {
        fprintf(2, "cat: cannot open %s\n", path);
        return 1;
    }
    char buf[512];
    int line = 1;
    int prev_nl = 1;
    for (;;) {
        int r = read(fd, buf, sizeof(buf));
        if (r < 0) { 
            close(fd); 
            return 1; 
        }
        if (r == 0) break;
        for (int k = 0; k < r; k++) {
            if (number && prev_nl) { dprintf(1, "%d\t", line++); prev_nl = 0; }
            fputc(1, buf[k]);
            prev_nl = (buf[k] == '\n');
        }
    }
    close(fd);
    return 0;
}
