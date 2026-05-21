.section .text
.global _start
.global kernel_main
.extern kernel_main

/* Multiboot2 header */
.align 8
multiboot_header:
    .long 0xe85250d6                      /* Magic */
    .long 0                               /* Architecture (i386) */
    .long (multiboot_header_end - multiboot_header)  /* Header length */
    .long 0x100000000 - (0xe85250d6 + 0 + (multiboot_header_end - multiboot_header))
    
    /* Information request */
    .align 8
    .word 1
    .word 0
    .long 12
    .long 0
    
    /* Entry address tag */
    .align 8
    .word 3
    .word 0
    .long 12
    .long _start
    
    /* End tag */
    .align 8
    .word 0
    .word 0
    .long 8
    
multiboot_header_end:

.section .data
.align 4096
boot_stack_bottom:
    .skip 16384
boot_stack_top:

.section .text
.align 16

_start:
    cli                                   /* Disable interrupts */
    mov $boot_stack_top, %rsp             /* Set up stack */
    and $~15, %rsp                        /* Align stack to 16 bytes */
    
    mov %rdi, %rsi                        /* Multiboot magic in rsi */
    mov %rsi, %rdi                        /* Multiboot info in rdi */
    
    call kernel_main
    
    hlt
    jmp .

.global load_idt
load_idt:
    lidt (%rdi)
    ret

.global enable_interrupts
enable_interrupts:
    sti
    ret

.global disable_interrupts
disable_interrupts:
    cli
    ret
