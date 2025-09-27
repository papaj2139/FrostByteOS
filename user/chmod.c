#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(2, "Usage: chmod <mode_octal> <path>\n");
        return 1;
    }
    int mode = 0;
    const char* s = argv[1];
    while (*s) {
        if (*s < '0' || *s > '7') {
            fprintf(2, "invalid mode\n");
            return 1;
        }
        mode = (mode << 3) | (*s - '0'); s++;
    }
    if (chmod(argv[2], mode) != 0) {
        fprintf(2, "chmod failed\n");
        return 1;
    }
    return 0;
}
