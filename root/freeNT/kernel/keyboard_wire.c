#include <stdint.h>
#include "idt.h"
#include "keybord.h"

/* serial_puts is defined in serial.c */
extern void serial_puts(const char *str);

/* keyboard_isr_stub is defined in keyboard_isr.s */
extern void keyboard_isr_stub(void);

/*
 * keyboard_wire_idt — register IRQ1 in the IDT and initialise the driver.
 *
 * Must be called AFTER idt_init() / interrupts_init() and BEFORE
 * interrupts_enable() (__asm__ volatile("sti")).
 */
void keyboard_wire_idt(void)
{
    /*
     * IRQ1 maps to IDT vector 0x21:
     *   PIC1 is remapped to base 0x20 in interrupts.c → IRQ1 = 0x21.
     * selector 0x08  = kernel code segment (second GDT entry).
     * flags    0x8E  = Present | Ring-0 | 64-bit interrupt gate.
     */
    idt_set_gate(0x21,
                 (uint64_t)(uintptr_t)keyboard_isr_stub,
                 0x08,
                 0x8E);

    serial_puts("[KBD] IRQ1 wired to IDT vector 0x21.\n");

    /* Initialise the PS/2 controller and keyboard hardware */
    keyboard_init();
}