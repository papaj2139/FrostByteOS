#ifndef _PASSWD_H
#define _PASSWD_H

//check writable tmpfs location first fallback to /etc
#define PASSWD_FILE_WRITABLE "/tmp/etc/passwd"
#define GROUP_FILE_WRITABLE "/tmp/etc/group"
#define PASSWD_FILE "/etc/passwd"
#define GROUP_FILE "/etc/group"

//passwd entry format: username:password:uid:gid:gecos:homedir:shell
struct passwd {
    char* pw_name;     //username
    char* pw_passwd;   //encrypted password (simple XOR hash)
    int pw_uid;        //user ID
    int pw_gid;        //group ID
    char* pw_gecos;    //full name/comment
    char* pw_dir;      //home directory
    char* pw_shell;    //login shell
};

//group entry format: groupname:password:gid:members
struct group {
    char* gr_name;     //group name
    char* gr_passwd;   //group password
    int gr_gid;        //group ID
    char** gr_mem;     //null-terminated list of members
};

//user database functions
struct passwd* getpwnam(const char* name);
struct passwd* getpwuid(int uid);
void setpwent(void);
void endpwent(void);
struct passwd* getpwent(void);

//group database functions
struct group* getgrnam(const char* name);
struct group* getgrgid(int gid);
void setgrent(void);
void endgrent(void);
struct group* getgrent(void);

//utility functions
int putpwent(const struct passwd* pw, int fd);
char* crypt_simple(const char* key);  //simple XOR "encryption"
int verify_password(const char* input, const char* stored);

#endif
