#include <unistd.h>
#include <string.h>

static void puts1(const char* s) { 
    write(1, s, strlen(s)); 
}
static void puts2(const char* a, const char* b) { 
    write(1, a, strlen(a)); 
    write(1, b, strlen(b)); 
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    //recompute argc from argv
    int ac = 0; if (argv) { 
        while (argv[ac]) ac++; 
    }
    if (ac < 2) {
        puts1("Usage:\n  mount <device> <mount_point> <fs>\n  mount -u <mount_point>\n");
        return 1;
    }
    if (strcmp(argv[1], "-u") == 0) {
        if (ac < 3) { 
            puts1("mount -u <mount_point>\n"); 
            return 1; 
        }
        if (umount(argv[2]) == 0) { 
            puts2("unmounted ", argv[2]); 
            puts1("\n"); 
            return 0; 
        }
        puts1("umount failed\n");
        return 1;
    }
    if (ac < 4) { 
        puts1("mount <device> <mount_point> <fs>\n"); 
        return 1; 
    }
    const char* dev = argv[1];
    //accept full path like /dev/ata0p1 by stripping the /dev/ prefix
    if (dev[0] == '/' && dev[1] == 'd' && dev[2] == 'e' && dev[3] == 'v' && dev[4] == '/') {
        dev += 5;
    }
    if (mount(dev, argv[2], argv[3]) == 0) {
        puts1("mounted\n");
        return 0;
    }
    puts1("mount failed\n");
    return 1;
}
