#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

static int copy_stream(int in_fd, int out_fd) {
    char buf[1024];
    for (;;) {
        int r = read(in_fd, buf, sizeof(buf));
        if (r < 0) return -1;
        if (r == 0) break;
        int off = 0;
        while (off < r) {
            int w = write(out_fd, buf + off, r - off);
            if (w <= 0) return -1;
            off += w;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    int force = 0; int ai = 1;
    if (ai < argc && argv[ai] && strcmp(argv[ai], "-f") == 0) { force = 1; ai++; }
    if (argc - ai < 2) {
        fprintf(2, "Usage: cp [-f] <src> <dst>\\n");
        return 1;
    }
    const char* src = argv[ai];
    const char* dst = argv[ai+1];

    //if dst is a directory append basename(src)
    struct stat st;
    if (stat(dst, &st) == 0 && (st.st_mode & 0170000) == 0040000) {
        //find basename of src
        const char* base = src;
        const char* slash = src;
        while (*slash) {
            if (*slash == '/') base = slash + 1;
            slash++;
        }
        char full[256];
        size_t dl = strlen(dst);
        if (dl >= sizeof(full)) return 1;
        memcpy(full, dst, dl); full[dl] = '\0';
        if (dl == 0 || full[dl-1] != '/') {
            if (dl + 1 >= sizeof(full)) return 1;
            full[dl++] = '/'; full[dl] = '\0';
        }
        size_t bl = strlen(base);
        if (dl + bl >= sizeof(full)) return 1;
        memcpy(full + dl, base, bl + 1);
        dst = full;
    }

    if (force) unlink(dst);

    int in = open(src, 0);
    if (in < 0) return 1;
    int out = creat(dst, 0666);
    if (out < 0) { close(in); return 1; }
    int rc = copy_stream(in, out);
    close(in);
    close(out);
    return (rc == 0) ? 0 : 1;
}
