#ifndef PIT_H
#define PIT_H

#include <stdint.h>

/* PIT hardware frequency */
#define PIT_BASE_FREQUENCY 1193182

/* Initialize PIT to desired frequency */
void pit_init(uint32_t frequency);

/* Called from IRQ0 handler */
void pit_tick(void);

/* Get total uptime ticks */
uint64_t pit_get_ticks(void);

/* Milliseconds since boot */
uint64_t pit_get_milliseconds(void);

/* Busy wait delay */
void pit_sleep(uint64_t milliseconds);

#endif