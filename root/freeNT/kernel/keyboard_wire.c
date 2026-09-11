
#include <stdint.h>
#include "interrupts.h"
#include "keyboard.h"
#include "serial.h"
#include "apic.h"

static void keyboard_irq_adapter(interrupt_frame_t *frame)
{
    (void)frame;
    keyboard_irq_handler();
}

void keyboard_wire_idt(void)
{
    interrupts_register_handler(0x21, keyboard_irq_adapter);
    keyboard_init();
    interrupts_unmask_irq(1);
    serial_puts("[KBD] IRQ1 registered on the shared dispatcher.\n");

    if (apic_available()) {
        apic_route_irq(1, 0x21);
        serial_puts("[KBD] IRQ1 routed via I/O APIC.\n");
    }
}
