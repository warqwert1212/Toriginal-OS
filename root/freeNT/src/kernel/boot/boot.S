.global _start
.section .text.boot

# Multiboot1 Header Alignment
.align 4
multiboot_header:
    .long 0x1BADB002                      # Magic number
    .long 0x00000003                      # Flags (align modules + mem info)
    .long -(0x1BADB002 + 0x00000003)      # Checksum

_start:
    cli                                   # Disable interrupts
    mov $stack_top, %esp                  # Set up temporary stack

    /* Clear page tables */
    mov $p4_table, %edi
    xor %eax, %eax
    mov $3072, %ecx                       # Clear P4, P3, and P2 tables
    rep stosl

    /* Set up Higher-Half Paging aligned with boot64.s (-2GB space) */
    /* PML4 Entry 0 (Identity Map) points to p3_table */
    movl $(p3_table + 0x3), %eax
    movl %eax, p4_table
    
    /* PML4 Entry 511 (-2GB mapping) points to p3_table */
    movl $(p3_table + 0x3), %eax
    movl %eax, (p4_table + 0xFFF)

    /* P3 Entry 510 (Maps the -2GB virtual range) points to p2_table */
    movl $(p2_table + 0x3), %eax
    movl %eax, (p3_table + 0xFF0)
    /* P3 Entry 0 (Identity mapping fallback) points to p2_table */
    movl $(p2_table + 0x3), %eax
    movl %eax, p3_table

    /* P2 Entry 0 & 1: Map first 4MB via two 2MB pages */
    movl $0x83, %eax
    movl %eax, p2_table
    movl $0x200083, %eax
    movl %eax, (p2_table + 8)

    # Enable PAE
    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4

    # Load CR3
    mov $p4_table, %eax
    mov %eax, %cr3

    # Enable long mode in EFER MSR
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr

    # Enable Paging
    mov %cr0, %eax
    or $(1 << 31), %eax
    mov %eax, %cr0

    # Jump to 64-bit Long Mode
    lgdt gdt64_pointer
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    # Call Higher-Half Kernel Main safely via absolute address register
    movabs $kernel_main, %rax
    call *%rax

halt_loop:
    cli
    hlt
    jmp halt_loop

.section .bss
.align 4096
p4_table: .skip 4096                      
p3_table: .skip 4096
p2_table: .skip 4096                      
stack_bottom:
.skip 16384                               
stack_top:

.section .data
.align 4
gdt64_pointer:
    .word gdt64_end - gdt64 - 1
    .long gdt64