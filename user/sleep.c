#include <unistd.h>
#include <stdio.h>
#include <string.h>

static int parse_u(const char* s) {
    int v = 0; if (!s || !*s) return -1;
    for (const char* p = s; *p; ++p) {
        if (*p < '0' || *p > '9') return -1;
        v = v*10 + (*p - '0');
    }
    return v;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(2, "Usage: sleep <seconds>\n");
        return 1;
    }
    int sec = parse_u(argv[1]); if (sec < 0) {
        fprintf(2, "sleep: invalid number\n");
        return 1;
    }
    sleep((unsigned)sec);
    return 0;
}
