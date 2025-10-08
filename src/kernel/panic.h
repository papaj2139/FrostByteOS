#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

//panic routines
void kpanic(void);
void kpanic_msg(const char* reason);

//current BSOD style ("classic" or "modern")
extern char* bsodVer;

#endif
