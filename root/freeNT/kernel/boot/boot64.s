/* =========================================================================
 * FreeNT Boot64
 * Multiboot2 compliant
 * 32-bit -> 64-bit transition
 * ========================================================================= */

.set MB2_MAGIC,      0xe85250d6
.set MB2_ARCH,       0
.set MB2_LENGTH,     multiboot_end - multiboot_header
.set MB2_CHECKSUM,   -(MB2_MAGIC + MB2_ARCH + MB2_LENGTH)

.section .multiboot
.align 8

multiboot_header:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_LENGTH
    .long MB2_CHECKSUM

    .align 8
    .word 0
    .word 0
    .long 8

multiboot_end:

/* ========================================================================= */

.section .text
.code32

.global _start
.extern kernel_main

_start:
    cli
    cld

    /* Save multiboot registers before we clobber them */
    mov %eax, multiboot_magic
    mov %ebx, multiboot_info

    mov $stack_top, %esp

    call check_cpuid
    call check_long_mode

    call clear_page_tables
    call build_page_tables

    /* Enable PAE */
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    /* Load CR3 with PML4 address */
    mov $pml4_table, %eax
    mov %eax, %cr3

    /* Enable Long Mode via EFER MSR */
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    /* Enable Paging + Protected Mode */
    mov %cr0, %eax
    or $0x80000001, %eax
    mov %eax, %cr0

    lgdt gdt64_pointer

    ljmp $0x08, $long_mode_start

/* ========================================================================= */

hang:
    cli
1:
    hlt
    jmp 1b

/* ========================================================================= */
/* CPUID check — use pushfq/popfq in 32-bit mode via the 32-bit equivalents  */
/* FIXED: replaced pushfd/popfd with equivalent manual stack operations       */

check_cpuid:
    /* Try to flip bit 21 of EFLAGS — if it sticks, CPUID is supported */
    pushfl
    pop %eax
    mov %eax, %ecx
    xor $0x200000, %eax
    push %eax
    popfl
    pushfl
    pop %eax
    xor %ecx, %eax
    jz hang
    ret

/* ========================================================================= */

check_long_mode:
    mov $0x80000000, %eax
    cpuid
    cmp $0x80000001, %eax
    jb hang
    mov $0x80000001, %eax
    cpuid
    test $0x20000000, %edx
    jz hang
    ret

/* ========================================================================= */

clear_page_tables:
    mov $pml4_table, %edi
    xor %eax, %eax
    mov $4096, %ecx
    rep stosl
    ret

/* ========================================================================= */
/* FIXED: build page tables without using | on section addresses at asm time  */
/* Use lea + or to compute the combined address+flags value at runtime        */

build_page_tables:
    /* PML4[0] -> pdpt_table | 3 */
    lea pdpt_table, %eax
    or $3, %eax
    mov %eax, pml4_table

    /* PDPT[0] -> pd_table | 3 */
    lea pd_table, %eax
    or $3, %eax
    mov %eax, pdpt_table

    /* Map first 1GB using 512 x 2MB pages */
    xor %ecx, %ecx
build_pd:
    mov %ecx, %eax
    shl $21, %eax
    or $0x83, %eax          /* Present + Writable + Huge */
    mov %eax, pd_table(,%ecx,8)
    inc %ecx
    cmp $512, %ecx
    jne build_pd

    ret

/* ========================================================================= */

.code64

long_mode_start:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    mov $stack_top, %rsp
    xor %rbp, %rbp

    /* Pass multiboot info to kernel_main(magic, info) */
    mov multiboot_magic, %edi
    mov multiboot_info, %esi

    call kernel_main

halt64:
    hlt
    jmp halt64

/* ========================================================================= */

.align 16

gdt64:
    .quad 0x0000000000000000    /* Null descriptor */
    .quad 0x00AF9A000000FFFF    /* 64-bit code segment */
    .quad 0x00AF92000000FFFF    /* 64-bit data segment */

gdt64_end:

gdt64_pointer:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

/* ========================================================================= */

.section .bss

.align 4096
pml4_table:
    .skip 4096

.align 4096
pdpt_table:
    .skip 4096

.align 4096
pd_table:
    .skip 4096

.align 16
stack_bottom:
    .skip 16384
stack_top:

/* ========================================================================= */

.section .data

multiboot_magic:
    .long 0

multiboot_info:
    .long 0