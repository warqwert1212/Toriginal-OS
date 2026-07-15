#include "interrupts.h"
#include "mouse.h"
#include "serial.h"
#include "apic.h"

static void mouse_irq_adapter(interrupt_frame_t *frame) {
    (void)frame;
    mouse_irq_handler();
}

void mouse_wire_init(void) {
    interrupts_register_handler(0x2C, mouse_irq_adapter);
    mouse_init();
    serial_puts("[MOUSE] IRQ12 wired to vector 0x2C.\n");

    if (apic_available()) {
        apic_route_irq(12, 0x2C);
        serial_puts("[MOUSE] IRQ12 routed via I/O APIC.\n");
    }
}

