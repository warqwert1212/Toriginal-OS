/* =============================================================================
 * keyboard_wire.c - registers keyboard_isr_stub in the IDT at vector 0x21
 * (IRQ1) and brings up the keyboard driver itself.
 *
 * Must be called AFTER idt_init()/interrupts_init() (so the IDT table
 * itself exists and the PIC has already been remapped) and BEFORE the
 * CPU's interrupt flag is enabled with sti.
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