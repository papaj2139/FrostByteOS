#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <passwd.h>
#include <stdlib.h>

static void usage(void) {
    printf("Usage: getent database [key]\n");
    printf("Databases: passwd, group\n");
    _exit(1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
    }
    
    const char* database = argv[1];
    const char* key = argc > 2 ? argv[2] : NULL;
    
    if (strcmp(database, "passwd") == 0) {
        if (key) {
            //lookup specific user
            struct passwd* pw = NULL;
            //try as name first
            pw = getpwnam(key);
            if (!pw) {
                //try as UID
                int uid = atoi(key);
                pw = getpwuid(uid);
            }
            if (pw) {
                printf("%s:%s:%d:%d:%s:%s:%s\n",
                       pw->pw_name, pw->pw_passwd, pw->pw_uid, pw->pw_gid,
                       pw->pw_gecos, pw->pw_dir, pw->pw_shell);
            } else {
                return 2;
            }
        } else {
            //list all users
            struct passwd* pw;
            setpwent();
            while ((pw = getpwent()) != NULL) {
                printf("%s:%s:%d:%d:%s:%s:%s\n",
                       pw->pw_name, pw->pw_passwd, pw->pw_uid, pw->pw_gid,
                       pw->pw_gecos, pw->pw_dir, pw->pw_shell);
            }
            endpwent();
        }
    } else if (strcmp(database, "group") == 0) {
        if (key) {
            //lookup specific group
            struct group* gr = NULL;
            gr = getgrnam(key);
            if (!gr) {
                int gid = atoi(key);
                gr = getgrgid(gid);
            }
            if (gr) {
                printf("%s:%s:%d:\n", gr->gr_name, gr->gr_passwd, gr->gr_gid);
            } else {
                return 2;
            }
        } else {
            //list all groups
            struct group* gr;
            setgrent();
            while ((gr = getgrent()) != NULL) {
                printf("%s:%s:%d:\n", gr->gr_name, gr->gr_passwd, gr->gr_gid);
            }
            endgrent();
        }
    } else {
        printf("getent: unknown database '%s'\n", database);
        usage();
    }
    
    return 0;
}
