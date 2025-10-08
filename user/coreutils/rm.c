#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv) {
    int force = 0; int i = 1;
    if (i < argc && argv[i] && strcmp(argv[i], "-f") == 0) {
        force = 1;
        i++;
    }
    if (argc - i < 1) {
        fprintf(2, "Usage: rm [-f] <path>\n");
        return 1;
    }
    int r = unlink(argv[i]);
    if (r != 0 && !force) return 1;
    return 0;
}
