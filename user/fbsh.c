#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <tty.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <passwd.h>

#define ESC "\033["
#define RESET ESC "0m"
#define BOLD ESC "1m"
#define GREEN ESC "32m"
#define BLUE ESC "34m"
#define CYAN ESC "36m"
#define RED ESC "31m"

static void print(const char* s) {
    fputs(1, s);
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

//parse a command line handling >, <, | operators
//returns number of tokens modifies s in place
//sets redirect_in redirect_out if found returns pipe position or -1
static int parse_command(char* s, char** argv, int max, char** redirect_in, char** redirect_out, int* append_mode) {
    int argc = 0;
    *redirect_in = NULL;
    *redirect_out = NULL;
    *append_mode = 0;

    while (*s && *s <= ' ') s++;

    while (*s && argc < max - 1) {
        //check for redirection operators
        if (*s == '>') {
            *s++ = '\0';
            if (*s == '>') {
                *append_mode = 1;
                s++;
            }
            while (*s && *s <= ' ') s++;
            *redirect_out = s;
            //find end of filename
            while (*s && *s > ' ' && *s != '>' && *s != '<' && *s != '|') s++;
            if (*s) *s++ = '\0';
            continue;
        }
        if (*s == '<') {
            *s++ = '\0';
            while (*s && *s <= ' ') s++;
            *redirect_in = s;
            while (*s && *s > ' ' && *s != '>' && *s != '<' && *s != '|') s++;
            if (*s) *s++ = '\0';
            continue;
        }
        if (*s == '|') {
            //pipe found - don't consume it just return
            argv[argc] = NULL;
            return argc;
        }

        //regular token
        argv[argc++] = s;
        while (*s && *s > ' ' && *s != '>' && *s != '<' && *s != '|') s++;
        if (!*s) break;
        if (*s == '>' || *s == '<' || *s == '|') continue;
        *s++ = '\0';
        while (*s && *s <= ' ') s++;
    }
    argv[argc] = NULL;
    return argc;
}

//build full path from command name
static void build_path(const char* cmd, char* path, int pathsize) {
    path[0] = '\0';
    if (!cmd || !cmd[0]) return;

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
        for (const char* q = pref; *q && (p - path) < pathsize - 1; ++q) {
            *p++ = *q;
        }
    }
    for (const char* q = cmd; *q && (p - path) < pathsize - 1; ++q) {
        *p++ = *q;
    }
    *p = '\0';
}

//execute command with optional redirection
static int exec_simple_command(char** argv, char** envp, char* redir_in, char* redir_out, int append) {
    (void)append;
    if (!argv || !argv[0]) return -1;

    char path[128];
    build_path(argv[0], path, sizeof(path));
    if (path[0] == '\0') {
        print("invalid command\n");
        return -1;
    }

    int pid = fork();
    if (pid < 0) {
        print("fork failed\n");
        return -1;
    }

    if (pid == 0) {
        //child handle redirections
        if (redir_in) {
            int fd = open(redir_in, 0); //O_RDONLY
            if (fd < 0) {
                print("Cannot open input file: ");
                print(redir_in);
                print("\n");
                _exit(1);
            }
            dup2(fd, 0);
            close(fd);
        }

        if (redir_out) {
            //try to open for writing first (for procfs and other special files)
            int fd = open(redir_out, 1); //O_WRONLY
            if (fd < 0) {
                //if open fails try create (for regular files)
                unlink(redir_out); //delete existing file for truncate
                fd = creat(redir_out, 0644);
            }
            if (fd < 0) {
                print("Cannot open/create output file: ");
                print(redir_out);
                print("\n");
                _exit(1);
            }
            dup2(fd, 1);
            close(fd);
        }

        //exec
        argv[0] = path;
        execve(path, argv, envp);
        print("exec failed: ");
        print(path);
        print("\n");
        _exit(127);
    }

    //parent wait (no status output for redirected commands)
    int status = 0;
    wait(&status);
    return status;
}

