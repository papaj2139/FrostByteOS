#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv, char** envp) {
    (void)argc;
    (void)argv;
    (void)envp;
    printf("hello from hello-dyn (dynamic)\n");
    return 0;
}
