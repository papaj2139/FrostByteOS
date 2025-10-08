#ifndef LIBC_LIMITS_H
#define LIBC_LIMITS_H

//character limits
#define CHAR_BIT   8

#define SCHAR_MIN  (-128)
#define SCHAR_MAX  127
#define UCHAR_MAX  255

#ifdef __CHAR_UNSIGNED__
#define CHAR_MIN   0
#define CHAR_MAX   UCHAR_MAX
#else
#define CHAR_MIN   SCHAR_MIN
#define CHAR_MAX   SCHAR_MAX
#endif

//short limits
#define SHRT_MIN   (-32768)
#define SHRT_MAX   32767
#define USHRT_MAX  65535

//int limits
#define INT_MIN    (-2147483647 - 1)
#define INT_MAX    2147483647
#define UINT_MAX   4294967295U

//long limits (same as int in ILP32)
#define LONG_MIN   (-2147483647L - 1)
#define LONG_MAX   2147483647L
#define ULONG_MAX  4294967295UL

//long long limits
#define LLONG_MIN  (-9223372036854775807LL - 1)
#define LLONG_MAX  9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL

//ssize_t/ptrdiff_t compatible
#define PTRDIFF_MIN LONG_MIN
#define PTRDIFF_MAX LONG_MAX

//size type
#define SIZE_MAX   ULONG_MAX

#endif
