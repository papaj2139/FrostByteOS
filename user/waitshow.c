#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

static void puts1(const char* s) { 
    write(1, s, strlen(s)); 
}

static void print_dec(int v) {
    char buf[16]; int i = 0;
    if (v == 0) { buf[i++] = '0'; }
    else {
        int n = (v < 0) ? -v : v; char tmp[16]; int t = 0;
        while (n > 0 && t < (int)sizeof(tmp)) { tmp[t++] = '0' + (n % 10); n /= 10; }
        if (v < 0) buf[i++] = '-';
        while (t > 0 && i < (int)sizeof(buf)) buf[i++] = tmp[--t];
    }
    buf[i] = '\0'; write(1, buf, i);
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    if (argc < 2) {
        puts1("Usage: waitshow <program> [args...]\n");
        return 1;
    }
    //fork then exec the provided program so the parent can waitpid it
    int pid = fork();
    if (pid < 0) { puts1("fork failed\n"); return 1; }
    if (pid == 0) {
        //child: exec argv[1] with argv[1..]

        //build argv vector with path resolution (/bin prefix if no slash)
        static char path[128];
        const char* cmd = (argc > 1) ? argv[1] : (const char*)0;
        if (!cmd || !*cmd) { puts1("[waitshow-child] no command\n"); _exit(127); }
        int has_slash = 0; for (const char* t = cmd; *t; ++t) { 
            if (*t == '/') { 
                has_slash = 1; 
                break; } 
            }
        char* p = path; if (!
            has_slash && cmd[0] != '/'
        ) { 
            const char* pref = "/bin/";
            while (*pref) *p++ = *pref++; 
        }
        const char* q = cmd; while (*q) *p++ = *q++; *p = '\0';
        //shift argv by one for execve
        static char* exargv[16]; int xi = 0;
        for (int i = 1; i < argc && xi < 15; i++) exargv[xi++] = argv[i];
        exargv[xi] = 0;
        execve(path, exargv, (char* const*)0);
        puts1("exec failed\n");
        _exit(127);
    }
    //parent waitpid
    int status = 0;
    int w = waitpid(pid, &status, 0);
    if (w == pid) {
        if (WIFEXITED(status)) {
            puts1("child exit "); print_dec(WEXITSTATUS(status)); puts1("\n");
        } else if (WIFSIGNALED(status)) {
            puts1("child signaled "); print_dec(WTERMSIG(status)); puts1("\n");
        } else {
            puts1("child status "); print_dec(status); puts1("\n");
        }
        return 0;
    } else if (w == 0) {
        puts1("no status (WNOHANG)\n");
        return 0;
    } else {
        puts1("waitpid error\n");
        return 1;
    }
}
