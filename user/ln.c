#include <unistd.h>
#include <string.h>

static void puts1(const char* s) { 
    write(1, s, strlen(s)); 
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc < 3) {
        puts1("Usage: ln [-s] <target> <linkname>\n");
        return 1;
    }
    int argi = 1;
    int soft = 0;
    if (argv[argi] && argv[argi][0] == '-' && argv[argi][1] == 's' && argv[argi][2] == '\0') {
        soft = 1; argi++;
    }
    if (argc - argi < 2) {
        puts1("Usage: ln [-s] <target> <linkname>\n");
        return 1;
    }
    const char* target = argv[argi];
    const char* linkname = argv[argi+1];
    int r = soft ? symlink(target, linkname) : link(target, linkname);
    if (r != 0) {
        puts1("ln: failed\n");
        return 1;
    }
    return 0;
}
