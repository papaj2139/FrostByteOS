#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define SIGTERM 15
#define SIGKILL 9

static void puts1(const char* s) { 
    fputs(1, s); 
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc < 2) {
        puts1("Usage: kill [-9] <pid>\n");
        return 1;
    }
    int sig = SIGTERM;
    int argi = 1;
    if (argc >= 3 && argv[1] && argv[1][0] == '-' && argv[1][1] == '9' && argv[1][2] == '\0') {
        sig = SIGKILL;
        argi = 2;
    }
    if (argi >= argc) {
        puts1("Usage: kill [-9] <pid>\n");
        return 1;
    }
    //parse pid
    const char* s = argv[argi];
    int pid = 0;
    for (; *s; ++s) { if (*s < '0' || *s > '9') { 
        puts1("kill: invalid pid\n"); 
        return 1; 
    } 
    pid = pid*10 + (*s - '0'); }
    if (kill(pid, sig) != 0) {
        puts1("kill: failed\n");
        return 1;
    }
    return 0;
}
