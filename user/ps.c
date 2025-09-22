#include <unistd.h>
#include <string.h>

static void puts1(const char* s) { 
    write(1, s, strlen(s)); 
}

static void putc1(char c) { 
    write(1, &c, 1); 
}

static int is_digits(const char* s) {
    if (!s || !*s) return 0;
    for (const char* p = s; *p; ++p) if (*p < '0' || *p > '9') return 0;
    return 1;
}

static int read_file(const char* path, char* buf, int bufsz) {
    int fd = open(path, 0);
    if (fd < 0) return -1;
    int off = 0;
    for (;;) {
        int r = read(fd, buf + off, bufsz - 1 - off);
        if (r <= 0) break;
        off += r;
        if (off >= bufsz - 1) break;
    }
    close(fd);
    buf[off] = '\0';
    return off;
}

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;
    //open /proc and iterate numeric entries
    int d = open("/proc", 0);
    if (d < 0) {
        puts1("ps: cannot open /proc\n");
        return 1;
    }
    puts1("PID   STATE      CMD\n");
    char name[64]; unsigned type = 0; unsigned idx = 0;
    while (readdir_fd(d, idx++, name, sizeof(name), &type) == 0) {
        if (!is_digits(name)) continue;
        if (name[0] == '0' && name[1] == '\0') continue; //skip kernel PID 0
        //build /proc/<pid>/status and cmdline
        char path[128];
        char status[256];
        char cmdline[256];
        //status
        {
            char* p = path; const char* a = "/proc/"; while (*a && (p - path) < (int)sizeof(path)-1) *p++ = *a++;
            const char* b = name; while (*b && (p - path) < (int)sizeof(path)-1) *p++ = *b++;
            const char* c = "/status"; while (*c && (p - path) < (int)sizeof(path)-1) *p++ = *c++;
            *p = '\0';
            if (read_file(path, status, sizeof(status)) < 0) continue;
        }
        //cmdline
        {
            char* p = path; const char* a = "/proc/"; while (*a && (p - path) < (int)sizeof(path)-1) *p++ = *a++;
            const char* b = name; while (*b && (p - path) < (int)sizeof(path)-1) *p++ = *b++;
            const char* c = "/cmdline"; while (*c && (p - path) < (int)sizeof(path)-1) *p++ = *c++;
            *p = '\0';
            if (read_file(path, cmdline, sizeof(cmdline)) < 0) cmdline[0] = '\0';
        }
        //parse status lines Name and State
        const char* pname = "";
        const char* pstate = "";
        {
            const char* s = status;
            while (*s) {
                //find end of line
                const char* e = s; while (*e && *e != '\n' && *e != '\r') e++;
                //compare prefix
                if (e - s >= 5 && s[0]=='N' && s[1]=='a' && s[2]=='m' && s[3]=='e' && s[4]==':') {
                    const char* val = s + 5; while (*val == '\t' || *val == ' ') val++;
                    pname = val;
                } else if (e - s >= 6 && s[0]=='S' && s[1]=='t' && s[2]=='a' && s[3]=='t' && s[4]=='e' && s[5]==':') {
                    const char* val = s + 6; while (*val == '\t' || *val == ' ') val++;
                    pstate = val;
                }
                if (!*e) break;
                s = e + 1;
            }
        }
        //trim newlines in pname/pstate for print
        char nbuf[32]; char sbuf[32];
        int ni=0; for (const char* p=pname; *p && *p!='\n' && *p!='\r' && ni<31; ++p) nbuf[ni++]=*p; nbuf[ni]='\0';
        int si=0; for (const char* p=pstate; *p && *p!='\n' && *p!='\r' && si<31; ++p) sbuf[si++]=*p; sbuf[si]='\0';
        //fallback command
        const char* cmd = cmdline[0] ? cmdline : nbuf;
        //print row
        puts1(name); int pad = 5 - (int)strlen(name); while (pad-- > 0) putc1(' ');
        puts1(" ");
        puts1(sbuf); pad = 10 - (int)strlen(sbuf); while (pad-- > 0) putc1(' ');
        puts1(" ");
        puts1(cmd);
        putc1('\n');
    }
    close(d);
    return 0;
}
