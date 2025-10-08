#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

static const char* kTests[] = {
    "/bin/test_memory",
    "/bin/test_process",
    "/bin/test_ipc",
    "/bin/test_vfs",
};

static void write_str(const char* msg) {
    write(STDOUT_FILENO, msg, strlen(msg));
}

static void puts_line(const char* msg) {
    write_str(msg);
    write(STDOUT_FILENO, "\n", 1);
}

static int run_one(const char* path) {
    pid_t pid = fork();
    if (pid < 0) {
        puts_line("TEST runner: FAIL fork");
        return -1;
    }

    if (pid == 0) {
        char* const argv[] = { (char*)path, NULL };
        char* const envp[] = { NULL };
        execve(path, argv, envp);
        puts_line("TEST runner: FAIL execve");
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        puts_line("TEST runner: FAIL waitpid");
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        write_str("TEST runner: FAIL exit ");
        write_str(path);
        write(STDOUT_FILENO, "\n", 1);
        return -1;
    }

    write_str("TEST runner: PASS ");
    write_str(path);
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

int main(void) {
    size_t count = sizeof(kTests) / sizeof(kTests[0]);
    for (size_t i = 0; i < count; ++i) {
        if (run_one(kTests[i]) != 0) {
            return 1;
        }
    }
    puts_line("ALL TESTS PASS");
    return 0;
}
