#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc < 2) {
        fprintf(2, "Usage: mkdir <dir>\n");
        return 1;
    }
    if (mkdir(argv[1], 0) == 0) return 0;
    fprintf(2, "mkdir: failed to create %s\n", argv[1]);
    return 1;
}
