/* ==============================================================================
 * KEYBOARD_ISR.S - IRQ1 Interrupt Service Routine Stub
 * Saves full CPU state, calls C handler, restores, sends EOI, returns
 * AT&T syntax to match your existing boot64.S style
 * ============================================================================== */

.section .text
.globl keyboard_isr_stub
.extern keyboard_irq_handler

keyboard_isr_stub:
#    save all general purpose registers
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

#    Stack alignment check: the CPU already pushed RFLAGS+CS+RIP (24 bytes)
#    on IRQ entry, and the 15 pushes above add 120 bytes: 24+120=144, which
#    is already a multiple of 16. The extra "sub $8, %rsp" that used to be
#    here past-aligned the stack by 8 bytes at the call site, corrupting
#    the callee's view of the stack — this is the actual cause of the
#    garbled/repeating scancode data, not anything in keyboard.c's C code.
#    No adjustment needed; call directly.

#    Call the C handler (keyboard_irq_handler reads scancode + EOI)
    call keyboard_irq_handler

#    Restore all registers
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    iretq

.section .note.GNU-stack, "", @progbits
