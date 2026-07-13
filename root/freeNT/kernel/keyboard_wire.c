/* =============================================================================
 * keyboard_wire.c - registers keyboard shit

 * ========================================================================= */

#include <stdint.h>
#include "idt.h"
#include "keyboard.h"
#include "serial.h"

extern void keyboard_isr_stub(void);

void keyboard_wire_idt(void)
{
    idt_set_gate(0x21,
                 (uint64_t)(uintptr_t)keyboard_isr_stub,
                 0x08,
                 0x8E);

    serial_puts("[KBD] IRQ1 wired to IDT vector 0x21.\n");

    keyboard_init();
}