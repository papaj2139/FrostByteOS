#include <unistd.h>
#include <string.h>

static void puts1(const char* s) { write(1, s, strlen(s)); }

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc < 2) {
        puts1("Usage: cat <file>\n");
        return 1;
    }
    int fd = open(argv[1], 0);
    if (fd < 0) {
        puts1("cat: cannot open "); 
        puts1(argv[1]); 
        puts1("\n");
        return 1;
    }
    char buf[512];
    for (;;) {
        int r = read(fd, buf, sizeof(buf));
        if (r < 0) { 
            close(fd); 
            return 1; 
        }
        if (r == 0) break;
        write(1, buf, (size_t)r);
    }
    close(fd);
    return 0;
}
