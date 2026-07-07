/* =============================================================================
 * KEYBOARD_ISR.S - IRQ1 Interrupt Service Routine Stub
 *
 * Rewritten from scratch. Saves all general-purpose registers, calls the
 * C handler with a correctly 16-byte-aligned stack at the call site,
 * restores everything, and returns via iretq.
 *
 * Alignment math: on entry to this stub, the CPU has already pushed
 * RFLAGS, CS, RIP (24 bytes) for a same-privilege interrupt with no error
 * code. The 15 pushes below add 15*8 = 120 bytes. 24 + 120 = 144, which
 * is already a multiple of 16 — so RSP is correctly aligned for `call`
 * with no extra adjustment needed.
 * ============================================================================== */

.section .text
.globl keyboard_isr_stub
.extern keyboard_irq_handler

keyboard_isr_stub:
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

    call keyboard_irq_handler

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