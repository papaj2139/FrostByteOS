#include <passwd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

static struct passwd pwd_buf;
static struct group grp_buf;
static char line_buf[512];
static char* pw_fields[7];
static char* gr_fields[4];
static int pwd_fd = -1;
static int grp_fd = -1;

//simple XOR "encryption"
char* crypt_simple(const char* key) {
    static char buf[128];
    if (!key) return NULL;
    int len = strlen(key);
    if (len == 0) return "";
    
    //XOR with rotating key based on position
    for (int i = 0; i < len && i < 127; i++) {
        buf[i] = key[i] ^ ((i * 7 + 13) & 0xFF);
    }
    
    //convert to hex
    static char hex[256];
    for (int i = 0; i < len && i < 127; i++) {
        snprintf(hex + i*2, 3, "%02x", (unsigned char)buf[i]);
    }
    hex[len*2] = '\0';
    return hex;
}

int verify_password(const char* input, const char* stored) {
    if (!input || !stored) return 0;
    if (stored[0] == '\0') return 1; //empty password always matches
    char* encrypted = crypt_simple(input);
    return strcmp(encrypted, stored) == 0;
}

//parse a single passwd line into fields
static int parse_passwd_line(char* line, struct passwd* pw) {
    if (!line || !pw) return -1;
    
    //split by ':'
    int field = 0;
    char* p = line;
    char* start = p;
    
    while (*p && field < 7) {
        if (*p == ':' || *p == '\n') {
            *p = '\0';
            pw_fields[field++] = start;
            start = p + 1;
        }
        p++;
    }
    if (field < 6) return -1; //need at least 6 fields
    
    pw->pw_name = pw_fields[0];
    pw->pw_passwd = pw_fields[1];
    pw->pw_uid = atoi(pw_fields[2]);
    pw->pw_gid = atoi(pw_fields[3]);
    pw->pw_gecos = pw_fields[4];
    pw->pw_dir = pw_fields[5];
    pw->pw_shell = field > 6 ? pw_fields[6] : "/bin/sh";
    
    return 0;
}

static const char* get_passwd_file(void) {
    //try writable location first
    int fd = open(PASSWD_FILE_WRITABLE, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return PASSWD_FILE_WRITABLE;
    }
    return PASSWD_FILE;
}

void setpwent(void) {
    if (pwd_fd >= 0) close(pwd_fd);
    pwd_fd = open(get_passwd_file(), O_RDONLY);
}

void endpwent(void) {
    if (pwd_fd >= 0) {
        close(pwd_fd);
        pwd_fd = -1;
    }
}

struct passwd* getpwent(void) {
    if (pwd_fd < 0) setpwent();
    if (pwd_fd < 0) return NULL;
    
    //read line by line
    size_t pos = 0;
    char ch;
    while (pos < sizeof(line_buf) - 1) {
        int r = read(pwd_fd, &ch, 1);
        if (r <= 0) {
            endpwent();
            return NULL;
        }
        if (ch == '\n') {
            line_buf[pos] = '\0';
            if (pos > 0 && parse_passwd_line(line_buf, &pwd_buf) == 0) {
                return &pwd_buf;
            }
            pos = 0;
            continue;
        }
        line_buf[pos++] = ch;
    }
    
    endpwent();
    return NULL;
}

struct passwd* getpwnam(const char* name) {
    if (!name) return NULL;
    setpwent();
    struct passwd* pw;
    while ((pw = getpwent()) != NULL) {
        if (strcmp(pw->pw_name, name) == 0) {
            endpwent();
            return pw;
        }
    }
    endpwent();
    return NULL;
}

struct passwd* getpwuid(int uid) {
    setpwent();
    struct passwd* pw;
    while ((pw = getpwent()) != NULL) {
        if (pw->pw_uid == uid) {
            endpwent();
            return pw;
        }
    }
    endpwent();
    return NULL;
}

int putpwent(const struct passwd* pw, int fd) {
    if (!pw || fd < 0) return -1;
    
    char buf[512];
    snprintf(buf, sizeof(buf), "%s:%s:%d:%d:%s:%s:%s\n",
             pw->pw_name ? pw->pw_name : "",
             pw->pw_passwd ? pw->pw_passwd : "",
             pw->pw_uid,
             pw->pw_gid,
             pw->pw_gecos ? pw->pw_gecos : "",
             pw->pw_dir ? pw->pw_dir : "/",
             pw->pw_shell ? pw->pw_shell : "/bin/sh");
    
    int len = strlen(buf);
    return write(fd, buf, len) == len ? 0 : -1;
}

//group functions
static int parse_group_line(char* line, struct group* gr) {
    if (!line || !gr) return -1;
    
    int field = 0;
    char* p = line;
    char* start = p;
    
    while (*p && field < 4) {
        if (*p == ':' || *p == '\n') {
            *p = '\0';
            gr_fields[field++] = start;
            start = p + 1;
        }
        p++;
    }
    if (field < 3) return -1;
    
    gr->gr_name = gr_fields[0];
    gr->gr_passwd = gr_fields[1];
    gr->gr_gid = atoi(gr_fields[2]);
    gr->gr_mem = NULL; //TODO: parse member list
    
    return 0;
}

static const char* get_group_file(void) {
    //try writable location first
    int fd = open(GROUP_FILE_WRITABLE, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return GROUP_FILE_WRITABLE;
    }
    return GROUP_FILE;
}

void setgrent(void) {
    if (grp_fd >= 0) close(grp_fd);
    grp_fd = open(get_group_file(), O_RDONLY);
}

void endgrent(void) {
    if (grp_fd >= 0) {
        close(grp_fd);
        grp_fd = -1;
    }
}

struct group* getgrent(void) {
    if (grp_fd < 0) setgrent();
    if (grp_fd < 0) return NULL;
    
    size_t pos = 0;
    char ch;
    while (pos < sizeof(line_buf) - 1) {
        int r = read(grp_fd, &ch, 1);
        if (r <= 0) {
            endgrent();
            return NULL;
        }
        if (ch == '\n') {
            line_buf[pos] = '\0';
            if (pos > 0 && parse_group_line(line_buf, &grp_buf) == 0) {
                return &grp_buf;
            }
            pos = 0;
            continue;
        }
        line_buf[pos++] = ch;
    }
    
    endgrent();
    return NULL;
}

struct group* getgrnam(const char* name) {
    if (!name) return NULL;
    setgrent();
    struct group* gr;
    while ((gr = getgrent()) != NULL) {
        if (strcmp(gr->gr_name, name) == 0) {
            endgrent();
            return gr;
        }
    }
    endgrent();
    return NULL;
}

struct group* getgrgid(int gid) {
    setgrent();
    struct group* gr;
    while ((gr = getgrent()) != NULL) {
        if (gr->gr_gid == gid) {
            endgrent();
            return gr;
        }
    }
    endgrent();
    return NULL;
}
