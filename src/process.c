#include "process.h"
#include "scheduler.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "interrupts/tss.h"
#include "device_manager.h"
#include "drivers/tty.h"
#include "drivers/timer.h"
#include "drivers/serial.h"
#include "debug.h"
#include "fd.h"
#include <string.h>
#include <stddef.h>

//forward declaration for assembly functions
extern void context_switch_asm(cpu_context_t* old_context, cpu_context_t* new_context);

//global process management variables
process_t process_table[MAX_PROCESSES];
process_t* current_process = NULL;
uint32_t next_pid = 1;
void process_init(void) {
    //initialize process table
    memset(process_table, 0, sizeof(process_table));

    //create kernel process (PID 0)
    process_t* kernel_proc = &process_table[0];
    kernel_proc->pid = 0;
    kernel_proc->ppid = 0;
    kernel_proc->state = PROC_RUNNING;
    strcpy(kernel_proc->name, "kernel");
    kernel_proc->page_directory = vmm_get_kernel_directory();
    kernel_proc->aging_score = 0;
    scheduler_set_priority(kernel_proc, SCHED_PRIORITY_KERNEL);
    kernel_proc->priority = kernel_proc->base_priority;
    kernel_proc->time_slice = SCHED_DEFAULT_TIMESLICE;
    //root credentials
    kernel_proc->uid = 0;
    kernel_proc->gid = 0;
    kernel_proc->euid = 0;
    kernel_proc->egid = 0;
    kernel_proc->umask = 0022;
    //kernel current working directory is root  
    strcpy(kernel_proc->cwd, "/");

    //allocate a dedicated kernel stack and initialize kernel CPU context
    void* kstk_base = kmalloc(KERNEL_STACK_SIZE);
    if (kstk_base) {
        kernel_proc->kernel_stack = (uint32_t)kstk_base + KERNEL_STACK_SIZE;
        kernel_proc->kcontext.eip = (uint32_t)scheduler_idle_loop;
        kernel_proc->kcontext.esp = kernel_proc->kernel_stack - 16;
        kernel_proc->kcontext.eflags = 0x202;
        kernel_proc->kcontext.cs = 0x08;
        kernel_proc->kcontext.ds = kernel_proc->kcontext.es = kernel_proc->kcontext.fs = kernel_proc->kcontext.gs = 0x10;
        kernel_proc->kcontext.ss = 0x10;
        //prepare a fake call frame: [EBP]=0, [RET]=kernel_idle so 'pop ebp; ret' works
        uint32_t* ksp = (uint32_t*)kernel_proc->kcontext.esp;
        ksp -= 2;              //make room for EBP and RET
        ksp[0] = 0;            //fake EBP
        ksp[1] = kernel_proc->kcontext.eip;  //return address -> kernel_idle
        kernel_proc->kcontext.esp = (uint32_t)ksp;
        //EBP field must point to the location of the saved EBP on the stack
        kernel_proc->kcontext.ebp = (uint32_t)ksp;
    }

    current_process = kernel_proc;
    next_pid = 1;
    scheduler_init();
}

//translate a VA to PA in a given page directory without switching CR3
static __attribute__((unused)) uint32_t va_to_pa_in_dir(page_directory_t directory, uint32_t va) {
    if (!directory) return 0;
    #define PD_INDEX(addr) ((addr) >> 22)
    #define PT_INDEX(addr) (((addr) >> 12) & 0x3FF)
    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);
    if (!(directory[pdi] & PAGE_PRESENT)) return 0;
    uint32_t pt_phys = directory[pdi] & ~0xFFF;
    page_table_t pt = (page_table_t)PHYSICAL_TO_VIRTUAL(pt_phys);
    uint32_t pte = pt[pti];
    if (!(pte & PAGE_PRESENT)) return 0;
    return (pte & ~0xFFF) | (va & 0xFFF);
}

uint32_t process_get_next_pid(void) {
    //reuse the smallest free positive PID (>=1)
    for (uint32_t pid = 1; pid < MAX_PROCESSES; ++pid) {
        bool in_use = false;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (process_table[i].state != PROC_UNUSED && process_table[i].pid == pid) {
                in_use = true;
                break;
            }
        }
        if (!in_use) {
            next_pid = pid + 1; //hint for next allocation
            if (next_pid >= MAX_PROCESSES) next_pid = 1;
            return pid;
        }
    }
    //fallback monotonically allocate (should not happen under normal table size)
    uint32_t pid = next_pid++;
    if (next_pid >= MAX_PROCESSES) next_pid = 1;
    return pid;
}

process_t* process_get_current(void) {
    return current_process;
}

