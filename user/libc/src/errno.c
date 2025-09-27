#include <errno.h>

static int g_errno_value = 0;

int* __errno_location(void)
{
    return &g_errno_value;
}