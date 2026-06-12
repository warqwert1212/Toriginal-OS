// ==============================================================================
// KEYBOARD_WIRE.C
// Wires the keyboard ISR stub into the IDT at vector 0x21 (IRQ1)
// Call keyboard_wire_idt() from kernel_init() after init_idt()
// ==============================================================================

#include <stdint.h>
#include "../include/keybord.h"

void print_serial(const char* str);

// ---------------------------------------------------------------------------
// IDT entry structure — matches your existing idt.c / drivers/idt.c layout
// ---------------------------------------------------------------------------
struct kbd_idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

// ---------------------------------------------------------------------------
// Your IDT is declared in idt.c — extern it here
// If your IDT array has a different name, change this to match
// ---------------------------------------------------------------------------
extern struct kbd_idt_entry idt[256];

// ---------------------------------------------------------------------------
// The assembly stub declared in keyboard_isr.S
// ---------------------------------------------------------------------------
extern void keyboard_isr_stub(void);

// ---------------------------------------------------------------------------
// Set a single IDT gate
// selector 0x08 = kernel code segment (matches your GDT)
// type_attr 0x8E = Present, Ring 0, 64-bit interrupt gate
// ---------------------------------------------------------------------------
static void kbd_idt_set_gate(uint8_t vector,
                              uint64_t handler,
                              uint16_t selector,
                              uint8_t  type_attr) {
    struct kbd_idt_entry *e = &idt[vector];
    e->offset_low  = (uint16_t)(handler & 0xFFFF);
    e->offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    e->offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    e->selector    = selector;
    e->ist         = 0;
    e->type_attr   = type_attr;
    e->zero        = 0;
}

// ---------------------------------------------------------------------------
// Wire keyboard into IDT and initialize the driver
// Call AFTER init_idt() and BEFORE interrupts_enable()
// ---------------------------------------------------------------------------
void keyboard_wire_idt(void) {
    // IRQ1 maps to interrupt vector 0x21 (PIC1 base 0x20 + IRQ line 1)
    kbd_idt_set_gate(0x21,
                     (uint64_t)keyboard_isr_stub,
                     0x08,       // Kernel code segment selector
                     0x8E);      // Present | Ring 0 | 64-bit interrupt gate

    print_serial("[KBD] IRQ1 wired to IDT vector 0x21.\n");

    // Now initialize the PS/2 controller and keyboard hardware
    keyboard_init();
}