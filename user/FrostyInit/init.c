#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#define MAX_LINE 256
#define MAX_MOUNTS 16
#define MAX_COPIES 16
#define MAX_MKDIRS 16

typedef struct {
    char source[64];
    char target[64];
    char fstype[32];
    char options[32];
} mount_entry_t;

typedef struct {
    char src[128];
    char dst[128];
} copy_entry_t;

typedef struct {
    char path[128];
    int mode;
} mkdir_entry_t;

static char default_init[128] = "/bin/login";
static mount_entry_t mounts[MAX_MOUNTS];
static int mount_count = 0;
static copy_entry_t copies[MAX_COPIES];
static int copy_count = 0;
static mkdir_entry_t mkdirs[MAX_MKDIRS];
static int mkdir_count = 0;
static int respawn = 1;
static int parse_cmdline = 1;

//kernel command line params
static char root_device[64] = "";
static char init_override[128] = "";

static void log_msg(const char* msg) {
    write(1, "[FrostyInit] ", 13);
    write(1, msg, strlen(msg));
    write(1, "\n", 1);
}

static void log_error(const char* msg) {
    write(2, "[FrostyInit ERROR] ", 19);
    write(2, msg, strlen(msg));
    write(2, "\n", 1);
}

//trim whitespace from string
static void trim(char* s) {
    char* p = s;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);

    int len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
}

//parse octal mode string
static int parse_mode(const char* str) {
    int mode = 0;
    while (*str >= '0' && *str <= '7') {
        mode = mode * 8 + (*str - '0');
        str++;
    }
    return mode;
}

//parse configuration file
static int parse_config(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        log_error("Cannot open config file");
        return -1;
    }

    char buffer[2048];
    int n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (n <= 0) return -1;
    buffer[n] = '\0';

    //Parse line by line
    char* line = buffer;
    while (line && *line) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';

        trim(line);

        //skip comments and empty lines
        if (*line == '#' || *line == '\0') {
            line = next;
            continue;
        }

        //parse key=value
        char* eq = strchr(line, '=');
        if (!eq) {
            line = next;
            continue;
        }

        *eq = '\0';
        char* key = line;
        char* value = eq + 1;
        trim(key);
        trim(value);

        if (strcmp(key, "default_init") == 0) {
            strncpy(default_init, value, sizeof(default_init) - 1);
        } else if (strcmp(key, "respawn") == 0) {
            respawn = (strcmp(value, "yes") == 0 || strcmp(value, "1") == 0);
        } else if (strcmp(key, "parse_cmdline") == 0) {
            parse_cmdline = (strcmp(value, "yes") == 0 || strcmp(value, "1") == 0);
        } else if (strcmp(key, "mounts") == 0 && mount_count < MAX_MOUNTS) {
            //format: source:target:fstype:options
            char* s = value;
            char* t = strchr(s, ':');
            if (!t) goto next_line;
            *t++ = '\0';
            char* fs = strchr(t, ':');
            if (!fs) goto next_line;
            *fs++ = '\0';
            char* opts = strchr(fs, ':');
            if (opts) *opts++ = '\0';

            strncpy(mounts[mount_count].source, s, 63);
            strncpy(mounts[mount_count].target, t, 63);
            strncpy(mounts[mount_count].fstype, fs, 31);
            if (opts) strncpy(mounts[mount_count].options, opts, 31);
            mount_count++;
        } else if (strcmp(key, "copy_files") == 0 && copy_count < MAX_COPIES) {
            //Format: src:dst
            char* src = value;
            char* dst = strchr(src, ':');
            if (!dst) goto next_line;
            *dst++ = '\0';
            strncpy(copies[copy_count].src, src, 127);
            strncpy(copies[copy_count].dst, dst, 127);
            copy_count++;
        } else if (strcmp(key, "mkdirs") == 0 && mkdir_count < MAX_MKDIRS) {
            //Format: path:mode
            char* path = value;
            char* mode_str = strchr(path, ':');
            int mode = 0755;
            if (mode_str) {
                *mode_str++ = '\0';
                mode = parse_mode(mode_str);
            }
            strncpy(mkdirs[mkdir_count].path, path, 127);
            mkdirs[mkdir_count].mode = mode;
            mkdir_count++;
        }

next_line:
        line = next;
    }

    return 0;
}

//default config when config file is missing
static void setup_default_config(void) {
    log_msg("Setting up default mounts and directories");

    //default mounts: tmpfs on /tmp, devfs on /dev, procfs already mounted
    strcpy(mounts[mount_count].source, "none");
    strcpy(mounts[mount_count].target, "/tmp");
    strcpy(mounts[mount_count].fstype, "tmpfs");
    strcpy(mounts[mount_count].options, "rw");
    mount_count++;

    strcpy(mounts[mount_count].source, "none");
    strcpy(mounts[mount_count].target, "/dev");
    strcpy(mounts[mount_count].fstype, "devfs");
    strcpy(mounts[mount_count].options, "rw");
    mount_count++;

    //default directories
    strcpy(mkdirs[mkdir_count].path, "/tmp");
    mkdirs[mkdir_count].mode = 01777;  //sticky bit + rwxrwxrwx
    mkdir_count++;

    strcpy(mkdirs[mkdir_count].path, "/dev");
    mkdirs[mkdir_count].mode = 0755;
    mkdir_count++;

    strcpy(mkdirs[mkdir_count].path, "/root");
    mkdirs[mkdir_count].mode = 0700;
    mkdir_count++;

    strcpy(mkdirs[mkdir_count].path, "/home");
    mkdirs[mkdir_count].mode = 0755;
    mkdir_count++;
}

