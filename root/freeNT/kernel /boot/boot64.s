/* freeNT Bootloader
 * Multiboot2
 * x86 -> x86_64 transition
 */

.set MB2_MAGIC,      0xe85250d6
.set MB2_ARCH,       0
.set MB2_LENGTH,     multiboot_header_end - multiboot_header
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

multiboot_header_end:

.section .text
.code32

.global _start
.extern kernel_main

_start:

    cli

    mov %eax, multiboot_magic
    mov %ebx, multiboot_info

    mov $stack_top, %esp

    call check_cpuid
    call check_long_mode

    call setup_page_tables

    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    mov $pml4_table, %eax
    mov %eax, %cr3

    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    mov %cr0, %eax
    or $0x80000001, %eax
    mov %eax, %cr0

    lgdt gdt64_pointer

    ljmp $0x08, $long_mode_start

hang:
    cli
    hlt
    jmp hang

check_cpuid:
    pushfd
    pop %eax
    mov %eax,%ecx
    xor $0x200000,%eax
    push %eax
    popfd
    pushfd
    pop %eax
    xor %ecx,%eax
    jz hang
    ret

check_long_mode:
    mov $0x80000000,%eax
    cpuid
    cmp $0x80000001,%eax
    jb hang

    mov $0x80000001,%eax
    cpuid
    test $0x20000000,%edx
    jz hang
    ret

setup_page_tables:

    movl $(pdpt_table + 0x03), pml4_table

    movl $(pd_table + 0x03), pdpt_table

    movl $0x00000083, pd_table
    movl $0x00200083, pd_table + 8
    movl $0x00400083, pd_table + 16
    movl $0x00600083, pd_table + 24

    ret

.code64

long_mode_start:

    mov $0x10,%ax

    mov %ax,%ds
    mov %ax,%es
    mov %ax,%ss
    mov %ax,%fs
    mov %ax,%gs

    mov $stack_top,%rsp

    mov multiboot_magic,%edi
    mov multiboot_info,%esi

    call kernel_main

halt:
    hlt
    jmp halt

.align 16

gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF

gdt64_end:

gdt64_pointer:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

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

.section .data

multiboot_magic:
    .long 0

multiboot_info:
    .long 0