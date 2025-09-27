#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

//types for ELF32
typedef unsigned int  Elf32_Word;
typedef unsigned int  Elf32_Addr;
typedef unsigned int  Elf32_Off;
typedef unsigned short Elf32_Half;

typedef struct {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;

typedef struct {
    int   d_tag;      //signed
    union {
        Elf32_Word d_val;
        Elf32_Addr d_ptr;
    } d_un;
} Elf32_Dyn;

#define PT_LOAD    1
#define PT_DYNAMIC 2

#define DT_NULL        0
#define DT_NEEDED      1
#define DT_STRTAB      5
#define DT_STRSZ       10
#define DT_RPATH       15
#define DT_RUNPATH     29

static void puts1(const char* s) {
    fputs(1, s);
}

static void puthex(unsigned x) {
    dprintf(1, "%08x", x);
}

//simple read_at using reopen+skip because there is no lseek
static int read_at(const char* path, unsigned off, void* buf, unsigned sz) {
    int fd = open(path, 0);
    if (fd < 0) return -1;
    char sink[512];
    unsigned skip = off;
    while (skip) {
        unsigned chunk = (skip > sizeof(sink)) ? (unsigned)sizeof(sink) : skip;
        int r = read(fd, sink, chunk);
        if (r <= 0) { close(fd); return -1; }
        skip -= (unsigned)r;
    }
    unsigned got = 0;
    while (got < sz) {
        int r = read(fd, (char*)buf + got, sz - got);
        if (r <= 0) { close(fd); return -1; }
        got += (unsigned)r;
    }
    close(fd);
    return 0;
}

//translate vaddr -> file offset using PT_LOAD segments
static int va_to_off(Elf32_Phdr* segs, int nseg, unsigned vaddr, unsigned* out_off) {
    for (int i = 0; i < nseg; i++) {
        Elf32_Phdr* ph = &segs[i];
        if (ph->p_type != PT_LOAD) continue;
        unsigned start = ph->p_vaddr;
        unsigned end = ph->p_vaddr + ph->p_memsz;
        if (vaddr >= start && vaddr < end) {
            *out_off = ph->p_offset + (vaddr - start);
            return 0;
        }
    }
    return -1;
}

static const char* getenv_ldlp(char** envp) {
    if (!envp) return 0;
    for (int i = 0; envp[i]; i++) {
        const char* s = envp[i];
        const char* p = "LD_LIBRARY_PATH=";
        size_t pl = strlen(p);
        if (strncmp(s, p, pl) == 0) return s + pl;
    }
    return 0;
}

static int try_dirlist(const char* dirlist, const char* libname, char* out, unsigned outsz) {
    if (!dirlist || !libname || !out || outsz == 0) return -1;
    const char* s = dirlist; const char* start = s;
    while (1) {
        if (*s == ':' || *s == '\0') {
            unsigned len = (unsigned)(s - start);
            if (len > 0 && len < outsz - 2) {
                unsigned pos = 0;
                if (len >= outsz) len = outsz - 1;
                memcpy(out, start, len); pos = len;
                if (pos == 0 || out[pos - 1] != '/') out[pos++] = '/';
                unsigned nl = (unsigned)strlen(libname);
                if (pos + nl >= outsz) nl = outsz - pos - 1;
                memcpy(out + pos, libname, nl); out[pos + nl] = '\0';
                int fd = open(out, 0);
                if (fd >= 0) {
                    close(fd);
                    return 0;
                }
            }
            if (*s == '\0') break;
            start = s + 1;
        }
        s++;
    }
    return -1;
}

int main(int argc, char** argv, char** envp) {
    if (argc < 2) {
        puts1("usage: ldd <file>\n");
        return 1;
    }
    const char* path = argv[1];
    const char* env_ldlp = getenv_ldlp(envp);

    Elf32_Ehdr eh;
    if (read_at(path, 0, &eh, sizeof(eh)) != 0) {
        puts1("ldd: failed to read ELF header\n");
        return 1;
    }
    if (eh.e_phoff == 0 || eh.e_phnum == 0) {
        puts1("ldd: no program headers\n");
        return 1;
    }

    //read program headers
    Elf32_Phdr segs[16];
    int ph_count = (eh.e_phnum < 16) ? eh.e_phnum : 16;
    for (int i = 0; i < ph_count; i++) {
        if (read_at(path, eh.e_phoff + i * eh.e_phentsize, &segs[i], sizeof(Elf32_Phdr)) != 0) {
            puts1("ldd: failed to read PHDR\n");
            return 1;
        }
    }

    //find PT_DYNAMIC
    Elf32_Phdr dyn = {0};
    int dyn_found = 0;
    for (int i = 0; i < ph_count; i++) {
        if (segs[i].p_type == PT_DYNAMIC) {
            dyn = segs[i];
            dyn_found = 1;
            break;
        }
    }
    if (!dyn_found) {
        puts1("ldd: no PT_DYNAMIC (statically linked?)\n");
        return 0;
    }

    unsigned strtab_va = 0;
    unsigned strsz = 0;
    unsigned needed_offs[32];
    int needed_count = 0;
    unsigned rpath_off = 0, runpath_off = 0;

    //iterate dynamic entries in file using p_offset
    for (unsigned off = 0; off + sizeof(Elf32_Dyn) <= dyn.p_filesz; off += sizeof(Elf32_Dyn)) {
        Elf32_Dyn d;
        if (read_at(path, dyn.p_offset + off, &d, sizeof(d)) != 0) break;
        if (d.d_tag == DT_NULL) break;
        if (d.d_tag == DT_STRTAB) strtab_va = d.d_un.d_ptr;
        else if (d.d_tag == DT_STRSZ) strsz = d.d_un.d_val;
        else if (d.d_tag == DT_NEEDED) {
            if (needed_count < (int)(sizeof(needed_offs)/sizeof(needed_offs[0]))) {
                needed_offs[needed_count++] = d.d_un.d_val;
            }
        } else if (d.d_tag == DT_RPATH) {
            rpath_off = d.d_un.d_val;
        } else if (d.d_tag == DT_RUNPATH) {
            runpath_off = d.d_un.d_val;
        }
    }

    //translate STRTAB VA to file offset
    unsigned strtab_off = 0;
    if (strtab_va == 0 || va_to_off(segs, ph_count, strtab_va, &strtab_off) != 0) {
        puts1("ldd: could not locate STRTAB\n");
        return 1;
    }

    //print RUNPATH / RPATH if present
    if (runpath_off) {
        char buf[128];
        unsigned so = strtab_off + runpath_off;
        if (read_at(path, so, buf, sizeof(buf)) == 0) {
            buf[sizeof(buf)-1] = '\0';
            puts1("RUNPATH: "); puts1(buf); puts1("\n");
        }
    }
    if (rpath_off) {
        char buf[128];
        unsigned so = strtab_off + rpath_off;
        if (read_at(path, so, buf, sizeof(buf)) == 0) {
            buf[sizeof(buf)-1] = '\0';
            puts1("RPATH: "); puts1(buf); puts1("\n");
        }
    }

    //print NEEDED entries with resolution
    for (int i = 0; i < needed_count; i++) {
        unsigned noff = needed_offs[i];
        char name[96];
        if (read_at(path, strtab_off + noff, name, sizeof(name)) != 0) continue;
        name[sizeof(name)-1] = '\0';
        char resolved[128]; resolved[0] = '\0';
        //search order: LD_LIBRARY_PATH, RUNPATH, RPATH, /lib
        if (env_ldlp) {
            if (try_dirlist(env_ldlp, name, resolved, sizeof(resolved)) == 0) {
                puts1("NEEDED: ");
                puts1(name);
                puts1(" => ");
                puts1(resolved);
                puts1("\n");
                continue;
            }
        }
        char plist[128]; plist[0] = '\0';
        if (runpath_off) {
            unsigned so = strtab_off + runpath_off;
            if (read_at(path, so, plist, sizeof(plist)) == 0) {
                plist[sizeof(plist)-1] = '\0';
                if (try_dirlist(plist, name, resolved, sizeof(resolved)) == 0) {
                    puts1("NEEDED: ");
                    puts1(name);
                    puts1(" => ");
                    puts1(resolved);
                    puts1("\n");
                    continue;
                }
            }
        }
        plist[0] = '\0';
        if (rpath_off) {
            unsigned so = strtab_off + rpath_off;
            if (read_at(path, so, plist, sizeof(plist)) == 0) {
                plist[sizeof(plist)-1] = '\0';
                if (try_dirlist(plist, name, resolved, sizeof(resolved)) == 0) {
                    puts1("NEEDED: ");
                    puts1(name);
                    puts1(" => ");
                    puts1(resolved);
                    puts1("\n");
                    continue;
                }
            }
        }
        //fallback /lib
        if (try_dirlist("/lib", name, resolved, sizeof(resolved)) == 0) {
            puts1("NEEDED: ");
            puts1(name);
            puts1(" => ");
            puts1(resolved);
            puts1("\n");
        } else {
            puts1("NEEDED: ");
            puts1(name);
            puts1(" => not found\n");
        }
    }

    return 0;
}
