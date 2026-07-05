#include <stdint.h>
#include "idt.h"
#include "keyboard.h"

/* serial_puts is declared in serial.h (implemented in serial.c) */
#include "serial.h"

/* keyboard_isr_stub is defined in keyboard_isr.s */
extern void keyboard_isr_stub(void);


void keyboard_wire_idt(void)
{
    idt_set_gate(0x21,
                 (uint64_t)(uintptr_t)keyboard_isr_stub,
                 0x08,
                 0x8E);

    serial_puts("[KBD] IRQ1 wired to IDT vector 0x21.\n");

    /* Initialise the PS/2 controller and keyboard hardware */
    keyboard_init();
}