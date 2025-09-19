#include "process.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "interrupts/tss.h"
#include "device_manager.h"
#include "drivers/tty.h"
#include "drivers/timer.h"
#include "drivers/serial.h"
#include <string.h>
#include <stddef.h>

//forward declaration for assembly functions
extern void context_switch_asm(cpu_context_t* old_context, cpu_context_t* new_context);

//global process management variables
process_t process_table[MAX_PROCESSES];
process_t* current_process = NULL;
uint32_t next_pid = 1;
static uint32_t scheduler_ticks = 0;

//time slice for round-robin scheduling (in timer ticks)
#define TIME_SLICE_TICKS 10

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
    kernel_proc->priority = 0;
    kernel_proc->time_slice = TIME_SLICE_TICKS;
    
    current_process = kernel_proc;
    next_pid = 1;
}

//translate a VA to PA in a given page directory without switching CR3
static uint32_t va_to_pa_in_dir(page_directory_t directory, uint32_t va) {
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
            process_destroy(p);
        }
    }
}

process_t* process_create(const char* name, void* entry_point, bool user_mode) {
    serial_write_string("[PROC] create begin\n");
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
    
    if (!proc) { serial_write_string("[PROC] no free slot\n"); return NULL; }  //no free slots
    
    //initialize process
    memset(proc, 0, sizeof(process_t));
    proc->pid = process_get_next_pid();
    proc->ppid = current_process ? current_process->pid : 0;
    proc->state = PROC_EMBRYO;
    strncpy(proc->name, name, PROCESS_NAME_MAX - 1);
    proc->priority = 1;  //default priority
    proc->time_slice = TIME_SLICE_TICKS;
    
    //set up memory space
    if (user_mode) {
        serial_write_string("[PROC] user path\n");
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
        serial_write_string("[PROC] kmalloc kstack (user)\n");
        void* kstk_base = kmalloc(KERNEL_STACK_SIZE);
        if (!kstk_base) {
            serial_write_string("[PROC] kmalloc kstack failed (user)\n");
            vmm_destroy_directory(proc->page_directory);
            proc->state = PROC_UNUSED;
            return NULL;
        }
        serial_write_string("[PROC] kstack ok (user)\n");
        proc->kernel_stack = (uint32_t)kstk_base + KERNEL_STACK_SIZE; //top of stack
        
        //set up user stack (top of user space)
        proc->user_stack_top = USER_VIRTUAL_END;
        serial_write_string("[PROC] alloc user stack phys\n");
        uint32_t user_stack_phys = pmm_alloc_page();
        if (!user_stack_phys) {
            //free previously allocated kernel stack
            kfree((void*)(proc->kernel_stack - KERNEL_STACK_SIZE));
            vmm_destroy_directory(proc->page_directory);
            proc->state = PROC_UNUSED;
            return NULL;
        }
        serial_write_string("[PROC] user stack phys ok\n");
        
        //map user stack
        serial_write_string("[PROC] map user stack\n");
        vmm_map_page_in_directory(proc->page_directory, 
                                USER_VIRTUAL_END - 0x1000, 
                                user_stack_phys, 
                                PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        
        //set up heap
        proc->heap_start = USER_VIRTUAL_START + 0x100000;  //1MB offset
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
        serial_write_string("[PROC] kernel path\n");
        //kernel process use kernel page directory
        proc->page_directory = vmm_get_kernel_directory();
        serial_write_string("[PROC] kmalloc kstack (kern)\n");
        void* kstk_base_k = kmalloc(KERNEL_STACK_SIZE);
        if (!kstk_base_k) {
            serial_write_string("[PROC] kmalloc kstack failed (kern)\n");
            proc->state = PROC_UNUSED;
            return NULL;
        }
        proc->kernel_stack = (uint32_t)kstk_base_k + KERNEL_STACK_SIZE;
        serial_write_string("[PROC] kstack ok (kern)\n");
        
        //initialize CPU context for kernel mode
        proc->context.eip = (uint32_t)entry_point;
        proc->context.esp = proc->kernel_stack - 16;
        proc->context.ebp = proc->context.esp;
        proc->context.eflags = 0x202;  //interrupts enabled
        proc->context.cs = 0x08;  //kernel code segment
        proc->context.ds = proc->context.es = proc->context.fs = proc->context.gs = 0x10;  //kernel data segment
        proc->context.ss = 0x10;  //kernel stack segment
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
    
    proc->state = PROC_RUNNABLE;
    serial_write_string("[PROC] create end\n");
    return proc;
}

void process_destroy(process_t* proc) {
    if (!proc || proc->state == PROC_UNUSED) return;
    
    //free memory resources
    if (proc->page_directory != vmm_get_kernel_directory()) {
        vmm_destroy_directory(proc->page_directory);
    }
    
    if (proc->kernel_stack) {
        //kernel_stack stores the top-of-stack (virtual). Free the base.
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

void scheduler_init(void) {
    //scheduler is initialized when process_init() is called
}

//round-robin scheduler
void schedule(void) {
    if (!current_process) return;
    
    //reap defunct processes whenever we enter the scheduler (safe to free non-current)
    process_reap_zombies();

    process_t* next = NULL;
    
    //find next runnable process
    int start_idx = (current_process - process_table + 1) % MAX_PROCESSES;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        int idx = (start_idx + i) % MAX_PROCESSES;
        if (process_table[idx].state == PROC_RUNNABLE) {
            next = &process_table[idx];
            break;
        }
    }
    
    //if no runnable process found stay with current (or kernel)
    if (!next) {
        if (current_process->state == PROC_RUNNING) {
            current_process->time_slice = TIME_SLICE_TICKS;  //reset time slice
            return;
        }
        //current process is not running, switch to kernel process
        next = &process_table[0];  //kernel process
    }
    
    //context switch if different process
    if (next != current_process) {
        process_t* old = current_process;
        if (old->state == PROC_RUNNING) {
            old->state = PROC_RUNNABLE;
        }

        next->state = PROC_RUNNING;
        next->time_slice = TIME_SLICE_TICKS;

        //update current before switching (important if we never return here)
        current_process = next;

        //NOTE: CR3 switching is disabled for stability processes run under the kernel page directory
        //TODO: re-enable per-process CR3 once syscall/IRQ paths are fully verified under user PDs

        //enter user or kernel via context switch (iret path handles CPL3 transition)
        if ((next->context.cs & 3) == 3) {
            if (!next->started) {
                next->started = true;
            }
            //ensure TSS uses this process's kernel stack for privilege transitions
            tss_set_kernel_stack(next->kernel_stack);
            context_switch(old, next);
        } else {
            //switching to a kernel target no privilege change
            context_switch(old, next);
        }
    }
}

void process_yield(void) {
    if (current_process) {
        current_process->time_slice = 0;  //force reschedule
    }
    schedule();
}

void process_exit(int exit_code) {
    if (!current_process || current_process->pid == 0) return;  //cn't exit kernel
    
    current_process->exit_code = exit_code;
    current_process->state = PROC_ZOMBIE;
    
    //wake up parent if waiting
    if (current_process->parent) {
        process_wake(current_process->parent);
    }
    
    schedule();  //switch to another process
}

void process_sleep(uint32_t ticks) {
    if (!current_process) return;
    
    current_process->state = PROC_SLEEPING;
    //TODO: Add to sleep queue with wake time
    
    schedule();
}

void process_wake(process_t* proc) {
    if (proc && proc->state == PROC_SLEEPING) {
        proc->state = PROC_RUNNABLE;
    }
}

//called by timer interrupt to handle time slicing
void process_timer_tick(void) {
    scheduler_ticks++;
    if (current_process && current_process->time_slice > 0) {
        current_process->time_slice--;
        if (current_process->time_slice == 0) {
            //cooperative for now: don't preempt inside IRQ context
            current_process->time_slice = TIME_SLICE_TICKS;
        }
    }
}

//context switching function
void context_switch(process_t* old_proc, process_t* new_proc) {
    //note: keep using the kernel page directory for now (no CR3 switch)
    //this mirrors the previously working behavior to avoid triple faults
    //need to stabilize per-process address space
    (void)old_proc; (void)new_proc;
    
    //save old context and restore new context using assembly
    context_switch_asm(&old_proc->context, &new_proc->context);
}
