#include "process.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "interrupts/tss.h"
#include "drivers/timer.h"
#include <string.h>
#include <stddef.h>

//forward declaration for assembly function
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

uint32_t process_get_next_pid(void) {
    uint32_t pid = next_pid++;
    //wrap around if we hit max
    if (next_pid >= MAX_PROCESSES) {
        next_pid = 1;
    }
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

process_t* process_create(const char* name, void* entry_point, bool user_mode) {
    //find free process slot
    process_t* proc = NULL;
    for (int i = 1; i < MAX_PROCESSES; i++) {  //skip slot 0 where kernel is
        if (process_table[i].state == PROC_UNUSED) {
            proc = &process_table[i];
            break;
        }
    }
    
    if (!proc) return NULL;  //no free slots
    
    //initialize process
    memset(proc, 0, sizeof(process_t));
    proc->pid = process_get_next_pid();
    proc->ppid = current_process ? current_process->pid : 0;
    proc->state = PROC_EMBRYO;
    strncpy(proc->name, name, PROCESS_NAME_MAX - 1);
    proc->priority = 1;  // Default priority
    proc->time_slice = TIME_SLICE_TICKS;
    
    //set up memory space
    if (user_mode) {
        //create new page directory for user process
        proc->page_directory = vmm_create_directory();
        if (!proc->page_directory) {
            proc->state = PROC_UNUSED;
            return NULL;
        }
        
        //map kernel space into user process
        vmm_map_kernel_space(proc->page_directory);
        
        //allocate kernel stack
        uint32_t kernel_stack_phys = pmm_alloc_page();
        if (!kernel_stack_phys) {
            vmm_destroy_directory(proc->page_directory);
            proc->state = PROC_UNUSED;
            return NULL;
        }
        proc->kernel_stack = kernel_stack_phys;
        
        //set up user stack (top of user space)
        proc->user_stack_top = USER_VIRTUAL_END;
        uint32_t user_stack_phys = pmm_alloc_page();
        if (!user_stack_phys) {
            pmm_free_page(kernel_stack_phys);
            vmm_destroy_directory(proc->page_directory);
            proc->state = PROC_UNUSED;
            return NULL;
        }
        
        //map user stack
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
        
    } else {
        //kernel process use kernel page directory
        proc->page_directory = vmm_get_kernel_directory();
        proc->kernel_stack = (uint32_t)kmalloc(KERNEL_STACK_SIZE) + KERNEL_STACK_SIZE;
        
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
    return proc;
}

void process_destroy(process_t* proc) {
    if (!proc || proc->state == PROC_UNUSED) return;
    
    //free memory resources
    if (proc->page_directory != vmm_get_kernel_directory()) {
        vmm_destroy_directory(proc->page_directory);
    }
    
    if (proc->kernel_stack) {
        if (proc->page_directory != vmm_get_kernel_directory()) {
            pmm_free_page(proc->kernel_stack);
        } else {
            kfree((void*)(proc->kernel_stack - KERNEL_STACK_SIZE));
        }
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
        
        context_switch(old, next);
        current_process = next;
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
    
    //TODO: Wake up parent if waiting
    
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
            schedule();  //time slice expired reschedule
        }
    }
}

//context switching function
void context_switch(process_t* old_proc, process_t* new_proc) {
    //switch page directory
    vmm_switch_directory(new_proc->page_directory);
    
    //update TSS with new kernel stack
    tss_set_kernel_stack(new_proc->kernel_stack);
    
    //save old context and restore new context using assembly
    context_switch_asm(&old_proc->context, &new_proc->context);
}
