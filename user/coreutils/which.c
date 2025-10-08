#include <unistd.h>
#include <stdio.h>
#include <string.h>

static int exists(const char* path) {
    int fd = open(path, 0);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(2, "Usage: which <name> [name...]\n");
        return 1;
    }
    int found_any = 0;
    for (int i = 1; i < argc; i++) {
        const char* name = argv[i];
        if (!name || !*name) continue;
        char path[128];
        //try /bin/name
        int n = 0; const char* p = "/bin/";
        while (*p && n < (int)sizeof(path)-1) path[n++] = *p++;
        const char* q = name;
        while (*q && n < (int)sizeof(path)-1) path[n++] = *q++;
        path[n] = '\0';
        if (exists(path)) { printf("%s\n", path); found_any = 1; continue; }
        //try /usr/bin/name
        n = 0; p = "/usr/bin/";
        while (*p && n < (int)sizeof(path)-1) path[n++] = *p++;
        q = name;
        while (*q && n < (int)sizeof(path)-1) path[n++] = *q++;
        path[n] = '\0';
        if (exists(path)) {
            printf("%s\n", path);
            found_any = 1;
            continue;
        }
    }
    return found_any ? 0 : 1;
}
