#ifndef LIBC_DLFCN_H
#define LIBC_DLFCN_H

#ifdef __cplusplus
extern "C" {
#endif

//flags (subset)
#define RTLD_LAZY  1
#define RTLD_NOW   2
#define RTLD_LOCAL 4
#define RTLD_GLOBAL 8

int  dlopen(const char* path, int flags);
void* dlsym(int handle, const char* name);
int  dlclose(int handle);

//eturn last error string (not implemented yet)
static inline const char* dlerror(void) { return 0; }

#ifdef __cplusplus
}
#endif

#endif