process_t* process_get_by_pid(uint32_t pid) {
    if (pid >= MAX_PROCESSES) return NULL;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROC_UNUSED && process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return NULL;
}

//destroy all zombie processes except the current one
void process_reap_zombies(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* p = &process_table[i];
        if (p->state == PROC_ZOMBIE && p != current_process) {
            //do NOT reap zombies that still have a living parent
            //they must be reaped by their parent via sys_wait()
            if (p->parent == NULL || p->parent->state == PROC_UNUSED) {
                process_destroy(p);
            }
        }
    }
}

process_t* process_create(const char* name, void* entry_point, bool user_mode) {
    #if LOG_PROC
    serial_write_string("[PROC] create begin\n");
    #endif
    //clean up any defunct processes so their slots/PIDs are reusable
    process_reap_zombies();
    //find free process slot
    process_t* proc = NULL;
    for (int i = 1; i < MAX_PROCESSES; i++) {  //skip slot 0 where kernel is
        if (process_table[i].state == PROC_UNUSED) {
            proc = &process_table[i];
            break;
        }
    }

    if (!proc) { 
        #if LOG_PROC
        serial_write_string("[PROC] no free slot\n");
        #endif
        return NULL; 
    }  //no free slots

    //initialize process
    memset(proc, 0, sizeof(process_t));
    proc->pid = process_get_next_pid();
    proc->ppid = current_process ? current_process->pid : 0;
    //inherit credentials
    if (current_process) {
        proc->uid = current_process->uid;
        proc->gid = current_process->gid;
        proc->euid = current_process->euid;
        proc->egid = current_process->egid;
        proc->umask = current_process->umask;
    } else {
        proc->uid = proc->gid = proc->euid = proc->egid = 0;
        proc->umask = 0022;
    }
    proc->state = PROC_EMBRYO;
    strncpy(proc->name, name, PROCESS_NAME_MAX - 1);
    proc->aging_score = 0;
    uint8_t sched_level = user_mode ? SCHED_PRIORITY_DEFAULT : SCHED_PRIORITY_KERNEL;
    scheduler_set_priority(proc, sched_level);
    proc->priority = proc->base_priority;
    //inherit cwd from parent if available else set to '/'
    if (current_process && current_process->cwd[0]) {
        strncpy(proc->cwd, current_process->cwd, sizeof(proc->cwd) - 1);
        proc->cwd[sizeof(proc->cwd) - 1] = '\0';
    } else {
        strcpy(proc->cwd, "/");
    }
    proc->time_slice = SCHED_DEFAULT_TIMESLICE;

    //set up memory space
    if (user_mode) {
        #if LOG_PROC
        serial_write_string("[PROC] user path\n");
        #endif
        //create new page directory for user process
        proc->page_directory = vmm_create_directory();
        if (!proc->page_directory) {
            proc->state = PROC_UNUSED;
            return NULL;
        }

        //map kernel space into user process
        vmm_map_kernel_space(proc->page_directory);

        //ensure VGA text buffer is mapped so kernel can render panics/prints under this address space
        vmm_map_page_in_directory(proc->page_directory,
                                  0x000B8000,
                                  0x000B8000,
                                  PAGE_PRESENT | PAGE_WRITABLE);

        //allocate kernel stack (kernel virtual memory)
        #if LOG_PROC
        serial_write_string("[PROC] kmalloc kstack (user)\n");
        #endif
        void* kstk_base = kmalloc(KERNEL_STACK_SIZE);
        if (!kstk_base) {
            serial_write_string("[PROC] kmalloc kstack failed (user)\n");
            vmm_destroy_directory(proc->page_directory);
            proc->state = PROC_UNUSED;
            return NULL;
        }
        #if LOG_PROC
        serial_write_string("[PROC] kstack ok (user)\n");
        #endif
        proc->kernel_stack = (uint32_t)kstk_base + KERNEL_STACK_SIZE; //top of stack

        //set up user stack (top of user space)
        proc->user_stack_top = USER_VIRTUAL_END;
        #if LOG_PROC
        serial_write_string("[PROC] alloc user stack phys\n");
        #endif
        uint32_t user_stack_phys = pmm_alloc_page();
        if (!user_stack_phys) {
            //free previously allocated kernel stack
            kfree((void*)(proc->kernel_stack - KERNEL_STACK_SIZE));
            vmm_destroy_directory(proc->page_directory);
            proc->state = PROC_UNUSED;
            return NULL;
        }
        #if LOG_PROC
        serial_write_string("[PROC] user stack phys ok\n");
        #endif

        //map user stack - allocate 8KB (2 pages)
        #if LOG_PROC
        serial_write_string("[PROC] map user stack (8KB)\n");
        #endif
        #define USER_STACK_PAGES 2
        uint32_t stack_phys_arr[USER_STACK_PAGES];
        memset(stack_phys_arr, 0, sizeof(stack_phys_arr));
        for (int i = 0; i < USER_STACK_PAGES; i++) {
            uint32_t stack_page_phys = (i == 0) ? user_stack_phys : pmm_alloc_page();
            if (!stack_page_phys) {
                serial_write_string("[PROC] failed to alloc stack page\n");
                //cleanup: free kernel stack and already allocated stack pages
                kfree((void*)(proc->kernel_stack - KERNEL_STACK_SIZE));
                for (int j = 0; j < i; j++) {
                    if (stack_phys_arr[j]) pmm_free_page(stack_phys_arr[j]);
                }
                vmm_destroy_directory(proc->page_directory);
                proc->state = PROC_UNUSED;
                return NULL;
            }
            stack_phys_arr[i] = stack_page_phys;
            vmm_map_page_in_directory(proc->page_directory,
                                    USER_VIRTUAL_END - ((i + 1) * 0x1000),
                                    stack_page_phys,
                                    PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }


        //set up heap: place outside 0..8MB identity region so PDEs can be user-accessible
        proc->heap_start = 0x03000000;  //USER_HEAP_BASE (must match sys_brk/sbrk)
        proc->heap_end = proc->heap_start;

        //initialize CPU context for user mode
        proc->context.eip = (uint32_t)entry_point;
        proc->context.esp = USER_VIRTUAL_END - 16;  //keave some space
        proc->context.ebp = proc->context.esp;
        proc->context.eflags = 0x202;  //interrupts enabled
        proc->context.cs = 0x1B;  //user code segment (GDT entry 3, RPL=3)
        proc->context.ds = proc->context.es = proc->context.fs = proc->context.gs = 0x23;  //user data segment
        proc->context.ss = 0x23;  //user stack segment
        proc->user_eip = (uint32_t)entry_point;

        //set controlling TTY and default mode (canonical + echo)
        proc->tty = device_find_by_name("tty0");
        proc->tty_mode = TTY_MODE_CANON | TTY_MODE_ECHO;

    } else {
        #if LOG_PROC
        serial_write_string("[PROC] kernel path\n");
        #endif
        //kernel process use kernel page directory
        proc->page_directory = vmm_get_kernel_directory();
        #if LOG_PROC
        serial_write_string("[PROC] kmalloc kstack (kern)\n");
        #endif
        void* kstk_base_k = kmalloc(KERNEL_STACK_SIZE);
        if (!kstk_base_k) {
            serial_write_string("[PROC] kmalloc kstack failed (kern)\n");
            proc->state = PROC_UNUSED;
            return NULL;
        }
        proc->kernel_stack = (uint32_t)kstk_base_k + KERNEL_STACK_SIZE;
        #if LOG_PROC
        serial_write_string("[PROC] kstack ok (kern)\n");
        #endif

        //initialize CPU context for kernel mode
        proc->context.eip = (uint32_t)entry_point;
        //build a fake call frame so kernel restore via 'pop ebp; ret' works
        uint32_t* ksp = (uint32_t*)(proc->kernel_stack - 16);
        ksp -= 2;                 //space for [EBP] and [RET]
        ksp[0] = 0;               //fake saved EBP
        ksp[1] = proc->context.eip; //return address -> entry_point
        proc->context.esp = (uint32_t)ksp;
        proc->context.ebp = (uint32_t)ksp;
        proc->context.eflags = 0x202;  //interrupts enabled
        proc->context.cs = 0x08;  //kernel code segment
        proc->context.ds = proc->context.es = proc->context.fs = proc->context.gs = 0x10;  //kernel data segment
        proc->context.ss = 0x10;  //kernel stack segment

        //mirror into kcontext (we use kcontext when restoring kernel targets)
        proc->kcontext = proc->context;
    }

    //set parent-child relationship
    if (current_process) {
        proc->parent = current_process;
        proc->sibling = current_process->children;
        current_process->children = proc;
    }

    //initialize file descriptor table
    for (int i = 0; i < 16; i++) {
        proc->fd_table[i] = -1;  //all closed initially
    }

    //set up stdio for user processes if possible (/dev/tty0)
    if (user_mode) {
        fd_init_process_stdio(proc);
    }

    proc->state = PROC_RUNNABLE;
    scheduler_make_runnable(proc);
    #if LOG_PROC
    serial_write_string("[PROC] create end\n");
    #endif
    return proc;
}

void process_destroy(process_t* proc) {
    if (!proc || proc->state == PROC_UNUSED) return;
    //if sleeping on a wait queue unlink
    if (proc->waiting_on) {
        uint32_t eflags_q;
        __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags_q) :: "memory");
        wait_queue_t* q = proc->waiting_on;
        process_t** pp = &q->head;
        while (*pp) {
            if (*pp == proc) {
                *pp = proc->wait_next;
                break;
            }
            pp = &((*pp)->wait_next);
        }
        proc->wait_next = NULL;
        proc->waiting_on = NULL;
        if (eflags_q & 0x200) __asm__ volatile ("sti");
    }

    //close all open file descriptors for this process
    fd_close_all_for(proc);

    //free memory resources
    if (proc->page_directory != vmm_get_kernel_directory()) {
        vmm_destroy_directory(proc->page_directory);
    }

    if (proc->kernel_stack) {
        //kernel_stack stores the top-of-stack (virtual) free the base
        kfree((void*)(proc->kernel_stack - KERNEL_STACK_SIZE));
    }

    //remove from parent child list
    if (proc->parent) {
        if (proc->parent->children == proc) {
            proc->parent->children = proc->sibling;
        } else {
            process_t* child = proc->parent->children;
            while (child && child->sibling != proc) {
                child = child->sibling;
            }
            if (child) {
                child->sibling = proc->sibling;
            }
        }
    }

    //mark as unused
    proc->state = PROC_UNUSED;
    memset(proc, 0, sizeof(process_t));
}

