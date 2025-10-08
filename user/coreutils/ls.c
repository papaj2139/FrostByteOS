#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static void print_permissions(unsigned mode) {
    //file type
    if ((mode & 0170000) == 0040000) fputc(1, 'd'); //directory
    else if ((mode & 0170000) == 0120000) fputc(1, 'l'); //symlink
    else fputc(1, '-'); //regular file
    
    //owner permissions
    fputc(1, (mode & 0400) ? 'r' : '-');
    fputc(1, (mode & 0200) ? 'w' : '-');
    fputc(1, (mode & 0100) ? 'x' : '-');
    
    //group permissions
    fputc(1, (mode & 040) ? 'r' : '-');
    fputc(1, (mode & 020) ? 'w' : '-');
    fputc(1, (mode & 010) ? 'x' : '-');
    
    //other permissions
    fputc(1, (mode & 04) ? 'r' : '-');
    fputc(1, (mode & 02) ? 'w' : '-');
    fputc(1, (mode & 01) ? 'x' : '-');
}

int main(int argc, char** argv, char** envp) {
    (void)envp;
    const char* path = ".";
    int show_all = 0;
    int long_format = 0;
    int ai = 1;
    
    //parse options 
    while (ai < argc && argv[ai] && argv[ai][0] == '-' && argv[ai][1]) {
        if (strcmp(argv[ai], "-a") == 0) {
            show_all = 1;
            ai++;
        } else if (strcmp(argv[ai], "-l") == 0) {
            long_format = 1;
            ai++;
        } else if (strcmp(argv[ai], "-la") == 0 || strcmp(argv[ai], "-al") == 0) {
            long_format = 1;
            show_all = 1;
            ai++;
        } else {
            break;
        }
    }
    
    if (ai < argc && argv[ai] && argv[ai][0]) path = argv[ai];

    int fd = open(path, 0);
    if (fd < 0) {
        fprintf(2, "ls: cannot open %s\n", path);
        return 1;
    }

    char name[64];
    unsigned type = 0;
    unsigned idx = 0;
    int any = 0;
    
    while (readdir_fd(fd, idx, name, sizeof(name), &type) == 0) {
        //skip '.' and '..' by default unless -a
        if (!show_all && name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            idx++;
            continue;
        }
        any = 1;
        
        if (long_format) {
            //build full path for stat
            char fullpath[128];
            int pi = 0;
            //copy path
            for (int i = 0; path[i] && pi < 120; i++) {
                fullpath[pi++] = path[i];
            }
            //add separator if needed
            if (pi > 0 && fullpath[pi-1] != '/') {
                fullpath[pi++] = '/';
            }
            //add name
            for (int i = 0; name[i] && pi < 127; i++) {
                fullpath[pi++] = name[i];
            }
            fullpath[pi] = '\0';
            
            //get file stats
            struct stat st;
            if (stat(fullpath, &st) == 0) {
                //print permissions
                print_permissions(st.st_mode);
                fputc(1, ' ');
                
                //print links (hardcoded to 1 for now)
                fputc(1, '1');
                fputc(1, ' ');
                
                //print size (right-aligned, width 8)
                char sizebuf[16];
                int si = 0;
                unsigned long sz = st.st_size;
                if (sz == 0) {
                    sizebuf[si++] = '0';
                } else {
                    while (sz > 0) {
                        sizebuf[si++] = '0' + (sz % 10);
                        sz /= 10;
                    }
                }
                //pad with spaces
                for (int sp = si; sp < 8; sp++) fputc(1, ' ');
                //print size (reversed)
                for (int j = si - 1; j >= 0; j--) {
                    fputc(1, sizebuf[j]);
                }
                fputc(1, ' ');
            } else {
                //stat failed print placeholders
                fputs(1, "?????????? 1        0 ");
            }
        }
        
        fputs(1, name);
        if (type == 0x02) {
            fputc(1, '/');
        }
        fputc(1, '\n');
        idx++;
    }

    close(fd);
    return any ? 0 : 0;
}
