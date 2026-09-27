/* =============================================================================
 * KEYBOARD_ISR.S - IRQ1 Interrupt Service Routine Stub
 *
 * NOTE: not currently wired into the live IDT - interrupts.c's
 * isr_trampoline (kernel/interrupts.c) is what every vector actually
 * points at today. This file is kept as a documented, ready-to-wire
 * fallback, same status as interrupts.s's isrN/irqN stubs.
 *
 * Saves all general-purpose registers, calls the C handler with a
 * correctly 16-byte-aligned stack at the call site, restores
 * everything, and returns via iretq.
 *
 * Explicit alignment before `call`: a same-privilege interrupt (the
 * normal case for a hardware IRQ firing while the kernel is already
 * running) does NOT get automatic stack realignment from the CPU -
 * RSP on entry is whatever the interrupted code happened to have,
 * not guaranteed 16-aligned. See kernel/interrupts.c's isr_trampoline
 * comment for the full reasoning (same fix, same file this stub would
 * mirror if it's ever wired up) - capture RSP into %rbx (free to
 * reuse: its real value is already safely pushed below and gets
 * restored from there) before aligning, so it can be undone exactly
 * before the pops below unwind.
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

    mov %rsp, %rbx
    and $-16, %rsp
    call keyboard_irq_handler
    mov %rbx, %rsp

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
