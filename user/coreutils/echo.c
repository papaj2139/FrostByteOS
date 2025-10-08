#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv, char** envp) {
    (void)envp;
    int newline = 1;
    int i = 1;
    if (i < argc && argv[i] && strcmp(argv[i], "-n") == 0) {
        newline = 0;
        i++;
    }
    for (; i < argc; i++) {
        if (i > (newline ? 1 : 2)) fputc(1, ' ');
        if (argv[i]) fputs(1, argv[i]);
    }
    if (newline) fputc(1, '\n');
    return 0;
}
