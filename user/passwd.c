#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <passwd.h>
#include <fcntl.h>
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

int main(int argc, char** argv) {
    char* target_user = NULL;
    int uid = getuid();
    
    if (argc > 1) {
        target_user = argv[1];
    } else {
        struct passwd* pw = getpwuid(uid);
        if (!pw) {
            printf("passwd: cannot determine username\n");
            return 1;
        }
        target_user = pw->pw_name;
    }
    
    struct passwd* target_pw = getpwnam(target_user);
    if (!target_pw) {
        printf("passwd: user '%s' does not exist\n", target_user);
        return 1;
    }
    
    //make a copy of target user info since getpwnam returns static buffer
    struct passwd target_copy;
    char name_copy[128], gecos_copy[128], dir_copy[128], shell_copy[128];
    strncpy(name_copy, target_pw->pw_name, sizeof(name_copy)-1);
    name_copy[sizeof(name_copy)-1] = '\0';
    strncpy(gecos_copy, target_pw->pw_gecos ? target_pw->pw_gecos : "", sizeof(gecos_copy)-1);
    gecos_copy[sizeof(gecos_copy)-1] = '\0';
    strncpy(dir_copy, target_pw->pw_dir ? target_pw->pw_dir : "/", sizeof(dir_copy)-1);
    dir_copy[sizeof(dir_copy)-1] = '\0';
    strncpy(shell_copy, target_pw->pw_shell ? target_pw->pw_shell : "/bin/sh", sizeof(shell_copy)-1);
    shell_copy[sizeof(shell_copy)-1] = '\0';
    
    target_copy.pw_name = name_copy;
    target_copy.pw_uid = target_pw->pw_uid;
    target_copy.pw_gid = target_pw->pw_gid;
    target_copy.pw_gecos = gecos_copy;
    target_copy.pw_dir = dir_copy;
    target_copy.pw_shell = shell_copy;
    char* old_passwd = target_pw->pw_passwd ? target_pw->pw_passwd : "";
    
    //check permissions
    if (uid != 0 && uid != target_copy.pw_uid) {
        printf("passwd: you may not view or modify password information for %s\n", target_user);
        return 1;
    }
    
    //verify current password if not root
    if (uid != 0 && old_passwd && old_passwd[0]) {
        char oldpw[128];
        read_password("Current password: ", oldpw, sizeof(oldpw));
        if (!verify_password(oldpw, old_passwd)) {
            printf("passwd: authentication failure\n");
            return 1;
        }
    }
    
    //read new password
    char newpw1[128], newpw2[128];
    read_password("New password: ", newpw1, sizeof(newpw1));
    read_password("Retype new password: ", newpw2, sizeof(newpw2));
    
    if (strcmp(newpw1, newpw2) != 0) {
        printf("passwd: passwords do not match\n");
        return 1;
    }
    
    //encrypt and update
    char* encrypted = crypt_simple(newpw1);
    
    //rewrite passwd file
    const char* passwd_file = PASSWD_FILE_WRITABLE;
    int test_fd = open(passwd_file, O_RDONLY);
    if (test_fd < 0) {
        passwd_file = PASSWD_FILE;
    } else {
        close(test_fd);
    }
    
    int fd_in = open(passwd_file, O_RDONLY);
    char tmp_file[128];
    snprintf(tmp_file, sizeof(tmp_file), "%s.tmp", passwd_file);
    int fd_tmp = open(tmp_file, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd_in < 0 || fd_tmp < 0) {
        printf("passwd: cannot open passwd file %s\n", passwd_file);
        return 1;
    }
    
    char line[512];
    int pos = 0;
    char ch;
    while (read(fd_in, &ch, 1) > 0) {
        if (ch == '\n') {
            line[pos] = '\0';
            if (pos > 0) {
                //parse username (keep original line intact)
                char username_buf[128];
                int i = 0;
                while (i < pos && line[i] != ':' && i < 127) {
                    username_buf[i] = line[i];
                    i++;
                }
                username_buf[i] = '\0';
                
                if (strcmp(username_buf, target_user) == 0) {
                    //this is the user - write updated entry
                    target_copy.pw_passwd = encrypted;
                    putpwent(&target_copy, fd_tmp);
                } else {
                    //copy entire line as-is
                    write(fd_tmp, line, pos);
                    write(fd_tmp, "\n", 1);
                }
            }
            pos = 0;
            continue;
        }
        if ((size_t)pos < sizeof(line) - 1) line[pos++] = ch;
    }
    
    close(fd_in);
    close(fd_tmp);
    
    //replace original
    unlink(passwd_file);
    rename(tmp_file, passwd_file);
    
    printf("passwd: password updated successfully\n");
    return 0;
}
