#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <tty.h>
#include <sys/wait.h>

static void print(const char* s) {
    fputs(1, s);
}

static void print_dec(int v) {
    printf("%d", v);
}

static void chomp(char* b, int n) {
    if (n <= 0) return;
    int i = n - 1;
    //strip trailing \n and/or \r
    while (i >= 0 && (b[i] == '\n' || b[i] == '\r')) {
        b[i] = 0;
        i--;
    }
}

static int split_args(char* s, char** argv, int max)
{
    int argc = 0;
    //skip leading whitespace
    while (*s && *s <= ' ') s++;
    while (*s && argc < max - 1) {
        argv[argc++] = s;
        //find end of token (stop at any whitespace)
        while (*s && *s > ' ') s++;
        if (!*s) break;
        *s++ = '\0';
        while (*s && *s <= ' ') s++;
    }
    argv[argc] = 0;
    return argc;
}

static int run_command(char** prog_argv, char** envp)
{
    if (!prog_argv || !prog_argv[0]) return -1;
    const char* cmd = prog_argv[0];
    static char path[128];
    //build path safely without relying on strlen/strchr
    path[0] = '\0';
    if (cmd && cmd[0]) {
        int has_slash = 0;
        for (const char* t = cmd; *t; ++t) {
            if (*t == '/') {
                has_slash = 1;
                break;
            }
        }
        char* p = path;
        if (!has_slash && cmd[0] != '/') {
            const char* pref = "/bin/";
            for (const char* q = pref; *q && (p - path) < (int)sizeof(path) - 1; ++q) {
                *p++ = *q;
            }
        }
        for (const char* q = cmd; *q && (p - path) < (int)sizeof(path) - 1; ++q) {
            *p++ = *q;
        }
        *p = '\0';
    }
    if (path[0] == '\0') {
        print("invalid command\n");
        return -1;
    }

    //ake a stable snapshot of argv so we don't depend on caller stack buffer
    static char* argv_copy[16];
    static char argv_buf[256];
    int copy_ac = 0;
    char* wp = argv_buf; int left = (int)sizeof(argv_buf);
    for (int i = 0; prog_argv[i] && i < 16; i++) {
        const char* src = prog_argv[i];
        int len = 0; while (src[len] && len < left - 1) len++;
        if (left <= 1) break;
        //copy and advance
        for (int k = 0; k < len; k++) wp[k] = src[k];
        wp[len] = '\0';
        argv_copy[i] = wp;
        wp += len + 1; left -= (len + 1);
        copy_ac++;
    }
    if (copy_ac < 16) argv_copy[copy_ac] = 0; else argv_copy[15] = 0;

    //ready to execute resolved path

    int pid = fork();
    if (pid < 0) {
        print("fork failed\n");
        return -1;
    }
    if (pid == 0) {
        //child: build a stable argv vector: argv[0] = path then user args from snapshot argv_copy[1..]
        static char* exargv[16];
        int xi = 0;
        exargv[xi++] = (char*)path;
        for (int j = 1; argv_copy[j] && xi < 15; j++) {
            exargv[xi++] = argv_copy[j];
        }
        exargv[xi] = 0;
        execve(path, exargv, envp);
        print("exec failed: ");
        print(path);
        print("\n");
        _exit(127);
    }
    //parent wait
    int status = 0;
    wait(&status);
    //decode and print child status
    if (WIFEXITED(status)) {
        print("[status] exit ");
        print_dec(WEXITSTATUS(status));
        print("\n");
    } else if (WIFSIGNALED(status)) {
        print("[status] signaled ");
        print_dec(WTERMSIG(status));
        print("\n");
    } else {
        print("[status] unknown ");
        print_dec(status);
        print("\n");
    }
    return status;
}

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv;
    print("FrostByte Shell\n");
    char buf[256];
    for (;;) {
        char cwd[256];
        cwd[0] = 0;
        if (getcwd(cwd, sizeof(cwd))) {
            print(cwd);
            print("> ");
        } else {
            print("fbsh> ");
        }
        int n = read(0, buf, sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = 0;
        chomp(buf, n);
        //skip leading whitespace
        char* s = buf;
        while (*s && *s <= ' ') s++;
        if (*s == 0) continue;
        if (strncmp(s, "exit", 4) == 0) {
            _exit(0);
        }
        if (strncmp(s, "stty ", 5) == 0) {
            char* arg = s + 5;
            while (*arg == ' ') arg++;
            if (strcmp(arg, "raw") == 0) {
                unsigned int mode = 0;
                ioctl(0, TTY_IOCTL_SET_MODE, &mode);
                print("[stty] raw mode (no echo)\n");
            } else if (strcmp(arg, "canon") == 0) {
                unsigned int mode = TTY_MODE_CANON | TTY_MODE_ECHO;
                ioctl(0, TTY_IOCTL_SET_MODE, &mode);
                print("[stty] canonical mode with echo\n");
            } else if (strcmp(arg, "echo on") == 0) {
                unsigned int mode = 0;
                ioctl(0, TTY_IOCTL_GET_MODE, &mode);
                mode |= TTY_MODE_ECHO;
                ioctl(0, TTY_IOCTL_SET_MODE, &mode);
                print("[stty] echo on\n");
            } else if (strcmp(arg, "echo off") == 0) {
                unsigned int mode = 0;
                ioctl(0, TTY_IOCTL_GET_MODE, &mode);
                mode &= ~TTY_MODE_ECHO;
                ioctl(0, TTY_IOCTL_SET_MODE, &mode);
                print("[stty] echo off\n");
            } else {
                print("Usage: stty raw|canon|echo on|echo off\n");
            }
            continue;
        }

        //tokenize
        char* av[16];
        int ac = split_args(s, av, 16);
        if (ac <= 0) continue;

        if (strcmp(av[0], "pwd") == 0) {
            char cwdbuf[256];
            if (getcwd(cwdbuf, sizeof(cwdbuf))) {
                print(cwdbuf); print("\n");
            } else {
                print("/\n");
            }
            continue;
        }
        if (strcmp(av[0], "cd") == 0) {
            const char* target = (ac > 1) ? av[1] : "/";
            if (chdir(target) != 0) {
                print("cd: failed\n");
            }
            continue;
        }

        //try to execute external command
        run_command(av, envp);
    }
}
