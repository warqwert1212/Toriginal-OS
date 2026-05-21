#include "interrupts.h"
#include "io.h"
#include "string.h"

/* Interrupt handlers table */
static interrupt_handler_t handlers[256] = {0};

/* IDT storage */
static idt_entry_t idt[256] = {0};
static idt_ptr_t idt_ptr = {0};

/* Forward declarations for ASM functions */
extern void load_idt(idt_ptr_t *ptr);
extern void enable_interrupts(void);
extern void disable_interrupts(void);

/* Exception handler (CPU exceptions) */
void int_handler_exception(interrupt_frame_t *frame) {
    io_put_string("Exception ");
    /* TODO: Print interrupt number */
    io_put_string("\n");
    
    while (1) {
        asm volatile("hlt");
    }
}

/* IRQ handler */
void int_handler_irq(interrupt_frame_t *frame) {
    /* Call registered handler if exists */
    if (handlers[frame->interrupt_number])
        handlers[frame->interrupt_number](frame);
    
    /* Send EOI to PIC */
    if (frame->interrupt_number >= 0x20 && frame->interrupt_number < 0x30) {
        outb(0x20, 0x20);  /* PIC1 EOI */
        if (frame->interrupt_number >= 0x28)
            outb(0xA0, 0x20);  /* PIC2 EOI */
    }
}

/* Syscall handler stub */
void int_handler_syscall(interrupt_frame_t *frame) {
    /* TODO: Dispatch to syscall handler */
}

/* Create IDT entry */
static void idt_set_entry(uint32_t index, uint64_t handler, uint16_t selector, 
                         uint8_t flags) {
    idt_entry_t *entry = &idt[index];
    
    entry->offset_low = handler & 0xFFFF;
    entry->offset_mid = (handler >> 16) & 0xFFFF;
    entry->offset_high = (handler >> 32) & 0xFFFFFFFF;
    entry->selector = selector;
    entry->ist = 0;
    entry->type_attr = flags;
    entry->zero = 0;
}

/* Initialize IDT */
void idt_init(void) {
    memset(idt, 0, sizeof(idt));
    
    /* Set IDT entries for exceptions and IRQs */
    /* TODO: Set up individual exception handlers in assembly */
    
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint64_t)idt;
    
    /* Load IDT */
    load_idt(&idt_ptr);
}

/* Initialize interrupt handling */
void interrupts_init(void) {
    memset(handlers, 0, sizeof(handlers));
    
    /* Register default handlers */
    for (int i = 0; i < 256; i++) {
        if (i < 32) {
            /* Exception */
            handlers[i] = int_handler_exception;
        } else if (i < 48) {
            /* IRQ */
            handlers[i] = int_handler_irq;
        } else if (i == 0x80) {
            /* Syscall */
            handlers[i] = int_handler_syscall;
        }
    }
    
    /* Initialize PIC (Programmable Interrupt Controller) */
    /* ICW1 */
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    
    /* ICW2 - Set base vectors */
    outb(0x21, 0x20);  /* PIC1 base */
    outb(0xA1, 0x28);  /* PIC2 base */
    
    /* ICW3 */
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    
    /* ICW4 */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    /* Clear masks */
    outb(0x21, 0x0);
    outb(0xA1, 0x0);
}

/* Register interrupt handler */
void interrupts_register_handler(uint32_t interrupt_num, interrupt_handler_t handler) {
    if (interrupt_num < 256)
        handlers[interrupt_num] = handler;
}

/* Enable interrupts */
void interrupts_enable(void) {
    enable_interrupts();
}

/* Disable interrupts */
void interrupts_disable(void) {
    disable_interrupts();
}
