/* Multiboot2 header must be 32-bit aligned and at start of boot section */
#define MULTIBOOT_MAGIC 0xe85250d6
#define MULTIBOOT_ARCHITECTURE_I386 0

.section .text.boot
.align 8
.globl multiboot_header
.globl _start

multiboot_header:
    .long MULTIBOOT_MAGIC
    .long MULTIBOOT_ARCHITECTURE_I386
    .long (multiboot_header_end - multiboot_header)
    .long -(MULTIBOOT_MAGIC + MULTIBOOT_ARCHITECTURE_I386 + (multiboot_header_end - multiboot_header))
    
    /* End tag */
    .align 8
    .word 0
    .word 0
    .long 8

multiboot_header_end:

.section .bss.boot
.align 16
boot_stack_bottom:
    .skip 16384
boot_stack_top:

.section .text.boot
.code32

_start:
    cli
    cld
    
    /* Set up stack */
    mov $boot_stack_top, %esp
    and $~15, %esp
    
    /* Save multiboot info for kernel_main */
    push %ebx                      /* Multiboot info struct */
    push %eax                      /* Multiboot magic number */
    
    /* Call kernel main (32-bit version) */
    call kernel_main
    
    /* Should never return */
    cli
    hlt
.loop_forever:
    jmp .loop_forever

.section .text
.code64

.globl load_idt
load_idt:
    lidt (%rdi)
    ret

.globl enable_interrupts
enable_interrupts:
    sti
    ret

.globl disable_interrupts
disable_interrupts:
    cli
    ret

/* Mark stack as non-executable */
.section .note.GNU-stack, "", @progbits