void process_yield(void) {
    if (current_process) {
        current_process->time_slice = 0;  //force reschedule
    }
    schedule();
}

void process_exit(int exit_code) {
    if (!current_process || current_process->pid == 0) return;  //cn't exit kernel

    //reparent all children to init (PID 1) before zombifying
    process_t* initp = process_get_by_pid(1);
    process_t* child = current_process->children;
    current_process->children = NULL;
    while (child) {
        process_t* next = child->sibling;
        //detach from current process
        child->parent = initp;
        child->ppid = initp ? initp->pid : 0;
        //prepend into init children list (if available)
        if (initp) {
            child->sibling = initp->children;
            initp->children = child;
        } else {
            child->sibling = NULL;
        }
        child = next;
    }

    current_process->exit_code = exit_code;
    current_process->state = PROC_ZOMBIE;

    if (current_process->parent) {
        #if LOG_PROC
        serial_write_string("[PROC_EXIT] wake parent pid=\n");
        serial_printf("%d", (int)current_process->parent->pid);
        serial_write_string(" child pid=\n");
        serial_printf("%d", (int)current_process->pid);
        serial_write_string(" free_pages=\n");
        serial_printf("%d", (int)pmm_get_free_pages());
        serial_write_string("\n");
        #endif
        process_wake(current_process->parent);
    }

    #if LOG_PROC
    serial_write_string("[PROC_EXIT] post-wake pid=\n");
    serial_printf("%d", (int)current_process->pid);
    serial_write_string(" free_pages=\n");
    serial_printf("%d", (int)pmm_get_free_pages());
    serial_write_string("\n");
    #endif

    scheduler_on_process_exit(current_process);
    schedule();  //switch to another process
}

