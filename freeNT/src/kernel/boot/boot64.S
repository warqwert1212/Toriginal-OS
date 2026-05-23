/* freeNT 64-bit bootloader - Multiboot2 compliant
   Transitions from 32-bit to 64-bit mode  */

#define MULTIBOOT_MAGIC 0xe85250d6
#define MULTIBOOT_ARCH_I386 0

.section .text.boot
.align 8
.globl multiboot_header
.globl _start

/* Multiboot2 header */
multiboot_header:
    .long MULTIBOOT_MAGIC
    .long MULTIBOOT_ARCH_I386
    .long (multiboot_header_end - multiboot_header)
    .long -(MULTIBOOT_MAGIC + MULTIBOOT_ARCH_I386 + (multiboot_header_end - multiboot_header))
    
    /* End tag */
    .align 8
    .word 0
    .word 0
    .long 8

multiboot_header_end:

.section .text.boot
.code32

_start:
    cli
    cld
    
    /* Save multiboot parameters */
    mov %eax, multiboot_magic
    mov %ebx, multiboot_info
    
    /* Disable paging */
    mov %cr0, %eax
    and $~0x80000000, %eax
    mov %eax, %cr0
    xor %eax, %eax
    mov %eax, %cr3
    
    /* Set up page tables in low memory (0x1000-0x5000) */
    mov $0x1000, %edi
    xor %eax, %eax
    mov $0x1000, %ecx
    rep stosl
    
    /* PML4: Entry 0 and 256 */
    mov $0x2003, 0x1000
    mov $0x2003, (0x1000 + 256*8)
    
    /* PDPT: Entry 0 and 256  */
    mov $0x3003, 0x2000
    mov $0x3003, (0x2000 + 256*8)
    
    /* PD: Identity map first 2MB */
    mov $0x83, %eax
    mov %eax, 0x3000
    mov %eax, (0x3000 + 256*8)
    
    /* Load CR3 */
    mov $0x1000, %eax
    mov %eax, %cr3
    
    /* Enable PAE */
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4
    
    /* Set LM bit */
    mov $0xc0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr
    
    /* Enable paging */
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0
    
    /* Load GDT */
    lgdt gdt64_ptr
    
    /* Jump to 64-bit mode */
    ljmp $8, $_start64

.align 8
gdt64:
    .quad 0                      /* NULL */
    .quad 0x00af9a000000ffff     /* Code */
    .quad 0x00af92000000ffff     /* Data */
gdt64_end:

.align 8
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

.section .text.boot
.code64

_start64:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    
    /* Set stack */
    mov $0xFFFF800000008000, %rsp
    xor %rbp, %rbp
    
    /* Call kernel_main using RIP-relative addressing */
    mov multiboot_magic, %edi
    mov multiboot_info, %esi
    
    /* Use movabs for absolute 64-bit address */
    movabs $kernel_main, %rax
    call *%rax
    
    cli
    hlt
    jmp .

.section .data.boot
multiboot_magic:
    .long 0
multiboot_info:
    .long 0

.section .text

.globl load_idt
.type load_idt, @function
load_idt:
    lidt (%rdi)
    ret

.globl enable_interrupts
.type enable_interrupts, @function
enable_interrupts:
    sti
    ret

.globl disable_interrupts
.type disable_interrupts, @function
disable_interrupts:
    cli
    ret

.section .note.GNU-stack, "", @progbits
