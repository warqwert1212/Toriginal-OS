#include "interrupts.h"
#include "mouse.h"
#include "serial.h"

static void mouse_irq_adapter(interrupt_frame_t *frame) {
    (void)frame;
    mouse_irq_handler();
}

/*
 * mouse_wire_init — register the mouse IRQ handler and bring up the
 * PS/2 mouse driver itself.
 *
 * Must be called AFTER idt_init()/interrupts_init() (so vector 0x2C's
 * generic stub already exists and handlers[] is initialised), and
 * after interrupts_init() has unmasked IRQ2 + IRQ12 on the PICs.
 */
void mouse_wire_init(void) {
    interrupts_register_handler(0x2C, mouse_irq_adapter);
    mouse_init();
    serial_puts("[MOUSE] IRQ12 wired to vector 0x2C.\n");
}
