#include <unistd.h>
#include <string.h>
#include <stdio.h>

static void puts1(const char* s) { 
    fputs(1, s); 
}

static void do_div0(void) {
    asm volatile("xor %%edx, %%edx; mov $1, %%eax; xor %%ecx, %%ecx; div %%ecx" ::: "eax", "ecx", "edx");
}

static void do_breakpoint(void) { 
    asm volatile("int3"); 
}

static void do_overflow(void) {
    asm volatile("mov $0x7fffffff, %%eax; add $1, %%eax; into" ::: "eax");
}

static void do_bound(void) {
    struct { int low; int high; } bounds = {0, 1};
    int idx = 2;
    asm volatile("bound %0, %1" :: "r"(idx), "m"(bounds));
}

static void do_invalid_opcode(void) { 
    asm volatile("ud2"); 
}

static void do_gpf(void) { 
    asm volatile("cli"); 
}

static void do_pagefault(void) {
    volatile int* p = (volatile int*)0x0;
    *p = 42;
}

static void do_x87_fpe(void) {
    unsigned short cw;
    asm volatile("fnstcw %0" : "=m"(cw));
    cw &= ~(1u << 2); //unmask divide-by-zero exception (ZM)
    asm volatile("fldcw %0" :: "m"(cw));
    asm volatile("fld1; fldz; fdivp %%st, %%st(1); fwait" ::: "st");
}

static void do_align_check(void) {
    //set AC flag in EFLAGS (bit 18)
    asm volatile("pushf; pop %%eax; or $0x40000, %%eax; push %%eax; popf" ::: "eax");
    volatile char buf[8];
    volatile int* p = (volatile int*)(buf + 1); //misaligned dword
    int v = *p; (void)v;
}

static void do_debug(void) {
    //set TF (trap flag) next instruction will cause #DB
    asm volatile("pushf; pop %%eax; or $0x100, %%eax; push %%eax; popf; nop" ::: "eax");
}

static void usage(const char* argv0) {
    puts1("Usage: "); puts1(argv0); puts1(" <exception>\n");
    puts1("Exceptions:\n");
    puts1("  div0           - raise divide-by-zero (#0)\n");
    puts1("  int3|breakpoint- raise breakpoint (#3)\n");
    puts1("  overflow|into  - raise overflow via INTO (#4)\n");
    puts1("  bound          - raise BOUND range exceeded (#5)\n");
    puts1("  ud|ud2|ill     - raise invalid opcode (#6)\n");
    puts1("  gpf|gp         - raise general protection fault (#13)\n");
    puts1("  page|pf        - raise page fault (#14)\n");
    puts1("  x87|fpe        - raise x87 floating-point exception (#16)\n");
    puts1("  ac|align       - attempt alignment check (#17)\n");
    puts1("  debug|int1     - set TF to raise debug exception (#1)\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const char* a = argv[1];
    if (!strcmp(a, "div0")) { do_div0(); }
    else if (!strcmp(a, "int3") || !strcmp(a, "breakpoint")) { do_breakpoint(); }
    else if (!strcmp(a, "overflow") || !strcmp(a, "into")) { do_overflow(); }
    else if (!strcmp(a, "bound")) { do_bound(); }
    else if (!strcmp(a, "ud") || !strcmp(a, "ud2") || !strcmp(a, "ill") || !strcmp(a, "invalid")) { do_invalid_opcode(); }
    else if (!strcmp(a, "gpf") || !strcmp(a, "gp") || !strcmp(a, "general")) { do_gpf(); }
    else if (!strcmp(a, "page") || !strcmp(a, "pf") || !strcmp(a, "segv")) { do_pagefault(); }
    else if (!strcmp(a, "x87") || !strcmp(a, "fpe") || !strcmp(a, "fdiv0")) { do_x87_fpe(); }
    else if (!strcmp(a, "ac") || !strcmp(a, "align") || !strcmp(a, "alignment")) { do_align_check(); }
    else if (!strcmp(a, "debug") || !strcmp(a, "int1") || !strcmp(a, "trap")) { do_debug(); }
    else {
        usage(argv[0]);
        return 1;
    }

    //if reached here the exception did not fire (or was masked)
    puts1("No exception occurred (maybe masked/unsupported)\n");
    return 0;
}
