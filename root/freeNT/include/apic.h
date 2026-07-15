#ifndef APIC_H
#define APIC_H

#include <stdint.h>

int apic_available(void);

void apic_init(void);

void apic_route_irq(uint8_t irq, uint8_t vector);

void apic_send_eoi(void);

#endif