//parse kernel command line
static void parse_kernel_cmdline(void) {
    int fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0) {
        log_msg("Cannot read /proc/cmdline");
        return;
    }

    char buffer[512];
    int n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (n <= 0) return;
    buffer[n] = '\0';

    //parse space-separated arguments
    char* arg = strtok(buffer, " \t\n");
    while (arg) {
        if (strncmp(arg, "root=", 5) == 0) {
            strncpy(root_device, arg + 5, sizeof(root_device) - 1);
            log_msg("Found root device in cmdline");
        } else if (strncmp(arg, "init=", 5) == 0) {
            strncpy(init_override, arg + 5, sizeof(init_override) - 1);
            log_msg("Found init override in cmdline");
        }
        arg = strtok(NULL, " \t\n");
    }
}

//copy a file
static int copy_file(const char* src, const char* dst) {
    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0) return -1;

    int fd_dst = creat(dst, 0644);
    if (fd_dst < 0) {
        close(fd_src);
        return -1;
    }

    char buf[512];
    int n;
    while ((n = read(fd_src, buf, sizeof(buf))) > 0) {
        write(fd_dst, buf, n);
    }

    close(fd_src);
    close(fd_dst);
    return 0;
}

//mount root filesystem if specified
static int mount_root(void) {
    if (root_device[0] == '\0') return 0;

    log_msg("Mounting root filesystem...");

    //try to mount as FAT32 first
    if (mount(root_device, "/mnt", "fat32") == 0) {
        log_msg("Root filesystem mounted successfully (FAT32)");
        return 0;
    }

    //try FAT16
    if (mount(root_device, "/mnt", "fat16") == 0) {
        log_msg("Root filesystem mounted successfully (FAT16)");
        return 0;
    }

    log_error("Failed to mount root filesystem");
    return -1;
}

int main(int argc, char** argv, char** envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    log_msg("FrostyInit starting...");

    //first mount procfs early so we can read cmdline
    log_msg("Mounting early procfs");
    mount("none", "/proc", "procfs");

    //parse kernel command line
    if (parse_cmdline) {
        log_msg("Parsing kernel command line");
        parse_kernel_cmdline();
    }

    //parse configuration file
    log_msg("Loading configuration from /etc/init.conf");
    if (parse_config("/etc/init.conf") != 0) {
        log_msg("Using default configuration");
        setup_default_config();
    }

    //mount root filesystem if specified in cmdline
    if (root_device[0] != '\0') {
        mkdir("/mnt", 0755);
        mount_root();
    }

    //create directories
    for (int i = 0; i < mkdir_count; i++) {
        log_msg("Creating directory");
        mkdir(mkdirs[i].path, mkdirs[i].mode);
    }

    //setup mounts
    for (int i = 0; i < mount_count; i++) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Mounting %s -> %s (%s)",
                 mounts[i].source, mounts[i].target, mounts[i].fstype);
        log_msg(msg);

        if (mount(mounts[i].source, mounts[i].target, mounts[i].fstype) != 0) {
            snprintf(msg, sizeof(msg), "Mount failed: %s", mounts[i].target);
            log_error(msg);
        }
    }

    //copy files
    for (int i = 0; i < copy_count; i++) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Copying %s -> %s", copies[i].src, copies[i].dst);
        log_msg(msg);

        if (copy_file(copies[i].src, copies[i].dst) != 0) {
            snprintf(msg, sizeof(msg), "Copy failed: %s", copies[i].src);
            log_error(msg);
        }
    }

    //determine which init program to run
    const char* init_program = default_init;
    if (init_override[0] != '\0') {
        init_program = init_override;
        log_msg("Using init override from kernel cmdline");
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "Starting %s (respawn=%s)", init_program, respawn ? "yes" : "no");
    log_msg(msg);

    if (respawn) {
        //respawn mode - restart init program if it exits
        for (;;) {
            int cpid = fork();
            if (cpid == 0) {
                char* args[] = { (char*)init_program, NULL };
                char* env[] = { NULL };
                execve(init_program, args, env);
                _exit(127);
            }

            //reap children until init program exits
            int status = 0;
            while (1) {
                int w = wait(&status);
                if (w == cpid) break; //init program finished, respawn
                if (w < 0) break;     //no children
            }

            log_msg("Init program exited, respawning...");
            sleep(1); //brief delay before respawn
        }
    } else {
        //one-shot mode - exec into init program
        char* args[] = { (char*)init_program, NULL };
        char* env[] = { NULL };
        execve(init_program, args, env);
        log_error("Failed to exec init program");
        _exit(1);
    }

    return 0;
}
