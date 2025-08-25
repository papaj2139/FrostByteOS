#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init(uint32_t frequency);
uint64_t timer_get_ticks(void);
uint32_t timer_get_frequency(void);

#endif