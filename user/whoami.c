#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    printf("%u\n", (unsigned)geteuid());
    return 0;
}
