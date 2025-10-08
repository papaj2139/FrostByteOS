#include <unistd.h>
#include <string.h>

static void puts1(const char* s) { 
    write(1, s, strlen(s)); 
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc < 2) {
        puts1("Usage: touch <file>\n");
        return 1;
    }
    int fd = creat(argv[1], 0);
    if (fd < 0) {
        puts1("touch: cannot create "); 
        puts1(argv[1]); 
        puts1("\n");
        return 1;
    }
    close(fd);
    return 0;
}
