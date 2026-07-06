/* ==============================================================================
 * KEYBOARD_ISR.S - IRQ1 Interrupt Service Routine Stub
 * Saves full CPU state, calls C handler, restores, sends EOI, returns
 * AT&T syntax to match your existing boot64.S style
 * ============================================================================== */

.section .text
.globl keyboard_isr_stub
.extern keyboard_irq_handler

keyboard_isr_stub:
    /* Save all general purpose registers */
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

    /* 
     * FIXED ALIGNMENT MECHANISM:
     * Instead of relying on vulnerable static math assumptions, we save the 
     * current stack frame pointer into RBP and forcefully realign RSP to a 
     * strict 16-byte boundary. This prevents System V ABI compliance crashes 
     * across all optimization levels (-O0 to -O3).
     */
    mov %rsp, %rbp
    and $-16, %rsp 

    /* Call the C handler safely */
    call keyboard_irq_handler

    /* Restore the true unaligned stack layout */
    mov %rbp, %rsp

    /* Restore all registers */
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
