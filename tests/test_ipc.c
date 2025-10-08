#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/types.h>

static int fail(const char* msg) {
    write(STDOUT_FILENO, msg, strlen(msg));
    write(STDOUT_FILENO, "\n", 1);
    return 1;
}

static void child_fail(const char* msg) {
    write(STDOUT_FILENO, msg, strlen(msg));
    write(STDOUT_FILENO, "\n", 1);
    _exit(1);
}

int main(void) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        return fail("TEST ipc: FAIL pipe");
    }

    pid_t pid = (pid_t)fork();
    if (pid < 0) {
        return fail("TEST ipc: FAIL fork");
    }

    if (pid == 0) {
        close(pipefd[0]);
        const char pipemsg[] = "pipe-message";
        size_t pipelen = sizeof(pipemsg) - 1;
        if (write(pipefd[1], pipemsg, (int)pipelen) != (int)pipelen) {
            child_fail("TEST ipc: FAIL child write");
        }
        close(pipefd[1]);

        int out = creat("/tmp/test_ipc.out", 0644);
        if (out < 0) {
            child_fail("TEST ipc: FAIL creat");
        }
        if (dup2(out, STDOUT_FILENO) < 0) {
            child_fail("TEST ipc: FAIL dup2");
        }
        close(out);

        const char redirect[] = "redirect-ok\n";
        if (write(STDOUT_FILENO, redirect, (int)(sizeof(redirect) - 1)) != (int)(sizeof(redirect) - 1)) {
            child_fail("TEST ipc: FAIL redirect write");
        }
        _exit(0);
    }

    close(pipefd[1]);
    char buf[32];
    const size_t expect = sizeof("pipe-message") - 1;
    int r = read(pipefd[0], buf, (int)sizeof(buf));
    close(pipefd[0]);
    if (r != (int)expect) {
        return fail("TEST ipc: FAIL read size");
    }
    buf[r] = '\0';
    if (strcmp(buf, "pipe-message") != 0) {
        return fail("TEST ipc: FAIL read data");
    }

    int status = 0;
    pid_t waited = waitpid(pid, &status, 0);
    if (waited != pid) {
        return fail("TEST ipc: FAIL waitpid");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return fail("TEST ipc: FAIL child exit");
    }

    int out = open("/tmp/test_ipc.out", O_RDONLY);
    if (out < 0) {
        return fail("TEST ipc: FAIL open result");
    }
    char filebuf[32];
    int fr = read(out, filebuf, (int)sizeof(filebuf));
    close(out);
    const char expected_file[] = "redirect-ok\n";
    if (fr != (int)(sizeof(expected_file) - 1)) {
        return fail("TEST ipc: FAIL redirect size");
    }
    filebuf[fr] = '\0';
    if (strcmp(filebuf, expected_file) != 0) {
        return fail("TEST ipc: FAIL redirect data");
    }

    write(STDOUT_FILENO, "TEST ipc: PASS\n", sizeof("TEST ipc: PASS\n") - 1);
    return 0;
}
