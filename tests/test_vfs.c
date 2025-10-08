#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

static int fail(const char* msg) {
    write(STDOUT_FILENO, msg, strlen(msg));
    write(STDOUT_FILENO, "\n", 1);
    return 1;
}

static int write_all(int fd, const char* data, int len) {
    int written = 0;
    while (written < len) {
        int rc = write(fd, data + written, len - written);
        if (rc <= 0) {
            return -1;
        }
        written += rc;
    }
    return 0;
}

int main(void) {
    const char* dir_path = "/tmp/test_vfs";
    const char* file_path = "/tmp/test_vfs/file";

    rmdir(dir_path);
    unlink(file_path);

    if (mkdir(dir_path, 0755) != 0) {
        return fail("TEST vfs: FAIL mkdir");
    }

    struct stat st;
    if (stat(dir_path, &st) != 0) {
        return fail("TEST vfs: FAIL stat dir");
    }
    if (!S_ISDIR(st.st_mode)) {
        return fail("TEST vfs: FAIL dir mode");
    }

    int fd = creat(file_path, 0644);
    if (fd < 0) {
        return fail("TEST vfs: FAIL creat");
    }

    const char payload[] = "vfs-check";
    if (write_all(fd, payload, (int)(sizeof(payload) - 1)) != 0) {
        close(fd);
        return fail("TEST vfs: FAIL write");
    }
    close(fd);

    if (stat(file_path, &st) != 0) {
        return fail("TEST vfs: FAIL stat file");
    }
    if (!S_ISREG(st.st_mode)) {
        return fail("TEST vfs: FAIL file mode");
    }
    if (st.st_size != (sizeof(payload) - 1)) {
        return fail("TEST vfs: FAIL file size");
    }

    if (unlink(file_path) != 0) {
        return fail("TEST vfs: FAIL unlink");
    }
    if (rmdir(dir_path) != 0) {
        return fail("TEST vfs: FAIL rmdir");
    }

    write(STDOUT_FILENO, "TEST vfs: PASS\n", sizeof("TEST vfs: PASS\n") - 1);
    return 0;
}
