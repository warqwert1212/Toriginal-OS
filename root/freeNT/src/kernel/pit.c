// ==============================================================================
// PIT.C - 8253/8254 Hardware Timer Driver
// ==============================================================================
#include "pit.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint64_t system_ticks = 0;

void init_pit(uint32_t frequency) {
    uint32_t divisor = 1193182 / frequency;

    // Send the command byte (0x36 sets square wave mode, non-binary)
    outb(0x43, 0x36);

    // Divisor must be sent byte-by-byte
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

uint64_t get_system_ticks(void) {
    return system_ticks;
}

// Will be directly called by the Interrupt Dispatcher on IRQ0
void pit_handler(void) {
    system_ticks++;
}