#include <unistd.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        const char* msg = "usage: write <path> <string>\n";
        write(2, msg, strlen(msg));
        return 1;
    }
    const char* path = argv[1];
    const char* text = argv[2];
    int fd = open(path, 2); //try O_RDWR 
    if (fd < 0) fd = open(path, 1); //O_WRONLY
    if (fd < 0) {
        const char* e = "open failed\n";
        write(2, e, strlen(e));
        return 1;
    }
    write(fd, text, strlen(text));
    write(fd, "\n", 1);
    close(fd);
    return 0;
}
