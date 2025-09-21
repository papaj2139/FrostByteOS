#include <unistd.h>
#include <string.h>

static void puts1(const char* s) { 
    write(1, s, strlen(s));
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc < 2) {
        puts1("Usage: mkdir <dir>\n");
        return 1;
    }
    if (mkdir(argv[1], 0) == 0) return 0;
    puts1("mkdir: failed to create "); 
    puts1(argv[1]); 
    puts1("\n");
    return 1;
}
