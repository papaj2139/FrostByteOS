#include "scheduler.h"
#include "process.h"
#include "drivers/timer.h"
#include "drivers/serial.h"
#include "interrupts/tss.h"
#include "debug.h"

#include <stddef.h>
#include <limits.h>
#include <stdint.h>

extern process_t process_table[MAX_PROCESSES];
extern process_t* current_process;

volatile int g_preempt_needed = 0;
static uint32_t scheduler_ticks = 0;

static const uint16_t weight_table[SCHED_PRIORITY_LEVELS] = {
    256, //0 highest
    224,
    192,
    160,
    128,
    112,
    96,
    80  //7 lowest
};

static inline uint16_t clamp_priority(uint8_t level) {
    if (level > SCHED_PRIORITY_MAX) return SCHED_PRIORITY_MAX;
    return level;
}

void scheduler_idle_loop(void) {
    for (;;) {
        __asm__ volatile ("sti; hlt");
        schedule();
    }
}

void scheduler_init(void) {
    scheduler_ticks = 0;
    g_preempt_needed = 0;
}

void scheduler_make_runnable(process_t* proc) {
    if (!proc) return;
    if (proc->state != PROC_RUNNABLE && proc->state != PROC_RUNNING) {
        proc->state = PROC_RUNNABLE;
    }
    if (proc->time_slice == 0 || proc->time_slice > SCHED_DEFAULT_TIMESLICE) {
        proc->time_slice = SCHED_DEFAULT_TIMESLICE;
    }
    if (proc->aging_score < 0) proc->aging_score = 0;
}

void scheduler_on_process_exit(process_t* proc) {
    (void)proc;
}

void scheduler_set_priority(process_t* proc, uint8_t level) {
    if (!proc) return;
    uint8_t clamped = (uint8_t)clamp_priority(level);
    proc->static_priority = clamped;
    proc->base_priority = clamped;
    proc->priority = clamped;
    proc->weight = weight_table[clamped];
}

uint8_t scheduler_get_priority(const process_t* proc) {
    if (!proc) return SCHED_PRIORITY_DEFAULT;
    return proc->static_priority;
}

static void ensure_idle_kcontext(process_t* proc) {
    if (!proc || proc->pid != 0) return;
    proc->kcontext.eip = (uint32_t)scheduler_idle_loop;
    uint32_t* ksp = (uint32_t*)(proc->kernel_stack - 16);
    ksp -= 2;
    ksp[0] = 0;
    ksp[1] = proc->kcontext.eip;
    proc->kcontext.esp = (uint32_t)ksp;
    proc->kcontext.ebp = (uint32_t)ksp;
    proc->kcontext.eflags = 0x202;
    proc->kcontext.cs = 0x08;
    proc->kcontext.ds = proc->kcontext.es = proc->kcontext.fs = proc->kcontext.gs = 0x10;
    proc->kcontext.ss = 0x10;
}

void schedule(void) {
    uint32_t eflags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags));

    if (!current_process) {
        if (eflags & 0x200) __asm__ volatile ("sti");
        return;
    }

    extern void process_reap_zombies(void);
    process_reap_zombies();

    process_t* next = NULL;
    int start_idx = (current_process - process_table + 1) % MAX_PROCESSES;
    int64_t best_score = 0;
    uint16_t best_weight = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        int idx = (start_idx + i) % MAX_PROCESSES;
        process_t* proc = &process_table[idx];
        if (proc->state != PROC_RUNNABLE) continue;
        uint16_t weight = proc->weight ? proc->weight : weight_table[SCHED_PRIORITY_DEFAULT];
        int64_t score = ((int64_t)proc->base_priority - (int64_t)proc->aging_score) * 1024;
        if (!next) {
            next = proc;
            best_score = score;
            best_weight = weight;
            continue;
        }
        //prefer lower score/weight ratio - compare cross products to avoid division
        int64_t lhs = score * best_weight;
        int64_t rhs = best_score * weight;
        if (lhs < rhs || (lhs == rhs && proc->pid < next->pid)) {
            next = proc;
            best_score = score;
            best_weight = weight;
        }
    }

    if (!next) {
        if (current_process->state == PROC_RUNNING) {
            current_process->time_slice = SCHED_DEFAULT_TIMESLICE;
            if (eflags & 0x200) __asm__ volatile ("sti");
            return;
        }
        next = &process_table[0];
    }

    if (next != current_process) {
        process_t* old = current_process;
        if (old->state == PROC_RUNNING) {
            old->state = PROC_RUNNABLE;
            if (old->aging_score < SCHED_AGING_MAX) {
                old->aging_score += SCHED_AGING_BOOST;
            }
        }

        next->state = PROC_RUNNING;
        next->time_slice = SCHED_DEFAULT_TIMESLICE;
        next->aging_score = 0;

        current_process = next;

        if ((next->context.cs & 3) == 3) {
            if (!next->started) next->started = true;
            tss_set_kernel_stack(next->kernel_stack);
            #if LOG_SCHED
            serial_write_string("[SCHED] switch ");
            serial_printf("%d", (int)old->pid);
            serial_write_string(" -> ");
            serial_printf("%d", (int)next->pid);
            serial_write_string(" ctx=user\n");
            #endif
            context_switch(old, next);
        } else {
            tss_set_kernel_stack(next->kernel_stack);
            ensure_idle_kcontext(next);
            #if LOG_SCHED
            serial_write_string("[SCHED] switch ");
            serial_printf("%d", (int)old->pid);
            serial_write_string(" -> ");
            serial_printf("%d", (int)next->pid);
            serial_write_string(" ctx=kernel\n");
            #endif
            context_switch(old, next);
        }
    }

    if (eflags & 0x200) __asm__ volatile ("sti");
}

void scheduler_tick(void) {
    scheduler_ticks++;
    uint64_t now = timer_get_ticks();

    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_t* proc = &process_table[i];
        if (proc->state == PROC_SLEEPING && proc->wakeup_tick != 0 && (uint32_t)now >= proc->wakeup_tick) {
            proc->wakeup_tick = 0;
            proc->in_kernel = false;
            proc->context.eax = 0;
            scheduler_make_runnable(proc);
        } else if (proc->state == PROC_RUNNABLE && proc != current_process) {
            if (proc->aging_score < SCHED_AGING_MAX) {
                proc->aging_score += SCHED_AGING_BOOST;
            }
        }
    }

    if (current_process && current_process->state == PROC_RUNNING) {
        if (current_process->time_slice > 0) {
            current_process->time_slice--;
        }
        if (current_process->time_slice == 0) {
            current_process->time_slice = SCHED_DEFAULT_TIMESLICE;
            bool has_other = false;
            for (int i = 0; i < MAX_PROCESSES; i++) {
                process_t* proc = &process_table[i];
                if (proc != current_process && proc->state == PROC_RUNNABLE) {
                    has_other = true;
                    break;
                }
            }
            if (has_other) {
                if (current_process->state == PROC_RUNNING) {
                    current_process->state = PROC_RUNNABLE;
                }
                g_preempt_needed = 1;
            }
        }
    }
}