void process_sleep(uint32_t ticks) {
    if (!current_process) return;
    //set absolute wakeup tick and sleep cooperatively
    uint64_t now = timer_get_ticks();
    current_process->wakeup_tick = (uint32_t)(now + ticks);
    current_process->state = PROC_SLEEPING;
    schedule();
}

void process_wake(process_t* proc) {
    if (proc && proc->state == PROC_SLEEPING) {
        #if LOG_PROC
        serial_write_string("[PROC_WAKE] pid=\n");
        serial_printf("%d", (int)proc->pid);
        serial_write_string(" free_pages=\n");
        serial_printf("%d", (int)pmm_get_free_pages());
        serial_write_string("\n");
        #endif
        scheduler_make_runnable(proc);
    }
}

//capture current CPU state from IRQ frame for preemptive context switch
//called when timer IRQ detects time slice expiry
void process_capture_irq_context(uint32_t* irq_stack_ptr) {
    process_t* cur = current_process;
    if (!cur || !irq_stack_ptr) return;
    
    //IRQ frame layout after pushad in irq0 stub (addresses relative to irq_stack_ptr):
    //[esp+0..31]: pushad saves EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX
    //[esp+32..]: CPU-pushed frame: [EIP][CS][EFLAGS] or [EIP][CS][EFLAGS][USERESP][SS]
    
    //extract pushad registers (in reverse order from pushad)
    uint32_t saved_eax = irq_stack_ptr[7];
    uint32_t saved_ecx = irq_stack_ptr[6];
    uint32_t saved_edx = irq_stack_ptr[5];
    uint32_t saved_ebx = irq_stack_ptr[4];
    //skip ESP at irq_stack_ptr[3] (original ESP before pushad)
    uint32_t saved_ebp = irq_stack_ptr[2];
    uint32_t saved_esi = irq_stack_ptr[1];
    uint32_t saved_edi = irq_stack_ptr[0];
    
    //extract CPU-pushed frame
    uint32_t saved_eip = irq_stack_ptr[8];
    uint32_t saved_cs = irq_stack_ptr[9];
    uint32_t saved_eflags = irq_stack_ptr[10];
    
    //check if this was a user->kernel transition (CS has RPL=3)
    if ((saved_cs & 3) == 3) {
        //user mode interrupt: CPU also pushed USERESP and SS
        uint32_t saved_useresp = irq_stack_ptr[11];
        uint32_t saved_ss = irq_stack_ptr[12];
        
        //save full user context so we can resume later
        cur->context.eax = saved_eax;
        cur->context.ebx = saved_ebx;
        cur->context.ecx = saved_ecx;
        cur->context.edx = saved_edx;
        cur->context.esi = saved_esi;
        cur->context.edi = saved_edi;
        cur->context.ebp = saved_ebp;
        cur->context.esp = saved_useresp;
        cur->context.eip = saved_eip;
        cur->context.cs = saved_cs;
        cur->context.ss = saved_ss;
        cur->context.eflags = saved_eflags;
        //ds/es/fs/gs are already user segments (0x23) in user mode
        cur->context.ds = cur->context.es = cur->context.fs = cur->context.gs = 0x23;
        cur->in_kernel = false;  //resuming to user mode
    }
}

