#ifndef FREENT_TIMER_H
#define FREENT_TIMER_H

#include <stdint.h>

void timer_init(uint32_t frequency);
uint64_t timer_ticks(void);

#endif /* FREENT_TIMER_H */
