// ==============================================================================
// PIT.H - Programmable Interval Timer Subsystem
// ==============================================================================
#pragma once
#include <stdint.h>

void init_pit(uint32_t frequency);
void pit_handler(void);
uint64_t get_system_ticks(void);