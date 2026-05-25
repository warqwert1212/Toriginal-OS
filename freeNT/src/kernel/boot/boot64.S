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

.code32
print_hex32:
    push %ecx
    mov $28, %cl
1:
    mov %eax, %edx
    shr %cl, %edx
    and $0xf, %edx
    cmp $10, %dl
    jb 2f
    add $'a'-10, %dl
    jmp 3f
2:
    add $'0', %dl
3:
    mov %dl, %al
    out %al, %dx
    sub $4, %cl
    jns 1b
    pop %ecx
    ret

.section .text.boot
.code32

_start:
    mov $0x3f8, %dx
    mov $'S', %al
    out %al, %dx
    mov $0x501, %dx
    mov $1, %al
    out %al, %dx
    cli
    mov $0x3f8, %dx
    mov $'Q', %al
    out %al, %dx
    cld
    

    mov $0x3f8, %dx
    mov $'R', %al
    out %al, %dx
    /* Save multiboot parameters */
    mov %eax, multiboot_magic
    mov %ebx, multiboot_info

    mov $0x3f8, %dx
    mov $'S', %al
    out %al, %dx
    

    mov $0x3f8, %dx
    mov $'P', %al
    out %al, %dx
    /* Disable paging */
    mov %cr0, %eax
    and $~0x80000000, %eax
    mov %eax, %cr0
    xor %eax, %eax
    mov %eax, %cr3

    mov $0x3f8, %dx
    mov $'A', %al
    out %al, %dx

    /* Set up page tables in low memory (0x1000-0x5000) */
    mov $0x1000, %edi
    xor %eax, %eax
    mov $0x1000, %ecx
    rep stosl
    
    /* PML4: Entry 0 (low identity) and 256 (high kernel) */
    movl $0x2003, 0x1000
    movl $0x2003, (0x1000 + 256*8)

    /* PDPT: Entry 0 for both low and high ranges */
    movl $0x3003, 0x2000

     /* PD: Identity map first 4MB in two 2MB pages.
         The high-half kernel is loaded at 2MB physical to match the 2MB page mapping. */
    movl $0x83, %eax
    mov %eax, 0x3000
     movl $0x200083, 0x3008
    mov %cr4, %eax
    mov $0x3f8, %dx
    mov $'B', %al
    out %al, %dx
    or $0x20, %eax
    mov %eax, %cr4

    mov $0x3f8, %dx
    mov $'C', %al
    out %al, %dx

    /* Set LM bit */
    mov $0xc0000080, %ecx
    rdmsr
    mov $0x3f8, %dx
    mov $'D', %al
    out %al, %dx
    or $0x100, %eax
    wrmsr

    mov $0x3f8, %dx
    mov $'E', %al
    out %al, %dx

    /* Load page table base */
    mov $0x1000, %eax
    mov %eax, %cr3

    /* Dump CPU register state before enabling paging */
    mov $0x3f8, %dx
    mov $'C', %al
    out %al, %dx
    mov $'R', %al
    out %al, %dx
    mov $'0', %al
    out %al, %dx
    mov $'=', %al
    out %al, %dx
    mov %cr0, %eax
    call print_hex32
    mov $'\n', %al
    out %al, %dx

    mov $0x3f8, %dx
    mov $'C', %al
    out %al, %dx
    mov $'R', %al
    out %al, %dx
    mov $'3', %al
    out %al, %dx
    mov $'=', %al
    out %al, %dx
    mov %cr3, %eax
    call print_hex32
    mov $'\n', %al
    out %al, %dx

    mov $0x3f8, %dx
    mov $'C', %al
    out %al, %dx
    mov $'R', %al
    out %al, %dx
    mov $'4', %al
    out %al, %dx
    mov $'=', %al
    out %al, %dx
    mov %cr4, %eax
    call print_hex32
    mov $'\n', %al
    out %al, %dx

    mov $0xc0000080, %ecx
    rdmsr
    mov $0x3f8, %dx
    mov $'E', %al
    out %al, %dx
    mov $'F', %al
    out %al, %dx
    mov $'E', %al
    out %al, %dx
    mov $'R', %al
    out %al, %dx
    mov $'=', %al
    out %al, %dx
    mov %eax, %edx
    call print_hex32
    mov $'_', %al
    out %al, %dx
    mov %edx, %eax
    call print_hex32
    mov $'\n', %al
    out %al, %dx

    /* Dump PML4/PDPT/PD entries (8 bytes each) to serial port 0x3f8
       Print layout: [PML4[0]]<NL>[PML4[256]]<NL>[PDPT@0x2000]<NL>[PD@0x3000]<NL>[PD@0x3008]<NL> */
    mov $0x3f8, %dx

    /* Helper: dump 8 bytes from address in %esi using %ecx as counter */
    /* PML4[0] @ 0x1000 */
    mov $0x1000, %esi
    mov $8, %ecx
