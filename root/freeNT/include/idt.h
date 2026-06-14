#ifndef IDT_H
#define IDT_H

#include <stdint.h>

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

typedef idtr_t idt_ptr_t;

/* The one true IDT array — defined in interrupts.c, extern'd everywhere else */
extern idt_entry_t idt[256];

/* Single authoritative signature used by both interrupts.c and keyboard_wire.c */
void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t flags);

void idt_init(void);

#endif /* IDT_H */