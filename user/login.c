#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <passwd.h>
#include <fcntl.h>

#define ESC "\033["
#define BOLD ESC "1m"
#define RESET ESC "0m"
#define CYAN ESC "36m"
#define GREEN ESC "32m"
#define YELLOW ESC "33m"

static void print_banner(void) {
    printf("\n");
    printf(CYAN BOLD "  ___             _   ___      _       \n");
    printf(" | __| _ ___ ___ | |_| _ ) _  | |_  ___\n");
    printf(" | _| '_/ _ \\(_-< |  _| _ \\| || | ||_ /\n");
    printf(" |_||_| \\___/__/  \\__|___/ \\_, |\\__||__|\n");
    printf("                           |__/         \n" RESET);
    printf(YELLOW "    FrostByte Operating System\n" RESET);
    printf("\n");
}

static int read_password(char* buf, int maxlen) {
    //read password without echo
    unsigned int oldmode = 0;
    ioctl(0, 0x1001, &oldmode); //TTY_IOCTL_GET_MODE
    unsigned int newmode = oldmode & ~0x02; //clear TTY_MODE_ECHO
    ioctl(0, 0x1002, &newmode); //TTY_IOCTL_SET_MODE
    
    int pos = 0;
    char ch;
    while (pos < maxlen - 1) {
        int r = read(0, &ch, 1);
        if (r <= 0) break;
        if (ch == '\n' || ch == '\r') break;
        if (ch == 8 || ch == 127) { //backspace
            if (pos > 0) pos--;
            continue;
        }
        buf[pos++] = ch;
    }
    buf[pos] = '\0';
    
    //restore echo
    ioctl(0, 0x1002, &oldmode);
    printf("\n");
    return pos;
}

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;
    
    print_banner();
    
    char username[64];
    char password[128];
    
    for (;;) {
        printf(BOLD "login: " RESET);
        int n = read(0, username, sizeof(username) - 1);
        if (n <= 0) continue;
        username[n] = '\0';
        //strip newline
        for (int i = 0; i < n; i++) {
            if (username[i] == '\n' || username[i] == '\r') {
                username[i] = '\0';
                break;
            }
        }
        if (username[0] == '\0') continue;
        
        //lookup user
        struct passwd* pw = getpwnam(username);
        if (!pw) {
            printf("Login incorrect\n\n");
            continue;
        }
        
        //check if password required
        if (pw->pw_passwd && pw->pw_passwd[0] != '\0') {
            printf("Password: ");
            read_password(password, sizeof(password));
            
            if (!verify_password(password, pw->pw_passwd)) {
                printf("Login incorrect\n\n");
                continue;
            }
        }
        
        //successful login
        printf(GREEN "Welcome, %s!\n" RESET, pw->pw_gecos && pw->pw_gecos[0] ? pw->pw_gecos : pw->pw_name);
        
        //set process credentials
        if (setgid(pw->pw_gid) != 0) {
            printf("setgid failed\n");
            continue;
        }
        if (setuid(pw->pw_uid) != 0) {
            printf("setuid failed\n");
            continue;
        }
        
        //change to home directory
        if (chdir(pw->pw_dir) != 0) {
            chdir("/"); //fallback to root
        }
        
        //build environment
        static char* new_envp[16];
        static char env_home[128];
        static char env_user[128];
        static char env_shell[128];
        static char env_logname[128];
        static char env_path[256];
        
        snprintf(env_home, sizeof(env_home), "HOME=%s", pw->pw_dir);
        snprintf(env_user, sizeof(env_user), "USER=%s", pw->pw_name);
        snprintf(env_logname, sizeof(env_logname), "LOGNAME=%s", pw->pw_name);
        snprintf(env_shell, sizeof(env_shell), "SHELL=%s", pw->pw_shell);
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
        shell_argv[0] = pw->pw_shell;
        shell_argv[1] = NULL;
        
        execve(pw->pw_shell, shell_argv, new_envp);
        
        //if exec fails
        printf("Cannot execute %s\n", pw->pw_shell);
        _exit(1);
    }
    
    return 0;
}
