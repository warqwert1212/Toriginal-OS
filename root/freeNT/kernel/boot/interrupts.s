.intel_syntax noprefix

.section .text

.global load_idt

.global isr0
.global isr1
.global isr2
.global isr3
.global isr4
.global isr5
.global isr6
.global isr7
.global isr8
.global isr9
.global isr10
.global isr11
.global isr12
.global isr13
.global isr14
.global isr15
.global isr16
.global isr17
.global isr18
.global isr19
.global isr20
.global isr21
.global isr22
.global isr23
.global isr24
.global isr25
.global isr26
.global isr27
.global isr28
.global isr29
.global isr30
.global isr31

.global irq0
.global irq1
.global irq2
.global irq3
.global irq4
.global irq5
.global irq6
.global irq7
.global irq8
.global irq9
.global irq10
.global irq11
.global irq12
.global irq13
.global irq14
.global irq15

.extern panic

load_idt:
    lidt [rdi]
    ret

/* ========================= */
/* CPU Exceptions            */
/* ========================= */

isr0:
    cli
    mov rdi, offset msg0
    call panic
    jmp $

isr1:
    cli
    mov rdi, offset msg1
    call panic
    jmp $

isr2:
    cli
    mov rdi, offset msg2
    call panic
    jmp $

isr3:
    cli
    mov rdi, offset msg3
    call panic
    jmp $

isr4:
    cli
    mov rdi, offset msg4
    call panic
    jmp $

isr5:
    cli
    mov rdi, offset msg5
    call panic
    jmp $

isr6:
    cli
    mov rdi, offset msg6
    call panic
    jmp $

isr7:
    cli
    mov rdi, offset msg7
    call panic
    jmp $

isr8:
    cli
    mov rdi, offset msg8
    call panic
    jmp $

isr9:
    cli
    mov rdi, offset msg9
    call panic
    jmp $

isr10:
    cli
    mov rdi, offset msg10
    call panic
    jmp $

isr11:
    cli
    mov rdi, offset msg11
    call panic
    jmp $

isr12:
    cli
    mov rdi, offset msg12
    call panic
    jmp $

isr13:
    cli
    mov rdi, offset msg13
    call panic
    jmp $

isr14:
    cli
    mov rdi, offset msg14
    call panic
    jmp $

isr15:
    cli
    mov rdi, offset msg15
    call panic
    jmp $

isr16:
    cli
    mov rdi, offset msg16
    call panic
    jmp $

isr17:
    cli
    mov rdi, offset msg17
    call panic
    jmp $

isr18:
    cli
    mov rdi, offset msg18
    call panic
    jmp $

isr19:
    cli
    mov rdi, offset msg19
    call panic
    jmp $

isr20:
    cli
    mov rdi, offset msg20
    call panic
    jmp $

isr21:
    cli
    mov rdi, offset msg21
    call panic
    jmp $

isr22:
    cli
    mov rdi, offset msg22
    call panic
    jmp $

isr23:
    cli
    mov rdi, offset msg23
    call panic
    jmp $

isr24:
    cli
    mov rdi, offset msg24
    call panic
    jmp $

isr25:
    cli
    mov rdi, offset msg25
    call panic
    jmp $

isr26:
    cli
    mov rdi, offset msg26
    call panic
    jmp $

isr27:
    cli
    mov rdi, offset msg27
    call panic
    jmp $

isr28:
    cli
    mov rdi, offset msg28
    call panic
    jmp $

isr29:
    cli
    mov rdi, offset msg29
    call panic
    jmp $

isr30:
    cli
    mov rdi, offset msg30
    call panic
    jmp $

isr31:
    cli
    mov rdi, offset msg31
    call panic
    jmp $

/* ========================= */
/* IRQ STUBS                 */
/* ========================= */

irq0:  iretq
irq1:  iretq
irq2:  iretq
irq3:  iretq
irq4:  iretq
irq5:  iretq
irq6:  iretq
irq7:  iretq
irq8:  iretq
irq9:  iretq
irq10: iretq
irq11: iretq
irq12: iretq
irq13: iretq
irq14: iretq
irq15: iretq

.section .rodata

msg0:  .asciz "EXCEPTION 0: Divide By Zero"
msg1:  .asciz "EXCEPTION 1: Debug"
msg2:  .asciz "EXCEPTION 2: NMI"
msg3:  .asciz "EXCEPTION 3: Breakpoint"
msg4:  .asciz "EXCEPTION 4: Overflow"
msg5:  .asciz "EXCEPTION 5: Bound Range"
msg6:  .asciz "EXCEPTION 6: Invalid Opcode"
msg7:  .asciz "EXCEPTION 7: Device Not Available"
msg8:  .asciz "EXCEPTION 8: Double Fault"
msg9:  .asciz "EXCEPTION 9"
msg10: .asciz "EXCEPTION 10"
msg11: .asciz "EXCEPTION 11"
msg12: .asciz "EXCEPTION 12"
msg13: .asciz "EXCEPTION 13: General Protection Fault"
msg14: .asciz "EXCEPTION 14: Page Fault"
msg15: .asciz "EXCEPTION 15"
msg16: .asciz "EXCEPTION 16"
msg17: .asciz "EXCEPTION 17"
msg18: .asciz "EXCEPTION 18"
msg19: .asciz "EXCEPTION 19"
msg20: .asciz "EXCEPTION 20"
msg21: .asciz "EXCEPTION 21"
msg22: .asciz "EXCEPTION 22"
msg23: .asciz "EXCEPTION 23"
msg24: .asciz "EXCEPTION 24"
msg25: .asciz "EXCEPTION 25"
msg26: .asciz "EXCEPTION 26"
msg27: .asciz "EXCEPTION 27"
msg28: .asciz "EXCEPTION 28"
msg29: .asciz "EXCEPTION 29"
msg30: .asciz "EXCEPTION 30"
msg31: .asciz "EXCEPTION 31"
