#include <unistd.h>
#include <stdio.h>
#include <string.h>

static void test_rename() {
    printf("Testing rename()...\n");
    
    //create a test file
    int fd = creat("/tmp/testfile.txt", 0644);
    if (fd < 0) {
        printf("  Failed to create /tmp/testfile.txt\n");
        return;
    }
    write(fd, "Hello World", 11);
    close(fd);
    
    //rename it
    int r = rename("/tmp/testfile.txt", "/tmp/renamed.txt");
    if (r == 0) {
        printf("  rename() succeeded\n");
        //verify new file exists
        fd = open("/tmp/renamed.txt", 0);
        if (fd >= 0) {
            printf("  Renamed file exists\n");
            close(fd);
            unlink("/tmp/renamed.txt");
        } else {
            printf("  Renamed file NOT found\n");
        }
    } else {
        printf("  rename() failed\n");
        unlink("/tmp/testfile.txt");
    }
}

static void test_dup() {
    printf("Testing dup()...\n");
    
    int fd = creat("/tmp/duptest.txt", 0644);
    if (fd < 0) {
        printf("  Failed to create test file\n");
        return;
    }
    
    int fd2 = dup(fd);
    if (fd2 >= 0) {
        printf("  dup() succeeded: fd=%d -> fd=%d\n", fd, fd2);
        write(fd, "test", 4);
        close(fd);
        close(fd2);
    } else {
        printf("  dup() failed\n");
        close(fd);
    }
    unlink("/tmp/duptest.txt");
}

static void test_dup2() {
    printf("Testing dup2()...\n");
    
    int fd = creat("/tmp/dup2test.txt", 0644);
    if (fd < 0) {
        printf("  Failed to create test file\n");
        return;
    }
    
    int target = 10;
    int r = dup2(fd, target);
    if (r == target) {
        printf("  dup2() succeeded: fd=%d -> fd=%d\n", fd, target);
        write(target, "test", 4);
        close(fd);
        close(target);
    } else {
        printf("  dup2() failed\n");
        close(fd);
    }
    unlink("/tmp/dup2test.txt");
}

static void test_pipe() {
    printf("Testing pipe()...\n");
    
    int pipefd[2];
    int r = pipe(pipefd);
    if (r != 0) {
        printf("  pipe() failed\n");
        return;
    }
    
    printf("  pipe() succeeded: read_fd=%d, write_fd=%d\n", pipefd[0], pipefd[1]);
    
    //write to pipe
    const char* msg = "Hello through pipe!";
    int w = write(pipefd[1], msg, strlen(msg));
    printf("  Wrote %d bytes to pipe\n", w);
    
    //read from pipe
    char buf[64] = {0};
    int rd = read(pipefd[0], buf, sizeof(buf) - 1);
    printf("  Read %d bytes from pipe: '%s'\n", rd, buf);
    
    close(pipefd[0]);
    close(pipefd[1]);
}

int main(void) {
    printf("=== FrostByteOS Pipe/Dup/Rename Test ===\n\n");
    
    test_rename();
    printf("\n");
    
    test_dup();
    printf("\n");
    
    test_dup2();
    printf("\n");
    
    test_pipe();
    printf("\n");
    
    printf("All tests completed!\n");
    return 0;
}