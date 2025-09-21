#include <unistd.h>
#include <string.h>

static void puts1(const char* s) { write(1, s, strlen(s)); }
static void putc1(char c) { write(1, &c, 1); }

int main(int argc, char** argv, char** envp) {
    (void)envp;
    const char* path = "/";
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        path = argv[1];
    }

    int fd = open(path, 0);
    if (fd < 0) {
        puts1("ls: cannot open ");
        puts1(path);
        puts1("\n");
        return 1;
    }

    char name[64];
    unsigned type = 0;
    unsigned idx = 0;
    int any = 0;
    while (readdir_fd(fd, idx, name, sizeof(name), &type) == 0) {
        any = 1;
        puts1(name);
        if (type == 0x02) { //VFS_FILE_TYPE_DIRECTORY
            putc1('/');
        }
        putc1('\n');
        idx++;
    }

    close(fd);
    return any ? 0 : 0;
}
