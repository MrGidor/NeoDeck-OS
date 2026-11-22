#ifndef TIMER_H
#define TIMER_H

#include "../include/types.h"

#define TIMER_HZ          100
#define TIMER_MS_PER_TICK (1000 / TIMER_HZ)
static_assert(1000 % TIMER_HZ == 0, "TIMER_HZ must divide 1000");

void     timer_init();
uint32_t timer_ticks();
uint32_t timer_uptime_ms();            /* wraps after ~49 days */
void     timer_sleep_ms(uint32_t ms);  /* sleeps >= ms; needs interrupts on */

#endif
