#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <passwd.h>
#include <stdlib.h>

static int read_password(const char* prompt, char* buf, int maxlen) {
    printf("%s", prompt);
    unsigned int oldmode = 0;
    ioctl(0, 0x1001, &oldmode);
    unsigned int newmode = oldmode & ~0x02;
    ioctl(0, 0x1002, &newmode);
    
    int pos = 0;
    char ch;
    while (pos < maxlen - 1) {
        int r = read(0, &ch, 1);
        if (r <= 0) break;
        if (ch == '\n' || ch == '\r') break;
        if (ch == 8 || ch == 127) {
            if (pos > 0) pos--;
            continue;
        }
        buf[pos++] = ch;
    }
    buf[pos] = '\0';
    
    ioctl(0, 0x1002, &oldmode);
    printf("\n");
    return pos;
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    char* target_user = "root";
    int current_uid = getuid();
    
    if (argc > 1) {
        target_user = argv[1];
    }
    
    struct passwd* target_pw = getpwnam(target_user);
    if (!target_pw) {
        printf("su: user '%s' does not exist\n", target_user);
        return 1;
    }
    
    //root doesn't need password
    if (current_uid != 0) {
        if (target_pw->pw_passwd && target_pw->pw_passwd[0]) {
            char password[128];
            read_password("Password: ", password, sizeof(password));
            
            if (!verify_password(password, target_pw->pw_passwd)) {
                printf("su: authentication failure\n");
                return 1;
            }
        }
    }
    
    //switch user
    if (setgid(target_pw->pw_gid) != 0) {
        printf("su: setgid failed\n");
        return 1;
    }
    if (setuid(target_pw->pw_uid) != 0) {
        printf("su: setuid failed\n");
        return 1;
    }
    
    //change directory
    if (chdir(target_pw->pw_dir) != 0) {
        chdir("/");
    }
    
    //build environment
    static char* new_envp[16];
    static char env_home[128];
    static char env_user[128];
    static char env_shell[128];
    static char env_logname[128];
    static char env_path[256];
    
    snprintf(env_home, sizeof(env_home), "HOME=%s", target_pw->pw_dir);
    snprintf(env_user, sizeof(env_user), "USER=%s", target_pw->pw_name);
    snprintf(env_logname, sizeof(env_logname), "LOGNAME=%s", target_pw->pw_name);
    snprintf(env_shell, sizeof(env_shell), "SHELL=%s", target_pw->pw_shell);
    snprintf(env_path, sizeof(env_path), "PATH=/bin:/usr/bin");
    
    int env_idx = 0;
    new_envp[env_idx++] = env_home;
    new_envp[env_idx++] = env_user;
    new_envp[env_idx++] = env_logname;
    new_envp[env_idx++] = env_shell;
    new_envp[env_idx++] = env_path;
    new_envp[env_idx] = NULL;
    
    //exec shell
    char* shell_argv[2];
    shell_argv[0] = target_pw->pw_shell;
    shell_argv[1] = NULL;
    
    execve(target_pw->pw_shell, shell_argv, new_envp);
    
    printf("su: cannot execute %s\n", target_pw->pw_shell);
    return 1;
}
