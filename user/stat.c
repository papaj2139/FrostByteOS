#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(2, "Usage: stat <path>\n");
        return 1;
    }
    struct stat st;
    if (stat(argv[1], &st) != 0) {
        fprintf(2, "stat failed\n");
        return 1;
    }
    printf("mode=0x%08X uid=%u gid=%u size=%u\n", st.st_mode, st.st_uid, st.st_gid, st.st_size);
    return 0;
}
