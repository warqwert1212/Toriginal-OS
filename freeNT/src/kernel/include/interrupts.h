#ifndef _KERNEL_INTERRUPTS_H
#define _KERNEL_INTERRUPTS_H

#include "types.h"

/* IDT Entry */
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

/* IDT Pointer */
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

/* Interrupt context (pushed on stack by CPU) */
typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t interrupt_number;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

/* Interrupt handler function type */
typedef void (*interrupt_handler_t)(interrupt_frame_t *frame);

/* Initialize interrupt handling */
void interrupts_init(void);
void idt_init(void);

/* Register interrupt handler */
void interrupts_register_handler(uint32_t interrupt_num, interrupt_handler_t handler);

/* Enable/disable interrupts */
void interrupts_enable(void);
void interrupts_disable(void);

/* Specific interrupt handlers */
void int_handler_exception(interrupt_frame_t *frame);
void int_handler_irq(interrupt_frame_t *frame);
void int_handler_syscall(interrupt_frame_t *frame);

#endif /* _KERNEL_INTERRUPTS_H */
