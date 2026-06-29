#include "pit.h"

static volatile uint64_t pit_ticks = 0;
static uint32_t pit_frequency = 100;

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile(
        "outb %0,%1"
        :
        : "a"(value),
          "Nd"(port));
}

void pit_init(uint32_t frequency)
{
    if (frequency == 0)
        frequency = 100;

    pit_frequency = frequency;

    uint16_t divisor =
        (uint16_t)(PIT_BASE_FREQUENCY / frequency);

    outb(0x43, 0x36);

    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);

    pit_ticks = 0;
}

void pit_tick(void)
{
    pit_ticks++;
}

uint64_t pit_get_ticks(void)
{
    return pit_ticks;
}

uint64_t pit_get_milliseconds(void)
{
    return (pit_ticks * 1000ULL) / pit_frequency;
}

void pit_sleep(uint64_t milliseconds)
{
    uint64_t start =
        pit_get_milliseconds();

    while ((pit_get_milliseconds() - start)
            < milliseconds)
    {
        __asm__ volatile("pause");
    }
}
