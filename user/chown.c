#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(2, "Usage: chown <uid>:<gid> <path>\n");
        return 1;
    }
    const char* spec = argv[1];
    int uid = -1, gid = -1;
    const char* colon = 0;
    for (const char* p = spec; *p; ++p) if (*p == ':') {
        colon = p;
        break;
    }
    if (!colon) {
        fprintf(2, "invalid spec, expected uid:gid\n");
        return 1;
    }
    //parse uid
    for (const char* p = spec; p < colon; ++p) {
        if (*p < '0' || *p > '9') {
            fprintf(2, "invalid uid\n");
            return 1;
        }
        uid = (uid < 0 ? 0 : uid * 10) + (*p - '0');
    }
    //parse gid
    for (const char* p = colon + 1; *p; ++p) {
        if (*p < '0' || *p > '9') {
            fprintf(2, "invalid gid\n");
            return 1;
        }
        gid = (gid < 0 ? 0 : gid * 10) + (*p - '0');
    }
    if (chown(argv[2], uid, gid) != 0) {
        fprintf(2, "chown failed: %s\n", argv[2]);
        return 1;
    }
    return 0;
}
