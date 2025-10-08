#include <unistd.h>
#include <stdint.h>
#include <string.h>

static int fail(const char* msg) {
    write(STDOUT_FILENO, msg, strlen(msg));
    write(STDOUT_FILENO, "\n", 1);
    return 1;
}

int main(void) {
    const intptr_t step = 4096;
    void* base = sbrk(0);
    if (base == (void*)-1) {
        return fail("TEST memory: FAIL sbrk base");
    }

    void* grow = sbrk(step * 4);
    if (grow == (void*)-1) {
        return fail("TEST memory: FAIL sbrk grow");
    }

    if (grow != base) {
        return fail("TEST memory: FAIL unexpected base");
    }

    memset(grow, 0xA5, step * 4);

    if (brk(base) != 0) {
        return fail("TEST memory: FAIL brk shrink");
    }

    write(STDOUT_FILENO, "TEST memory: PASS\n", sizeof("TEST memory: PASS\n") - 1);
    return 0;
}
