#include <signal.h>
#include <unistd.h>

int raise(int sig) {
    int pid = getpid();
    return kill(pid, sig);
}
