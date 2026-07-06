#include <stdint.h>
#include "idt.h"
#include "keyboard.h"

/* serial_puts is declared in serial.h (implemented in serial.c) */
#include "serial.h"

/* keyboard_isr_stub is defined in keyboard_isr.s */
extern void keyboard_isr_stub(void);

void keyboard_wire_idt(void)
{
    /* 1. Safely bind the handler stub to the IDT structure vector */
    idt_set_gate(0x21,
                 (uint64_t)(uintptr_t)keyboard_isr_stub,
                 0x08,
                 0x8E);

    /* 
     * HARDWARE MEMORY BUS SYNC:
     * Forces the x86 CPU to completely write the IDT gate pointer to physical memory. 
     * Without this, turning serial off allows the processor to execute keyboard_init() 
     * out-of-order, unmasking the PIC while the IDT vector is still uninitialized.
     */
    __asm__ volatile("mfence" ::: "memory");

    serial_puts("[KBD] IRQ1 wired to IDT vector 0x21.\n");

    /* 2. Now it is completely safe to initialize the controller and unmask the PIC line */
    keyboard_init();
    
    /* 3. Final safety flush to ensure cache state lines match memory */
    __asm__ volatile("mfence" ::: "memory");
}
