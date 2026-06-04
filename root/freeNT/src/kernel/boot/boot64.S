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
    push %edx          
    mov $28, %cl
1:
    mov %eax, %edx
bin_shift:
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
    mov $0x3f8, %dx    
    out %al, %dx
    sub $4, %cl
    jns 1b
    pop %edx
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
    
    /* Serial probe: 'A' at very start */
    mov $0x3f8, %dx
    mov $'A', %al
    out %al, %dx

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

    /* Set up page tables in low memory (0x1000-0x7000) */
    mov $0x1000, %edi
    xor %eax, %eax
    mov $0x2000, %ecx
    rep stosl

    /* PML4: Entry 0 (low identity) points to PDPT at 0x2000 */
    movl $0x2003, %eax
    movl %eax, 0x1000
    movl $0x0, %eax
    movl %eax, 0x1004

    /* PML4: Entry 511 (high kernel half -2GB range) points to distinct PDPT at 0x3000 */
    movl $0x3003, %eax
    movl %eax, 0x1ff8
    movl $0x0, %eax
    movl %eax, 0x1ffc

    /* Serial probe: 'B' before paging */
    mov $0x3f8, %dx
    mov $'B', %al
    out %al, %dx

    /* Identity PDPT (0x2000): entry 0 points to Identity PD at 0x4000 */
    movl $0x4003, %eax
    movl %eax, 0x2000
    movl $0x0, %eax
    movl %eax, 0x2004

    /* Kernel PDPT (0x3000): entry 510 (for -2GB mapping) points to Kernel PD at 0x5000 */
    movl $0x5003, %eax
    movl %eax, 0x3ff0
    movl $0x0, %eax
    movl %eax, 0x3ff4

    /* Identity PD (0x4000): Map first 4MB using two 2MB large pages */
    movl $0x83, %eax
    movl %eax, 0x4000
    movl $0x0, %eax
    movl %eax, 0x4004
    movl $0x200083, %eax
    movl %eax, 0x4008
    movl $0x0, %eax
    movl %eax, 0x400c

    /* Kernel PD (0x5000): Map physical first 4MB to Higher-Half canonical workspace */
    movl $0x83, %eax
    movl %eax, 0x5000
    movl $0x0, %eax
    movl %eax, 0x5004
    movl $0x200083, %eax
    movl %eax, 0x5008
    movl $0x0, %eax
    movl %eax, 0x500c

    mov $0x3f8, %dx
    mov $'C', %al
    out %al, %dx
    mov $'R', %al
    out %al, %dx
    mov $'4', %al
    out %al, %dx
    mov $'=', %al
    out %al, %dx
    
    /* Ensure PAE (CR4.PAE bit) is set */
    mov $0x0000002a, %eax
    mov %eax, %cr4
    
    /* Dump CR4 immediately after write */
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
    mov $0x3f8, %dx
    mov $'\n', %al
    out %al, %dx

    /* Set LM bit */
    mov $0xc0000080, %ecx
    rdmsr
    mov $0x3f8, %dx
    mov $'D', %al
    out %al, %dx
    or $0x100, %eax
    wrmsr
    
    /* Read back and dump EFER after WRMSR */
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
    call print_hex32
    mov $0x3f8, %dx
    mov $'\n', %al
    out %al, %dx

    mov $0x3f8, %dx
    mov $'E', %al
    out %al, %dx

    /* Load page table base */
    mov $0x1000, %eax
    mov %eax, %cr3
    
    /* Dump CR3 immediately after write */
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
    mov $0x3f8, %dx
    mov $'\n', %al
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

    /* Load Valid 64-Bit GDT Struct */
    lgdt gdt64_ptr

    mov $0x3f8, %dx
    mov $'J', %al
    out %al, %dx

    /* Far jump into long mode */
    ljmp $0x08, $_start64

/* Minimal GDT: NULL, 64-bit code (L=1), 64-bit data */
.align 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff     /* Code Segment Descriptor */
    .quad 0x00cf92000000ffff     /* Data Segment Descriptor */
gdt64_end:

.align 4
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64                  

.section .text.boot
.code64

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
    mov $0x3f8, %dx
    out %al, %dx
    sub $4, %cl
    jns 1b
    ret

_start64:
    /* SPIN-LOOP DIAGNOSTIC: Output 'Z' repeatedly */
    mov $10, %rcx        
.L_spin_z:
    mov $0x3f8, %dx
    mov $'Z', %al
    out %al, %dx
    loop .L_spin_z
    
    mov $0x3f8, %dx
    mov $'K', %al
    out %al, %dx

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    
    /* Set stack execution zone safely */
    mov $0x0000000000008000, %rsp
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
    mov $0x3f8, %dx
    mov $'\n', %al
    out %al, %dx

    mov $0x3f8, %dx
    mov $'Y', %al
    out %al, %dx

    /* Call kernel_main */
    mov multiboot_magic, %edi
    mov multiboot_info, %esi
    
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