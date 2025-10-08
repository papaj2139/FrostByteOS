#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(2, "usage: write <path> <string>\n");
        return 1;
    }
    const char* path = argv[1];
    const char* text = argv[2];
    int fd = open(path, 2); //try O_RDWR
    if (fd < 0) fd = open(path, 1); //O_WRONLY
    if (fd < 0) {
        fprintf(2, "open failed\n");
        return 1;
    }
    write(fd, text, strlen(text));
    write(fd, "\n", 1);
    close(fd);
    return 0;
}
