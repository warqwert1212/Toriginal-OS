#ifndef IDT_H
#define IDT_H

#include <stdint.h>
#include "interrupts.h"   /* owns interrupt_frame_t, interrupt_handler_t */

/* ── IDT gate descriptor — 16 bytes ─────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} idt_entry_t;

/* ── IDT pointer — passed to load_idt() ─────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idt_ptr_t;

/* ── Gate flags ──────────────────────────────────────────────────────────── */
#define IDT_INTERRUPT_GATE  0x8E
#define IDT_TRAP_GATE       0x8F
#define IDT_USER_GATE       0xEE

/* ── Public API ──────────────────────────────────────────────────────────── */
void idt_init(void);
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t sel, uint8_t flags);

#endif /* IDT_H */
