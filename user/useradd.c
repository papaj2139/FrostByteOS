#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <passwd.h>
#include <fcntl.h>
#include <stdlib.h>

static void usage(void) {
    printf("Usage: useradd [options] USERNAME\n");
    printf("Options:\n");
    printf("  -u UID        User ID\n");
    printf("  -g GID        Group ID\n");
    printf("  -d HOME       Home directory\n");
    printf("  -s SHELL      Login shell\n");
    printf("  -c COMMENT    Full name/GECOS field\n");
    printf("  -p PASSWORD   Password (will be encrypted)\n");
    _exit(1);
}

int main(int argc, char** argv) {
    if (getuid() != 0) {
        printf("useradd: must be root\n");
        return 1;
    }
    
    char* username = NULL;
    int uid = -1;
    int gid = 100; //default users group
    char* home = NULL;
    char* shell = "/bin/sh";
    char* gecos = "";
    char* password = "";
    
    //parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            uid = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            gid = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            home = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            shell = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            gecos = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            password = argv[++i];
        } else if (argv[i][0] != '-') {
            username = argv[i];
        } else {
            usage();
        }
    }
    
    if (!username) {
        printf("useradd: no username specified\n");
        usage();
    }
    
    //check if user already exists
    if (getpwnam(username)) {
        printf("useradd: user '%s' already exists\n", username);
        return 1;
    }
    
    //find next available UID if not specified
    if (uid < 0) {
        uid = 1000; //start from 1000
        struct passwd* pw;
        setpwent();
        while ((pw = getpwent()) != NULL) {
            if (pw->pw_uid >= uid) {
                uid = pw->pw_uid + 1;
            }
        }
        endpwent();
    }
    
    //default home dir
    static char default_home[128];
    if (!home) {
        snprintf(default_home, sizeof(default_home), "/home/%s", username);
        home = default_home;
    }
    
    //encrypt password
    char* encrypted = crypt_simple(password);
    
    //create passwd entry
    struct passwd new_pw;
    new_pw.pw_name = username;
    new_pw.pw_passwd = encrypted;
    new_pw.pw_uid = uid;
    new_pw.pw_gid = gid;
    new_pw.pw_gecos = gecos;
    new_pw.pw_dir = home;
    new_pw.pw_shell = shell;
    
    //append to passwd file (try writable location first)
    const char* passwd_file = PASSWD_FILE_WRITABLE;
    int fd = open(passwd_file, O_WRONLY | O_APPEND);
    if (fd < 0) {
        //try fallback
        passwd_file = PASSWD_FILE;
        fd = open(passwd_file, O_WRONLY | O_APPEND);
    }
    if (fd < 0) {
        printf("useradd: cannot open %s (not writable!)\n", passwd_file);
        printf("Hint: /etc is read-only. Use /tmp/etc/passwd or install to disk.\n");
        return 1;
    }
    
    if (putpwent(&new_pw, fd) != 0) {
        printf("useradd: failed to write entry\n");
        close(fd);
        return 1;
    }
    
    close(fd);
    
    printf("User '%s' created successfully (UID=%d, GID=%d)\n", username, uid, gid);
    printf("Home: %s\n", home);
    printf("Shell: %s\n", shell);
    
    //create home directory
    if (mkdir(home, 0755) == 0) {
        printf("Created home directory: %s\n", home);
        //TODO: chown home directory to new user
    }
    
    return 0;
}
