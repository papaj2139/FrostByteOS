#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    printf("uid=%u euid=%u gid=%u egid=%u\n", (unsigned)getuid(), (unsigned)geteuid(), (unsigned)getgid(), (unsigned)getegid());
    return 0;
}
