#include <unistd.h>
#include <string.h>

static volatile char* g_mp = 0;

static void print_hex(unsigned int v) {
    char buf[11]; //0x + 8 hex + \n
    buf[0] = '0'; 
    buf[1] = 'x';
    const char* hexd = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) {
        unsigned int shift = (7 - i) * 4;
        buf[2 + i] = hexd[(v >> shift) & 0xF];
    }
    buf[10] = '\n';
    write(1, buf, sizeof(buf));
}

int main(int argc, char** argv, char** envp) {
    (void)argc; 
    (void)argv; 
    (void)envp;

    const char* start = "forktest: starting\n";
    write(1, start, strlen(start));

    //sbrk test
    void* old = sbrk(4096);
    if (old != (void*)-1 && old != 0) {
        const char* msg = "sbrk: heap OK\n";
        memcpy(old, msg, strlen(msg) + 1);
        write(1, old, strlen((const char*)old));
        sbrk(-4096);
    }

    //mmap test (anonymous)
    g_mp = (char*)mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANON);
    const char* pfx = "mmap addr (parent) ";
    write(1, pfx, strlen(pfx));
    print_hex((unsigned int)g_mp);
    if (g_mp != (char*)-1 && g_mp != 0) {
        const char* m1 = "mmap: parent wrote\n";
        memcpy((void*)g_mp, m1, strlen(m1) + 1); 
        write(1, (const char*)g_mp, strlen((const char*)g_mp));
    } else {
        const char* mf = "mmap: failed\n";
        write(1, mf, strlen(mf));
    }

    int pid = fork();
    if (pid < 0) {
        const char* fe = "forktest: fork failed\n";
        write(1, fe, strlen(fe));
        _exit(1);
    }
    if (pid == 0) {
        //child
        const char* c = "child: pid ";
        write(1, c, strlen(c));
        int self = getpid();
        print_hex((unsigned int)self);
        const char* caddr = "mmap addr (child) ";
        write(1, caddr, strlen(caddr));
        print_hex((unsigned int)g_mp);
        if (g_mp != (char*)-1 && g_mp != 0) {
            const char* s1 = "child sees: ";
            write(1, s1, strlen(s1));
            write(1, (const char*)g_mp, strlen((const char*)g_mp));
            const char* m2 = "child updated\n";
            memcpy((void*)g_mp, m2, strlen(m2) + 1);
            const char* s2 = "child after update: ";
            write(1, s2, strlen(s2));
            write(1, (const char*)g_mp, strlen((const char*)g_mp));
            munmap((void*)g_mp, 4096);
        }
        sleep(1);
        _exit(42);
    } else {
        //parent
        const char* p1 = "parent: forked PID ";
        write(1, p1, strlen(p1));
        print_hex((unsigned int)pid);
        int status = 0;
        int ret = wait(&status);
        const char* p2 = "parent: waited PID ";
        write(1, p2, strlen(p2));
        print_hex((unsigned int)ret);
        const char* p3 = " with status ";
        write(1, p3, strlen(p3));
        print_hex((unsigned int)status);
        if (g_mp != (char*)-1 && g_mp != 0) {
            const char* s3 = "parent still has: ";
            write(1, s3, strlen(s3));
            write(1, (const char*)g_mp, strlen((const char*)g_mp));
            munmap((void*)g_mp, 4096);
        }
        char* av[] = { "/bin/sh", 0 };
        char* ev[] = { 0 };
        execve("/bin/sh", av, ev);
        _exit(1);
    }
    return 0;
}
