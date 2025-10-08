#include <unistd.h>
#include <string.h>
#include <stdio.h>

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

static unsigned parse_pages(const char* line) {
    //find first number in the line
    const char* p = line;
    while (*p && (*p < '0' || *p > '9')) p++;
    unsigned v = 0;
    while (*p >= '0' && *p <= '9') { v = v*10 + (unsigned)(*p - '0'); p++; }
    return v;
}

int main(int argc, char** argv) {
    int human_mib = 0;
    if (argc > 1 && argv[1] && strcmp(argv[1], "-m") == 0) human_mib = 1;
    char buf[256];
    if (read_file("/proc/meminfo", buf, sizeof(buf)) < 0) {
        fprintf(2, "free: cannot read /proc/meminfo\n");
        return 1;
    }
    if (!human_mib) {
        fputs(1, buf);
        return 0;
    }
    //convert pages -> MiB for three lines if present
    //expect lines like: MemTotal: X pages MemFree: Y pages MemUsed: Z pages
    const char* keys[3] = {"MemTotal:", "MemFree:", "MemUsed:"};
    char out[256]; out[0] = '\0';
    const char* s = buf;
    for (int k = 0; k < 3; k++) {
        //ffind the line starting with keys[k]
        const char* p = s;
        while (*p) {
            //start of line
            const char* line = p;
            while (*p && *p != '\n') p++;
            const char* next = (*p == '\n') ? (p + 1) : p;
            if (strncmp(line, keys[k], strlen(keys[k])) == 0) {
                unsigned pages = parse_pages(line);
                unsigned mib = (unsigned)((pages * 4096u + 1024u*1024u - 1) / (1024u*1024u));
                char tmp[64];
                int n = 0;
                //format: key value MiB
                for (const char* q = keys[k]; *q; ++q) tmp[n++] = *q;
                tmp[n++] = ' ';
                //write number
                char t2[16]; int t=0; unsigned v=mib;
                if (v==0) t2[t++]='0';
                while(v){
                    t2[t++]=(char)('0'+(v%10));
                    v/=10;
                }
                while (t) tmp[n++] = t2[--t];
                tmp[n++] = ' ';
                tmp[n++] = 'M';
                tmp[n++] = 'i';
                tmp[n++] = 'B';
                tmp[n++] = '\n';
                tmp[n] = '\0';
                strcat(out, tmp);
                break;
            }
            p = next;
        }
    }
    fputs(1, out[0] ? out : buf);
    return 0;
}
