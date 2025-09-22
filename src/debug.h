#ifndef DEBUG_H
#define DEBUG_H

//compile-time debug controls set to 1 to enable classes of logs
#ifndef LOG_SCHED
#define LOG_SCHED 0
#endif

#ifndef LOG_SCHED_TABLE
#define LOG_SCHED_TABLE 0
#endif

#ifndef LOG_SYSCALL
#define LOG_SYSCALL 0
#endif

#ifndef LOG_TICK
#define LOG_TICK 0
#endif

#ifndef LOG_PROC
#define LOG_PROC 0
#endif

//alias for more verbose scheduler diagnostics rn same as LOG_SCHED
#ifndef LOG_SCHED_DIAG
#define LOG_SCHED_DIAG LOG_SCHED
#endif

#ifndef LOG_VFS
#define LOG_VFS 0
#endif

#ifndef LOG_ELF
#define LOG_ELF 0
#endif

#ifndef LOG_ELF_DIAG
#define LOG_ELF_DIAG 0
#endif

#ifndef LOG_EXEC
#define LOG_EXEC 0  
#endif

#ifndef LOG_FAT16
#define LOG_FAT16 0
#endif

#ifndef LOG_ATA
#define LOG_ATA 0
#endif

#endif
