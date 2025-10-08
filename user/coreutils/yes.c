#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    const char* s = (argc > 1 && argv[1]) ? argv[1] : "y";
    while (1) {
        fputs(1, s);
        fputc(1, '\n');
    }
    return 0;
}
