#include <unistd.h>
#include <string.h>

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;
    const char* msg = "[init] exec /bin/forktest\n";
    write(1, msg, strlen(msg));
    char* av[] = { "/bin/forktest", 0 };
    char* ev[] = { 0 };
    execve("/bin/forktest", av, ev);
    const char* fail = "[init] execve failed\n";
    write(1, fail, strlen(fail));
    _exit(1);
    return 0;
}
