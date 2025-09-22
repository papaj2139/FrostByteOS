#include <unistd.h>
#include <string.h>

static char g_buf[4096];
static char g_buf2[4096];

typedef struct { 
    char dev[32]; 
    char mnt[64]; 
} mnt_t;

static void puts1(const char* s) { 
    write(1, s, strlen(s)); 
}

static void putc1(char c) { 
    write(1, &c, 1); 
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

static int load_mounts(mnt_t* arr, int maxn) {
    int n = read_file("/proc/mounts", g_buf2, sizeof(g_buf2));
    if (n < 0) return 0;
    int count = 0;
    const char* s = g_buf2;
    while (*s && count < maxn) {
        const char* e = s; while (*e && *e != '\n' && *e != '\r') e++;
        //format: <mount_point> <fs> <dev>
        //extract three tokens
        const char* p = s;
        //mount_point
        while (p < e && *p == ' ') p++;
        const char* mp = p; while (p < e && *p != ' ' && *p != '\t') p++;
        //fs (skip token)
        while (p < e && (*p == ' ' || *p == '\t')) p++;
        while (p < e && *p != ' ' && *p != '\t') p++;
        //dev
        while (p < e && (*p == ' ' || *p == '\t')) p++;
        const char* dv = p; while (p < e && *p != ' ' && *p != '\t') p++;
        if (dv < e) {
            int dl = (int)(e - dv);
            if (dl >= (int)sizeof(arr[count].dev)) dl = (int)sizeof(arr[count].dev) - 1;
            memcpy(arr[count].dev, dv, dl); arr[count].dev[dl] = '\0';
            int ml = (int)(mp ? (int)(e - (e < mp ? e : mp)) : 0); //default
            //recompute mount len precisely up to first space after mp
            const char* q = mp; while (q < e && *q != ' ' && *q != '\t') q++;
            ml = (int)(q - mp);
            if (ml >= (int)sizeof(arr[count].mnt)) ml = (int)sizeof(arr[count].mnt) - 1;
            memcpy(arr[count].mnt, mp, ml); arr[count].mnt[ml] = '\0';
            count++;
        }
        if (!*e) break;
        s = e + 1;
    }
    return count;
}

static const char* find_mnt(mnt_t* arr, int n, const char* devname) {
    for (int i = 0; i < n; i++) {
        //devname in mounts is like "ata0p1" or "none"
        if (strcmp(arr[i].dev, devname) == 0) return arr[i].mnt;
    }
    return "";
}

int main(int argc, char** argv, char** envp) {
    (void)argc; (void)argv; (void)envp;
    int n = read_file("/proc/devices", g_buf, sizeof(g_buf));
    if (n < 0) {
        puts1("lsblk: cannot read /proc/devices\n");
        return 1;
    }

    mnt_t mnts[32];
    int mcnt = load_mounts(mnts, 32);

    //header
    puts1("NAME       TYPE      MOUNTPOINT\n");
    //parse lines <name> and <type>
    const char* s = g_buf;
    while (*s) {
        //get line
        const char* e = s; while (*e && *e != '\n' && *e != '\r') e++;
        //extract name and type by splitting on space
        const char* p = s; while (p < e && *p == ' ') p++;
        const char* name = p; while (p < e && *p != ' ' && *p != '\t') p++;
        const char* type = p; while (type < e && (*type == ' ' || *type == '\t')) type++;
        const char* type_end = type; while (type_end < e && *type_end != ' ' && *type_end != '\t' && *type_end != '\n' && *type_end != '\r') type_end++;
        if (name < e) {
            int nl = (int)(p - name);
            int tl = (int)(type_end - type);
            char type_str[16] = {0};
            for (int i = 0; i < tl && i < (int)sizeof(type_str)-1; i++) {
                type_str[i] = type[i];
            }
            //show storage devices
            if (strcmp(type_str, "storage") == 0) {
                for (int i = 0; i < nl && name + i < e; i++) putc1(name[i]);
                for (int i = nl; i < 11; i++) putc1(' ');
                //type padded
                const char* t = type; for (int i = 0; i < tl && t + i < e; i++) putc1(t[i]);
                for (int i = tl; i < 10; i++) putc1(' ');
                //mountpoint if any
                char nbuf[32]; int k = 0; while (k < (int)sizeof(nbuf)-1 && name + k < e && k < nl) { nbuf[k] = name[k]; k++; } nbuf[k] = '\0';
                const char* mp = find_mnt(mnts, mcnt, nbuf);
                if (mp && *mp) { puts1(mp); }
                putc1('\n');
            }
        }
        if (!*e) break;
        s = e + 1;
    }
    return 0;
}
