#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

struct process;

#define SCHED_DEFAULT_TIMESLICE 10
#define SCHED_AGING_BOOST       1
#define SCHED_AGING_MAX         32

#define SCHED_PRIORITY_MIN      0
#define SCHED_PRIORITY_MAX      7
#define SCHED_PRIORITY_LEVELS   (SCHED_PRIORITY_MAX - SCHED_PRIORITY_MIN + 1)
#define SCHED_PRIORITY_DEFAULT  3
#define SCHED_PRIORITY_KERNEL   0

void scheduler_init(void);
void schedule(void);
void scheduler_tick(void);
void scheduler_make_runnable(struct process* proc);
void scheduler_on_process_exit(struct process* proc);
void scheduler_idle_loop(void);
void scheduler_set_priority(struct process* proc, uint8_t level);
uint8_t scheduler_get_priority(const struct process* proc);

extern volatile int g_preempt_needed;

#endif
