#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

static void putstr(const char* s) {
    fputs(1, s);
}

static void puthex32(unsigned x) {
    dprintf(1, "%08x", x);
}

static const char* getenv_simple(char** envp, const char* key) {
    if (!envp || !key) return 0;
    size_t kl = strlen(key);
    for (int i = 0; envp[i]; i++) {
        const char* s = envp[i];
        if (strncmp(s, key, kl) == 0 && s[kl] == '=') return s + kl + 1;
    }
    return 0;
}

int main(int argc, char** argv, char** envp) {
    const char* lib = (argc > 1) ? argv[1] : "libc.so.1";
    const char* sym = (argc > 2) ? argv[2] : "strlen";

    const char* ldlp = getenv_simple(envp, "LD_LIBRARY_PATH");
    if (ldlp) {
        putstr("LD_LIBRARY_PATH=");
        putstr(ldlp);
        putstr("\n");
    }

    putstr("dlopen(\"");
    putstr(lib);
    putstr("\") => ");
    int h = dlopen(lib, 0);
    if (h < 0) {
        putstr("FAIL\n");
        return 1;
    }
    putstr("handle=");
    puthex32((unsigned)h);
    putstr("\n");

    putstr("dlsym(\"");
    putstr(sym);
    putstr("\") => ");
    void* p = dlsym(h, sym);
    if (!p) {
        putstr("NULL\n");
        return 2;
    }
    puthex32((unsigned)(unsigned long)p);
    putstr("\n");

    //if querying strlen call it
    if (strcmp(sym, "strlen") == 0) {
        size_t (*fstrlen)(const char*) = (size_t(*)(const char*))p;
        const char* test = "hello-from-dltest";
        size_t n = fstrlen(test);
        putstr("call strlen(\"");
        putstr(test);
        putstr("\") => ");
        char nb[16]; int i = 0;
        unsigned v = (unsigned)n;
        char tmp[16];
        int t=0;
        if (v == 0) nb[i++]='0';
        while (v) {
            tmp[t++] = (char)('0' + (v % 10));
            v/=10;
        }
        while (t) { nb[i++] = tmp[--t]; }
        nb[i] = '\0';
        putstr(nb);
        putstr("\n");
    }

    //try dlsym("write") and call it
    void* pw = dlsym(h, "write");
    if (pw) {
        int (*fwritep)(int, const void*, size_t) = (int(*)(int,const void*,size_t))pw;
        const char* msg = "write via dlsym() works!\n";
        fwritep(1, msg, strlen(msg));
    }

    dlclose(h);
    return 0;
}