//execute pipeline: cmd1 | cmd2 | cmd3 ...
static int run_pipeline(char* cmdline, char** envp) {
    //count pipes
    int pipe_count = 0;
    for (char* p = cmdline; *p; p++) {
        if (*p == '|') pipe_count++;
    }

    if (pipe_count == 0) {
        //no pipes simple command with possible redirection
        char* argv[16];
        char* redir_in = NULL;
        char* redir_out = NULL;
        int append = 0;
        int argc = parse_command(cmdline, argv, 16, &redir_in, &redir_out, &append);
        if (argc <= 0) return -1;
        return exec_simple_command(argv, envp, redir_in, redir_out, append);
    }

    //pipeline: split by |
    char* commands[8];
    int cmd_count = 0;
    commands[cmd_count++] = cmdline;

    for (char* p = cmdline; *p && cmd_count < 8; p++) {
        if (*p == '|') {
            *p = '\0';
            p++;
            while (*p && *p <= ' ') p++;
            if (*p) commands[cmd_count++] = p;
            p--; //will be incremented by loop
        }
    }

    int pipefd[2];
    int prev_read = -1;

    for (int i = 0; i < cmd_count; i++) {
        //parse this command
        char* argv[16];
        char* redir_in = NULL;
        char* redir_out = NULL;
        int append = 0;
        int argc = parse_command(commands[i], argv, 16, &redir_in, &redir_out, &append);
        if (argc <= 0) continue;

        //create pipe for all but last command
        int need_pipe = (i < cmd_count - 1);
        if (need_pipe) {
            if (pipe(pipefd) != 0) {
                print("pipe() failed\n");
                return -1;
            }
        }

        int pid = fork();
        if (pid < 0) {
            print("fork failed\n");
            return -1;
        }

        if (pid == 0) {
            //child
            //redirect input from previous pipe
            if (prev_read != -1) {
                dup2(prev_read, 0);
                close(prev_read);
            } else if (redir_in) {
                int fd = open(redir_in, 0);
                if (fd >= 0) {
                    dup2(fd, 0);
                    close(fd);
                }
            }

            //redirect output to next pipe or file
            if (need_pipe) {
                close(pipefd[0]); //close read end
                dup2(pipefd[1], 1);
                close(pipefd[1]);
            } else if (redir_out) {
                //try to open for writing first (for procfs and other special files)
                int fd = open(redir_out, 1); //O_WRONLY
                if (fd < 0) {
                    //if open fails try create (for regular files)
                    if (!append) unlink(redir_out); //delete existing for truncate
                    fd = creat(redir_out, 0644);
                }
                if (fd >= 0) {
                    dup2(fd, 1);
                    close(fd);
                }
            }

            //exec
            char path[128];
            build_path(argv[0], path, sizeof(path));
            argv[0] = path;
            execve(path, argv, envp);
            _exit(127);
        }

        //parent
        if (prev_read != -1) close(prev_read);
        if (need_pipe) {
            close(pipefd[1]); //close write end
            prev_read = pipefd[0]; //save read end for next command
        }
    }

    //wait for all children
    for (int i = 0; i < cmd_count; i++) {
        wait(NULL);
    }

    return 0;
}

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv;
    print("FrostByte Shell\n");
    char buf[256];
    for (;;) {
        //get username and prompt symbol
        int uid = getuid();
        struct passwd* pw = getpwuid(uid);
        const char* username = pw ? pw->pw_name : "?";
        const char* color = (uid == 0) ? RED : GREEN;
        const char* prompt_char = (uid == 0) ? "#" : "$";
        
        char cwd[256];
        cwd[0] = 0;
        if (getcwd(cwd, sizeof(cwd))) {
            //show user@host:cwd$ format
            print(color);
            print(BOLD);
            print(username);
            print(RESET);
            print(":");
            print(BLUE);
            print(cwd);
            print(RESET);
            print(prompt_char);
            print(" ");
        } else {
            print(color);
            print(BOLD);
            print(username);
            print(RESET);
            print(prompt_char);
            print(" ");
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

        //check for built-in commands first
        char* first_word = s;
        while (*first_word && *first_word <= ' ') first_word++;
        char* space = first_word;
        while (*space && *space > ' ') space++;
        int cmd_len = space - first_word;

        if (cmd_len == 3 && strncmp(first_word, "pwd", 3) == 0) {
            char cwdbuf[256];
            if (getcwd(cwdbuf, sizeof(cwdbuf))) {
                print(cwdbuf); print("\n");
            } else {
                print("/\n");
            }
            continue;
        }

        if (cmd_len == 2 && strncmp(first_word, "cd", 2) == 0) {
            char* target = space;
            while (*target && *target <= ' ') target++;
            if (*target == 0) target = "/";
            //null terminate target
            char* end = target;
            while (*end && *end > ' ') end++;
            *end = '\0';
            if (chdir(target) != 0) {
                print("cd: failed\n");
            }
            continue;
        }

        //execute command with pipe/redirect support
        run_pipeline(s, envp);
        
        //invalidate passwd cache so prompt updates correctly
        endpwent();
    }
}
