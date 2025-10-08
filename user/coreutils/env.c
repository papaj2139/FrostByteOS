#include <unistd.h>
#include <stdio.h>

int main(int argc, char** argv, char** envp) {
    (void)argc; 
    (void)argv;
    if (!envp) return 0;
    for (int i = 0; envp[i]; i++) {
        printf("%s\n", envp[i]);
    }
    return 0;
}
