#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include "mm/vmm.h"
#include "kernel/dynlink.h"

//forward declaration to avoid including device_manager.h here
struct device;
//forward declaration for wait queues
struct process;

//wait queue for sleeping processes (event-based wakeups)
typedef struct wait_queue {
    struct process* head;   //singly-linked list via process.wait_next
} wait_queue_t;

#define MAX_PROCESSES 64
#define PROCESS_NAME_MAX 32
#define KERNEL_STACK_SIZE 16384

//process states
typedef enum {
    PROC_UNUSED = 0,    //process slot is free
    PROC_EMBRYO,        //orocess being created
    PROC_RUNNABLE,      //ready to run
    PROC_RUNNING,       //currently running
    PROC_SLEEPING,      //sleeping/blocked
    PROC_ZOMBIE         //terminated but not reaped
} proc_state_t;

//CPU context for process switching
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, esp, ebp;
    uint32_t eip, eflags;
    uint32_t cs, ds, es, fs, gs, ss;
} cpu_context_t;

//process Control
typedef struct process {
    uint32_t pid;                    //process ID
    uint32_t ppid;                   //parent process ID
    proc_state_t state;              //process state
    char name[PROCESS_NAME_MAX];     //process name
    char cmdline[128];               //argv[0] or command name (for /proc/<pid>/cmdline)

    //MM
    page_directory_t page_directory; //virtual memory space
    uint32_t kernel_stack;           //kernel stack (physical addr)
    uint32_t user_stack_top;         //yser stack top (virtual addr)
    uint32_t heap_start;             //user heap start
    uint32_t heap_end;               //user heap end
    uint32_t user_eip;               //intended user-mode entry (virtual addr)

    //CPU contexts
    cpu_context_t context;           //saved user-mode CPU state (for iret to ring 3)
    cpu_context_t kcontext;          //saved kernel-mode CPU state (for resuming blocked syscalls)

    //scheduling
    uint32_t time_slice;             //remaining time slice
    uint32_t priority;               //process priority (0 = highest)
    uint32_t wakeup_tick;            //absolute tick to wake from sleep (0 = not sleeping)
    uint32_t base_priority;          //base priority before aging/boost
    int32_t  aging_score;            //scheduler aging accumulator / bonus
    uint8_t  static_priority;        //user-visible nice level (0..MAX)
    uint16_t weight;                 //scheduler weight derived from static priority

    //parent/child relationships
    struct process* parent;          //parent process
    struct process* children;        //first child
    struct process* sibling;         //next sibling

    //exit status
    int exit_code;                   //exit code when zombie

    //file descriptors (basic)
    int fd_table[16];                //file descriptor table

    bool started;                    //process has been started (first run done)
    bool in_kernel;                  //currently executing in kernel (resume via kcontext)

    //controlling TTY and its per-process mode
    struct device* tty;              //controlling TTY device (e.g., tty0)
    uint32_t tty_mode;               //TTY mode bits (see drivers/tty.h)

    //current working directory (absolute normalized path)
    char cwd[256];

    //signals
    uint32_t sig_pending;               //bitmask of pending signals (1..31)
    uint32_t sig_blocked;               //bitmask of blocked signals
    uint32_t sig_handlers[32];          //user pointers: 0=SIG_DFL 1=SIG_IGN else handler/trampoline
    cpu_context_t sig_saved_ctx;        //saved user context during signal handler
    uint32_t sig_delivering;            //signal currently being delivered
    bool sig_in_handler;                //currently in a signal handler

    //credentials
    uint32_t uid;                 //real uid
    uint32_t gid;                 //real gid
    uint32_t euid;                //effective uid
    uint32_t egid;                //effective gid
    uint32_t umask;               //process umask (permission mask)

    //dynamic linking context for this process (set by exec when PT_DYNAMIC present)
    dynlink_ctx_t dlctx;

    //wait-queue linkage (for blocking on events)
    struct process* wait_next;   //next entry in a wait queue
    wait_queue_t*   waiting_on;  //queue this process is sleeping on (NULL if none)
} process_t;

//process manager functions
void process_init(void);
process_t* process_create(const char* name, void* entry_point, bool user_mode);
void process_destroy(process_t* proc);
process_t* process_get_current(void);
process_t* process_get_by_pid(uint32_t pid);
uint32_t process_get_next_pid(void);
void process_yield(void);
void process_exit(int exit_code);
void process_sleep(uint32_t ticks);
void process_wake(process_t* proc);

//wait queue API
void wait_queue_init(wait_queue_t* q);
void wait_queue_wake_all(wait_queue_t* q);
void wait_queue_wake_one(wait_queue_t* q);
void process_wait_on(wait_queue_t* q);

//context switching
void context_switch(process_t* old_proc, process_t* new_proc);
void process_capture_irq_context(uint32_t* irq_stack_ptr);  //for preemptive switching
extern void switch_to_user_mode(uint32_t eip, uint32_t esp);

//process list management
extern process_t process_table[MAX_PROCESSES];
extern process_t* current_process;
extern uint32_t next_pid;
extern volatile int g_preempt_needed;

#endif