1:
    movb (%esi), %al
    out %al, %dx
    inc %esi
    loop 1b
    /* newline */
    mov $'\n', %al
    out %al, %dx

    /* PML4[256] @ 0x1000 + 256*8 = 0x1800 */
    mov $0x1800, %esi
    mov $8, %ecx
2:
    movb (%esi), %al
    out %al, %dx
    inc %esi
    loop 2b
    mov $'\n', %al
    out %al, %dx

    /* PDPT @ 0x2000 */
    mov $0x2000, %esi
    mov $8, %ecx
3:
    movb (%esi), %al
    out %al, %dx
    inc %esi
    loop 3b
    mov $'\n', %al
    out %al, %dx

    /* PD @ 0x3000 */
    mov $0x3000, %esi
    mov $8, %ecx
4:
    movb (%esi), %al
    out %al, %dx
    inc %esi
    loop 4b
    mov $'\n', %al
    out %al, %dx

    /* PD entry 1 @ 0x3008 */
    mov $0x3008, %esi
    mov $8, %ecx
5:
    movb (%esi), %al
    out %al, %dx
    inc %esi
    loop 5b
    mov $'\n', %al
    out %al, %dx

    /* continue with existing marker */
    mov $0x3f8, %dx
    mov $'G', %al
    out %al, %dx

    /* Enable paging */
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0

    mov $0x3f8, %dx
    mov $'F', %al
    out %al, %dx

    mov $0x3f8, %dx
    mov $'H', %al
    out %al, %dx

    /* Load GDT */
    lgdt gdt64_ptr

    mov $0x3f8, %dx
    mov $'J', %al
    out %al, %dx

    /* Far jump into long mode */
    ljmp $0x08, $_start64

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

    jmp _start64
print_hex64:
    mov $60, %cl
1:
    mov %rax, %rdx
    shr %cl, %rdx
    and $0xf, %rdx
    cmp $10, %dl
    jb 2f
    add $'a'-10, %dl
    jmp 3f
2:
    add $'0', %dl
3:
    mov %dl, %al
    out %al, %dx
    sub $4, %cl
    jns 1b
    ret

_start64:
    mov $0x3f8, %dx
    mov $'K', %al
    out %al, %dx

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    
    /* Set stack */
    mov $0xFFFF800000008000, %rsp
    xor %rbp, %rbp

    mov $0x3f8, %dx
    mov $'S', %al
    out %al, %dx
    mov $'P', %al
    out %al, %dx
    mov $'=', %al
    out %al, %dx
    mov %rsp, %rax
    call print_hex64
    mov $'\n', %al
    out %al, %dx

    mov $0x3f8, %dx
    mov $'Y', %al
    out %al, %dx

    /* Call kernel_main using RIP-relative addressing */
    mov multiboot_magic, %edi
    mov multiboot_info, %esi
    
    /* Use movabs for absolute 64-bit address */
    movabs $kernel_main, %rax
    mov $0x3f8, %dx
    mov $'L', %al
    out %al, %dx
    call *%rax

    mov $0x3f8, %dx
    mov $'P', %al
    out %al, %dx
    
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
