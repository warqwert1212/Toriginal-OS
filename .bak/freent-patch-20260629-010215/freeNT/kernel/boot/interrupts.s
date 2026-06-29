/* =============================================================================
 * interrupts.s  —  ISR stubs + load_idt
 *
 * FIXES vs original:
 *   1. File declared .intel_syntax noprefix but is assembled by GAS
 *      (GNU Assembler) which defaults to AT&T syntax.  The mismatch meant
 *      every instruction was mis-parsed → assembler errors / wrong opcodes.
 *      Rewritten entirely in AT&T syntax (the same dialect used by boot64.s
 *      and keyboard_isr.s).
 *   2. load_idt used "lidt [rdi]" (Intel) → corrected to "lidt (%rdi)".
 *   3. "mov rdi, offset msgN" (Intel) → "leaq msgN(%rip), %rdi" (AT&T,
 *      RIP-relative, required for -mcmodel=kernel).
 *   4. "jmp $" (infinite loop) → "1: hlt; jmp 1b" so the CPU halts
 *      rather than spinning at 100 % and burning the host.
 *   5. IRQ stubs sent no EOI — GRUB/firmware leaves PIC in an unknown
 *      state; without EOI every subsequent IRQ is masked.  Stubs now send
 *      the correct EOI byte(s) before iretq.
 *      (interrupts.c's dynamic stubs also do this, but these static stubs
 *       are fallbacks registered before interrupts_init() runs.)
 *
 * NOTE: as of the current kernel.c wiring, idt_init() (interrups.c)
 * builds its own dynamic stub_table for ALL 256 vectors and points the
 * live IDT at those, not at the isrN/irqN labels below. Those labels are
 * therefore currently unreferenced by the IDT but kept here as documented,
 * ready-to-wire fallbacks (and this file is still required for load_idt).
 * ============================================================================= */

.section .text

/* ── load_idt — called from interrupts.c with pointer in %rdi ─────────── */
.global load_idt
load_idt:
    lidt (%rdi)
    ret

/* ── PIC EOI helpers (inlined as byte sequences) ─────────────────────── */
/* outb(0x20, 0x20) – master EOI */
.macro eoi_master
    movb $0x20, %al
    outb %al, $0x20
.endm

/* outb(0xA0, 0x20) + outb(0x20, 0x20) – slave + master EOI */
.macro eoi_both
    movb $0x20, %al
    outb %al, $0xA0
    outb %al, $0x20
.endm

/* ── CPU exception stubs (vectors 0-31) ──────────────────────────────── */
/* These call panic() with a string describing the fault.
 * RIP-relative addressing is required with -mcmodel=kernel.             */

.macro isr_stub num, msg_sym
.global isr\num
isr\num:
    cli
    leaq \msg_sym(%rip), %rdi
    call panic
1:  hlt
    jmp 1b
.endm

isr_stub  0, msg0
isr_stub  1, msg1
isr_stub  2, msg2
isr_stub  3, msg3
isr_stub  4, msg4
isr_stub  5, msg5
isr_stub  6, msg6
isr_stub  7, msg7
isr_stub  8, msg8
isr_stub  9, msg9
isr_stub 10, msg10
isr_stub 11, msg11
isr_stub 12, msg12
isr_stub 13, msg13
isr_stub 14, msg14
isr_stub 15, msg15
isr_stub 16, msg16
isr_stub 17, msg17
isr_stub 18, msg18
isr_stub 19, msg19
isr_stub 20, msg20
isr_stub 21, msg21
isr_stub 22, msg22
isr_stub 23, msg23
isr_stub 24, msg24
isr_stub 25, msg25
isr_stub 26, msg26
isr_stub 27, msg27
isr_stub 28, msg28
isr_stub 29, msg29
isr_stub 30, msg30
isr_stub 31, msg31

/* ── Hardware IRQ stubs (vectors 32-47, IRQ 0-15) ────────────────────── */
/* FIX 5: send EOI before returning so the PIC unmasks future interrupts  */

.global irq0
irq0:
    eoi_master
    iretq

.global irq1
irq1:
    eoi_master
    iretq

.global irq2
irq2:
    eoi_master
    iretq

.global irq3
irq3:
    eoi_master
    iretq

.global irq4
irq4:
    eoi_master
    iretq

.global irq5
irq5:
    eoi_master
    iretq

.global irq6
irq6:
    eoi_master
    iretq

.global irq7
irq7:
    eoi_master
    iretq

/* IRQ 8-15 come from the slave PIC — need slave+master EOI */
.global irq8
irq8:
    eoi_both
    iretq

.global irq9
irq9:
    eoi_both
    iretq

.global irq10
irq10:
    eoi_both
    iretq

.global irq11
irq11:
    eoi_both
    iretq

.global irq12
irq12:
    eoi_both
    iretq

.global irq13
irq13:
    eoi_both
    iretq

.global irq14
irq14:
    eoi_both
    iretq

.global irq15
irq15:
    eoi_both
    iretq

/* ── Exception message strings ───────────────────────────────────────── */
.section .rodata

msg0:  .asciz "EXCEPTION 0: Divide By Zero"
msg1:  .asciz "EXCEPTION 1: Debug"
msg2:  .asciz "EXCEPTION 2: NMI"
msg3:  .asciz "EXCEPTION 3: Breakpoint"
msg4:  .asciz "EXCEPTION 4: Overflow"
msg5:  .asciz "EXCEPTION 5: Bound Range Exceeded"
msg6:  .asciz "EXCEPTION 6: Invalid Opcode"
msg7:  .asciz "EXCEPTION 7: Device Not Available"
msg8:  .asciz "EXCEPTION 8: Double Fault"
msg9:  .asciz "EXCEPTION 9: Coprocessor Segment Overrun"
msg10: .asciz "EXCEPTION 10: Invalid TSS"
msg11: .asciz "EXCEPTION 11: Segment Not Present"
msg12: .asciz "EXCEPTION 12: Stack Segment Fault"
msg13: .asciz "EXCEPTION 13: General Protection Fault"
msg14: .asciz "EXCEPTION 14: Page Fault"
msg15: .asciz "EXCEPTION 15: Reserved"
msg16: .asciz "EXCEPTION 16: x87 FPU Error"
msg17: .asciz "EXCEPTION 17: Alignment Check"
msg18: .asciz "EXCEPTION 18: Machine Check"
msg19: .asciz "EXCEPTION 19: SIMD Floating-Point"
msg20: .asciz "EXCEPTION 20: Virtualization"
msg21: .asciz "EXCEPTION 21: Control Protection"
msg22: .asciz "EXCEPTION 22: Reserved"
msg23: .asciz "EXCEPTION 23: Reserved"
msg24: .asciz "EXCEPTION 24: Reserved"
msg25: .asciz "EXCEPTION 25: Reserved"
msg26: .asciz "EXCEPTION 26: Reserved"
msg27: .asciz "EXCEPTION 27: Reserved"
msg28: .asciz "EXCEPTION 28: Hypervisor Injection"
msg29: .asciz "EXCEPTION 29: VMM Communication"
msg30: .asciz "EXCEPTION 30: Security Exception"
msg31: .asciz "EXCEPTION 31: Reserved"

/* Mark stack as non-executable (suppresses linker warning) */
.section .note.GNU-stack,"",@progbits