//context switching function
void context_switch(process_t* old_proc, process_t* new_proc) {
    //save current kernel CPU state into old_proc->kcontext to avoid clobbering
    //the user-mode return frame stored in old_proc->context
    //if the process is currently inside a syscall (in_kernel) resume its kernel context
    //otherwise select user vs kernel by CS RPL
    cpu_context_t* new_ctx_ptr = (new_proc->in_kernel)
                                 ? &new_proc->kcontext
                                 : (((new_proc->context.cs & 3) == 3)
                                       ? &new_proc->context
                                       : &new_proc->kcontext);

    //switch CR3 to the appropriate page directory for PID 0 use kernel dir
    page_directory_t target_dir = (new_proc->pid == 0 || !new_proc->page_directory)
                                  ? vmm_get_kernel_directory()
                                  : new_proc->page_directory;
    vmm_switch_directory(target_dir);

    context_switch_asm(&old_proc->kcontext, new_ctx_ptr);
}

void wait_queue_init(wait_queue_t* q) {
    if (!q) return;
    q->head = NULL;
}

static inline void irq_save_cli(uint32_t* out_eflags) {
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(*out_eflags) :: "memory");
}
static inline void irq_restore(uint32_t eflags) {
    if (eflags & 0x200) __asm__ volatile ("sti");
}

void process_wait_on(wait_queue_t* q) {
    if (!q) { schedule(); return; }
    uint32_t ef; irq_save_cli(&ef);
    process_t* cur = current_process;
    if (!cur) { irq_restore(ef); return; }
    // insert at head (LIFO is fine for now)
    cur->wait_next = q->head;
    q->head = cur;
    cur->waiting_on = q;
    cur->state = PROC_SLEEPING;
    irq_restore(ef);
    // yield CPU until woken
    schedule();
}

void wait_queue_wake_all(wait_queue_t* q) {
    if (!q) return;
    uint32_t ef; irq_save_cli(&ef);
    process_t* p = q->head;
    q->head = NULL;
    while (p) {
        process_t* next = p->wait_next;
        p->wait_next = NULL;
        p->waiting_on = NULL;
        if (p->state == PROC_SLEEPING) scheduler_make_runnable(p);
        p = next;
    }
    irq_restore(ef);
}

void wait_queue_wake_one(wait_queue_t* q) {
    if (!q) return;
    uint32_t ef; irq_save_cli(&ef);
    process_t* p = q->head;
    if (p) {
        q->head = p->wait_next;
        p->wait_next = NULL;
        p->waiting_on = NULL;
        if (p->state == PROC_SLEEPING) scheduler_make_runnable(p);
    }
    irq_restore(ef);
}

