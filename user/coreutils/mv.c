#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

static const char* basename_of(const char* path) {
    const char* base = path;
    for (const char* p = path; *p; p++) if (*p == '/') base = p + 1;
    return base;
}

int main(int argc, char** argv) {
    int force = 0; int ai = 1;
    if (ai < argc && argv[ai] && strcmp(argv[ai], "-f") == 0) {
        force = 1;
        ai++;
    }
    if (argc - ai < 2) {
        fprintf(2, "Usage: mv [-f] <src> <dst>\n");
        return 1;
    }
    const char* src = argv[ai];
    const char* dst = argv[ai+1];

    //if dst is a directory append basename(src)
    struct stat st;
    char full[256];
    if (stat(dst, &st) == 0 && (st.st_mode & 0170000) == 0040000) {
        const char* base = basename_of(src);
        size_t dl = strlen(dst);
        if (dl >= sizeof(full)) { fprintf(2, "mv: path too long\n"); return 1; }
        memcpy(full, dst, dl);
        full[dl] = '\0';
        if (dl == 0 || full[dl-1] != '/') {
            if (dl + 1 >= sizeof(full)) { fprintf(2, "mv: path too long\n"); return 1; }
            full[dl++] = '/';
            full[dl] = '\0';
        }
        size_t bl = strlen(base);
        if (dl + bl >= sizeof(full)) { fprintf(2, "mv: path too long\n"); return 1; }
        memcpy(full + dl, base, bl + 1);
        dst = full;
    }

    //if -f try removing destination first (ignore errors)
    if (force) unlink(dst);

    //fast path: hard link + unlink (same filesystem)
    if (link(src, dst) == 0) {
        if (unlink(src) != 0) {
            fprintf(2, "mv: cannot remove source file after linking\n");
            //keep destination report failure
            return 1;
        }
        return 0;
    }

    //fallback copy then unlink
    int in = open(src, 0);
    if (in < 0) {
        fprintf(2, "mv: cannot open source\n");
        return 1;
    }
    int out = creat(dst, 0666);
    if (out < 0) {
        fprintf(2, "mv: cannot create destination\n");
        close(in);
        return 1;
    }

    char buf[1024];
    for (;;) {
        int r = read(in, buf, sizeof(buf));
        if (r < 0) {
            fprintf(2, "mv: read error\n");
            close(in);
            close(out);
            return 1;
        }
        if (r == 0) break;
        int off = 0;
        while (off < r) {
            int w = write(out, buf + off, r - off);
            if (w <= 0) {
                fprintf(2, "mv: write error\n");
                close(in);
                close(out);
                return 1;
            }
            off += w;
        }
    }
    close(in);
    close(out);

    if (unlink(src) != 0) {
        fprintf(2, "mv: cannot remove source after copy\n");
        return 1;
    }
    return 0;
}
