#include <unistd.h>
#include <string.h>
#include <stdio.h>

static void puts1(const char* s) {
    fputs(1, s);
}

int main(int argc, char** argv, char** envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    //mount core virtual filesystems
    puts1("[init] mounting devfs -> /dev\n");
    if (mount("none", "/dev", "devfs") != 0) {
        puts1("[init] mount devfs failed\n");
    }
    puts1("[init] mounting procfs -> /proc\n");
    if (mount("none", "/proc", "procfs") != 0) {
        puts1("[init] mount procfs failed\n");
    }

    //reap children and ensure a shell exists
    for (;;) {
        //launch a shell session
        int cpid = fork();
        if (cpid == 0) {
            char* sav[] = { "/bin/forkkest", 0 };
            char* sev[] = { 0 };
            execve("/bin/forktest", sav, sev);
            _exit(127);
        }

        //reap children until shell exits
        int status = 0;
        while (1) {
            int w = wait(&status);
            if (w == cpid) break; //shell finished respawn a new one
            if (w < 0) break;     //no children
        }
    }
}
