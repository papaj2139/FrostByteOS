#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv, char** envp) {
    (void)envp;
    const char* path = ".";
    int show_all = 0;
    int ai = 1;
    if (ai < argc && argv[ai] && strcmp(argv[ai], "-a") == 0) {
        show_all = 1;
        ai++;
    }
    if (ai < argc && argv[ai] && argv[ai][0]) path = argv[ai];

    int fd = open(path, 0);
    if (fd < 0) {
        fprintf(2, "ls: cannot open %s\n", path);
        return 1;
    }

    char name[64];
    unsigned type = 0;
    unsigned idx = 0;
    int any = 0;
    while (readdir_fd(fd, idx, name, sizeof(name), &type) == 0) {
        //skip '.' and '..' by default unless -a
        if (!show_all && name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            idx++;
            continue;
        }
        any = 1;
        fputs(1, name);
        if (type == 0x02) {
            fputc(1, '/');
        }
        fputc(1, '\n');
        idx++;
    }

    close(fd);
    return any ? 0 : 0;
}
