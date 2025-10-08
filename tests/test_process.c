#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

static int fail(const char* msg) {
    write(STDOUT_FILENO, msg, strlen(msg));
    write(STDOUT_FILENO, "\n", 1);
    return 1;
}

int main(void) {
    int status = 0;
    pid_t child = fork();
    if (child < 0) {
        return fail("TEST process: FAIL fork1");
    }

    if (child == 0) {
        write(STDOUT_FILENO, "child running\n", sizeof("child running\n") - 1);
        _exit(42);
    }

    pid_t waited = waitpid(child, &status, 0);
    if (waited != child) {
        return fail("TEST process: FAIL waitpid");
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 42) {
        return fail("TEST process: FAIL status");
    }

    write(STDOUT_FILENO, "TEST process: PASS\n", sizeof("TEST process: PASS\n") - 1);
    return 0;
}
