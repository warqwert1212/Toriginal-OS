#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

typedef struct interrupt_frame {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t interrupt_number;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

typedef void (*interrupt_handler_t)(interrupt_frame_t *);

void interrupts_init(void);
void interrupts_register_handler(uint32_t num, interrupt_handler_t handler);
void interrupts_enable(void);
void interrupts_disable(void);
void interrupts_unmask_irq(uint8_t irq);

#endif /* INTERRUPTS_H */
