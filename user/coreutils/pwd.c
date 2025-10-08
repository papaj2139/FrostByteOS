#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    char buf[256];
    if (!getcwd(buf, sizeof(buf))) return 1;
    printf("%s\n", buf);
    return 0;
}
