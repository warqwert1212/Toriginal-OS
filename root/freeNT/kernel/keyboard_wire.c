
#include <stdint.h>
#include "idt.h"
#include "keyboard.h"
#include "serial.h"
#include "apic.h"

extern void keyboard_isr_stub(void);

void keyboard_wire_idt(void)
{
    idt_set_gate(0x21,
                 (uint64_t)(uintptr_t)keyboard_isr_stub,
                 0x08,
                 0x8E);

    serial_puts("[KBD] IRQ1 wired to IDT vector 0x21.\n");

    keyboard_init();

    if (apic_available()) {
        apic_route_irq(1, 0x21);
        serial_puts("[KBD] IRQ1 routed via I/O APIC.\n");
    }
}
