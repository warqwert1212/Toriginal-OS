.set MB2_MAGIC,      0xe85250d6
.set MB2_ARCH,       0          /* i386 protected-mode */
.set MB2_LENGTH,     (multiboot_end - multiboot_header)
.set MB2_CHECKSUM,   -(MB2_MAGIC + MB2_ARCH + MB2_LENGTH)


.section .multiboot, "ax"
.align 8

multiboot_header:
    .long  MB2_MAGIC
    .long  MB2_ARCH
    .long  MB2_LENGTH
    .long  MB2_CHECKSUM

    .align 8
    .word  5              /* type = framebuffer request tag */
    .word  0               /* flags = 0 (not optional)        */
    .long  20               /* size = 5*4 = 20 bytes exactly   */
    .long  1024             /* requested width                 */
    .long  768              /* requested height                */
    .long  32               /* requested depth (bpp)           */
    /* no manual padding here — the .align 8 below inserts
     * exactly the right zero-padding before the next tag,
     * consistent with how GRUB itself walks the tag list
     * (read `size` bytes, then round up to 8-byte boundary) */

    /* FIX 1 – Properly-formed end tag (type=0, flags=0, size=8).
     *          Without this GRUB considers the header malformed and
     *          refuses to boot the image ("no bootable medium"). */
    .align 8
    .word  0        /* type  = 0 (end tag) */
    .word  0        /* flags = 0           */
    .long  8        /* size  = 8           */

multiboot_end:

/* ── 64-bit UEFI entry point ─────────────────────────────────────────────
 * TRBLU (kernel/boot/uefi/) hands off here already in long mode with
 * paging enabled (UEFI guarantees this on x86_64 - unlike the BIOS
 * path above, there's no real/protected mode to climb out of, no
 * CPUID/EFER/CR0 dance to run). What TRBLU does NOT guarantee is that
 * its page tables cover the same layout the rest of this kernel
 * assumes, so this entry point still calls the exact same
 * clear_page_tables/build_page_tables routines _start uses above and
 * switches CR3 to them before falling into long_mode_start - one
 * single page table layout regardless of which loader got us here.
 *
 * Calling convention: TRBLU jumps here (not calls - there's no return)
 * with RDI = a multiboot2-format info pointer it has synthesized
 * itself (identical tag layout to build_multiboot2_info in
 * stage2_pm.s - see kernel/boot/uefi/main.c's build_multiboot2_info),
 * and RSI = the multiboot2 bootloader magic (0x36D76289) - same two
 * values kernel_main() already expects from the BIOS path, just
 * shuffled into 64-bit registers directly instead of arriving in
 * EAX/EBX at a 32-bit entry. This is the ONLY new thing kernel_main
 * needs to understand about UEFI: nothing, because as far as it's
 * concerned this is just another multiboot2 boot. */
.global _start64_uefi

_start64_uefi:
    cli
    cld

    /* Stash TRBLU's info pointer/magic - build_page_tables clobbers
     * plenty of registers and we still need these after it returns. */
    movq %rdi, %r14        /* info pointer */
    movq %rsi, %r15        /* magic */

    movq $stack_top, %rsp

    call clear_page_tables
    call build_page_tables

    movq $pml4_table, %rax
    movq %rax, %cr3

    movl %r15d, multiboot_magic
    movl %r14d, multiboot_info

    jmp long_mode_start

/* ── 32-bit entry point ──────────────────────────────────────────────── */
.section .text
.code32

.global _start
.extern kernel_main

/* IMPORTANT: kernel64.ld's `*(.text._start)` rule to force _start
 * first is a no-op here (nothing in this file emits a section
 * literally named .text._start), so _start's actual link address is
 * just "whatever comes first, byte-for-byte, in this file's .text
 * section" - and this file's .text is linked first among all objects
 * (boot64.o is first in the Makefile's OBJS list). That address must
 * stay exactly 0x201000: stage2.s's KERNEL_ENTRY_LINK_ADDR and
 * installer.c's EXPECTED_KERNEL_ENTRY_ADDR both hardcode it (the
 * bootloader has to know the kernel's entry point before the kernel
 * is even loaded, so it can't be resolved as a normal symbol the way
 * protected_mode_entry's own jump target is). Concretely: _start must
 * be the FIRST label after the `.code32` line above - nothing,
 * including new diagnostic helpers, goes before it. (serial_putc32
 * learned this the hard way: it briefly sat here and silently shifted
 * the real entry point to 0x201013, which install-time's own
 * EXPECTED_KERNEL_ENTRY_ADDR check would have caught - "Kernel entry
 * point does not match what stage2.s expects" - rather than let it
 * boot wrong, but better to just not get it wrong. It now lives after
 * _start's body, same place check_cpuid/build_page_tables/etc.
 * already were.) */
_start:
    cli
    cld

    /* Save multiboot registers FIRST, before anything else touches
     * eax/ebx - GRUB hands the multiboot2 magic in EAX and the info
     * pointer in EBX at entry. Nothing below may run before this. */
    movl %eax, multiboot_magic
    movl %ebx, multiboot_info

    movl $msg_k1, %esi
    call serial_puts32

    movl $stack_top, %esp

    call check_cpuid
    call check_long_mode

    movl $msg_k2, %esi
    call serial_puts32

    call clear_page_tables
    call build_page_tables

    movl $msg_k3, %esi
    call serial_puts32

    movl %cr4, %eax
    orl  $0x620, %eax
    movl %eax, %cr4

    /* Load PML4 into CR3 */
    movl $pml4_table, %eax
    movl %eax, %cr3

    /* Enable Long Mode in EFER */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $0x100, %eax
    wrmsr

    movl %cr0, %eax
    orl  $0x80000003, %eax   /* PG | PE | MP */
    andl $0xFFFFFFFB, %eax   /* clear EM (bit 2) explicitly */
    movl %eax, %cr0

    movl $msg_k4, %esi
    call serial_puts32

    lgdt gdt64_pointer
    ljmp $0x08, $long_mode_start

/* ── Hang forever ─────────────────────────────────────────────────────
 * Reached only if check_cpuid or check_long_mode fails - this exact
 * CPU/VM config doesn't support long mode at all (some hypervisor CPU
 * presets disable the long-mode CPUID bit by default). Says which
 * check failed before halting, instead of just going dark. */
hang_no_cpuid:
    movl $msg_no_cpuid, %esi
    call serial_puts32
    jmp  hang
hang_no_longmode:
    movl $msg_no_longmode, %esi
    call serial_puts32
hang:
    cli
1:
    hlt
    jmp 1b

/* ---- Boot checkpoint messages, read by serial_puts32/64 below.
 * Plain C strings - this is the entire diagnostic system now: every
 * checkpoint is one "load the message, call the print routine" pair,
 * nothing per-character to get wrong. ---- */
msg_k1:          .asciz "K1: _start entered, multiboot regs saved\n"
msg_k2:          .asciz "K2: CPUID/long-mode checks passed\n"
msg_k3:          .asciz "K3: page tables built\n"
msg_k4:          .asciz "K4: paging+long mode enabled, jumping to long_mode_start\n"
msg_k5:          .asciz "K5: long_mode_start reached, calling kernel_main\n"
msg_no_cpuid:    .asciz "K: FATAL - this CPU has no CPUID support\n"
msg_no_longmode: .asciz "K: FATAL - this CPU/VM has no long mode support\n"

/* ---- serial_putc32: 32-bit COM1 byte send. IN: %al = byte.
 * Preserves %eax/%edx. ---- */
.global serial_putc32
serial_putc32:
    pushl   %edx
    pushl   %eax
serial_putc32_wait:
    movw    $0x3FD, %dx
    inb     %dx, %al
    testb   $0x20, %al
    jz      serial_putc32_wait
    popl    %eax
    movw    $0x3F8, %dx
    outb    %al, %dx
    popl    %edx
    ret

/* ---- serial_puts32: prints a NUL-terminated string over COM1.
 * IN: %esi = pointer to the string. Preserves %esi/%eax/%edx. ---- */
.global serial_puts32
serial_puts32:
    pushl   %esi
    pushl   %eax
serial_puts32_loop:
    movb    (%esi), %al
    testb   %al, %al
    jz      serial_puts32_done
    call    serial_putc32
    incl    %esi
    jmp     serial_puts32_loop
serial_puts32_done:
    popl    %eax
    popl    %esi
    ret

/* ── CPUID availability check ────────────────────────────────────────── */
check_cpuid:
    /* Flip bit 21 of EFLAGS; if it sticks, CPUID is supported */
    pushfl
    popl  %eax
    movl  %eax, %ecx
    xorl  $0x200000, %eax
    pushl %eax
    popfl
    pushfl
    popl  %eax
    xorl  %ecx, %eax
    jz    hang_no_cpuid
    ret

/* ── Long-mode availability check ───────────────────────────────────── */
check_long_mode:
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb   hang_no_longmode
    movl $0x80000001, %eax
    cpuid
    testl $0x20000000, %edx
    jz   hang_no_longmode
    ret

/* ── FIX 2 – Zero ALL page-table levels ──────────────────────────────── */
/*   The original only zeroed pml4_table; pdpt_table and pd_table        */
/*   remained uninitialised, producing random present-bit patterns and   */
/*   an immediate triple fault on first TLB miss.                        */
clear_page_tables:
    movl  $pml4_table, %edi
    xorl  %eax, %eax
    movl  $(6 * 4096 / 4), %ecx   /* zero pml4 + pdpt + 4×pd = 6 pages */
    rep   stosl
    ret

/* ── Build identity-map page tables ─────────────────────────────────── */
/*   FIX 3 – Use leal+orl so the full 32-bit (here: < 4 GB) address     */
/*   is stored correctly; the upper 32 bits of each 64-bit entry remain  */
/*   zero (physical addresses are < 4 GB at boot).                      */
/*                                                                       */
/*   FIX 7 – Identity-map the first 4 GiB, not just 1 GiB.               */
/*   The old code only built one PD table (512 × 2MiB = 1 GiB) and       */
/*   only ever wired up PDPT[0]. The VESA/VBE linear framebuffer BAR     */
/*   (reported by GRUB's multiboot2 framebuffer tag and consumed by      */
/*   graphics_init() in kernel.c) commonly sits well above 1 GiB — on    */
/*   this VirtualBox build it's at 0xE0000000 (3.5 GiB). The very first  */
/*   write to that framebuffer therefore hit an unmapped page. Because   */
/*   no IDT is loaded yet at this point in boot, the resulting #PF had   */
/*   nowhere to go, cascaded to #DF, and then triple-faulted immediately */
/*   (VirtualBox: "Guru Meditation ... VINF_EM_TRIPLE_FAULT", RIP        */
/*   pointing at the framebuffer store, idtr base/limit = 0).            */
/*   The guest CPU here doesn't expose 1 GiB pages (CPUID.80000001:EDX   */
/*   bit26 Page1GB = 0 for the guest), so we map with 2 MiB pages across */
/*   4 PD tables (4 PDPT entries) instead of using PDPTE.PS huge pages.  */
build_page_tables:
    /* PML4[0] -> pdpt_table | PRESENT | WRITE */
    leal  pdpt_table, %eax
    orl   $3, %eax
    movl  %eax, pml4_table

    /* PDPT[0..3] -> pd_table0..pd_table3 | PRESENT | WRITE
     * (pd_table0..3 are laid out contiguously in .bss, 4096 bytes apart) */
    leal  pd_table0, %eax
    orl   $3, %eax
    movl  %eax, pdpt_table

    leal  pd_table1, %eax
    orl   $3, %eax
    movl  %eax, pdpt_table+8

    leal  pd_table2, %eax
    orl   $3, %eax
    movl  %eax, pdpt_table+16

    leal  pd_table3, %eax
    orl   $3, %eax
    movl  %eax, pdpt_table+24

    /* pd_table0..3[0..511] -> 2048 × 2 MiB huge pages covering 0..4 GiB */
    xorl  %ecx, %ecx
build_pd_loop:
    movl  %ecx, %eax
    shll  $21, %eax
    orl   $0x83, %eax       /* PRESENT | WRITE | HUGE */
    movl  %eax, pd_table0(,%ecx,8)
    movl  $0,   pd_table0+4(,%ecx,8)   /* upper 32 bits = 0 */
    incl  %ecx
    cmpl  $2048, %ecx
    jne   build_pd_loop
    ret

/* ── 64-bit long-mode entry ──────────────────────────────────────────── */
.code64

/* ---- serial_putc64 / serial_puts64: COM1 byte/string send in
 * 64-bit mode. Same shape as serial_putc32/serial_puts32 above -
 * re-implemented per-bitness because a `call` across a code-width
 * change isn't valid, not because the logic differs at all.
 * IN (serial_putc64):  %al  = byte to send.
 * IN (serial_puts64):  %rsi = pointer to a NUL-terminated string.
 * Both preserve every register they use. ---- */
serial_putc64:
    pushq   %rdx
    pushq   %rax
serial_putc64_wait:
    movw    $0x3FD, %dx
    inb     %dx, %al
    testb   $0x20, %al
    jz      serial_putc64_wait
    popq    %rax
    movw    $0x3F8, %dx
    outb    %al, %dx
    popq    %rdx
    ret

serial_puts64:
    pushq   %rsi
    pushq   %rax
serial_puts64_loop:
    movb    (%rsi), %al
    testb   %al, %al
    jz      serial_puts64_done
    call    serial_putc64
    incq    %rsi
    jmp     serial_puts64_loop
serial_puts64_done:
    popq    %rax
    popq    %rsi
    ret

long_mode_start:
    /* Reload all data segment registers with the 64-bit data descriptor */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movq $stack_top, %rsp
    xorq %rbp, %rbp

    /* Checkpoint BEFORE loading the real kernel_main arguments below,
     * so this can freely use %rsi as its string-pointer argument
     * without needing to save/restore anything. */
    movq $msg_k5, %rsi
    call serial_puts64

    /* FIX 4 – Zero-extend 32-bit saved values into 64-bit registers.
     *          kernel_main(uint32_t magic, uint32_t info) uses the
     *          System V AMD64 ABI: first arg in %rdi, second in %rsi.  */
    movl multiboot_magic, %edi   /* zero-extends into %rdi */
    movl multiboot_info,  %esi   /* zero-extends into %rsi */

    call kernel_main

halt64:
    cli
    hlt
    jmp halt64

/* ── GDT ─────────────────────────────────────────────────────────────── */
/* FIX 5 – Correct 64-bit GDT encodings.
 *   Null descriptor  : all zeros
 *   Code descriptor  : L=1, D=0, G=1, P=1, S=1, Type=0xA (exec/read)
 *                      → 0x00AF9A000000FFFF
 *   Data descriptor  : G=1, P=1, S=1, Type=0x2 (read/write)
 *                      → 0x00CF92000000FFFF
 *
 * FIX 6 – Added the ring-3 entries SYSCALL/SYSRET's STAR MSR arithmetic
 * requires. syscall_init() (kernel/syscall.c) programs:
 *   STAR[47:32] = 0x0008  →  SYSCALL entry:  CS=0x08, SS=0x10 (indices 1,2 — already existed)
 *   STAR[63:48] = 0x001B  →  SYSRET  return:  CS=0x1B+16=0x2B, SS=0x1B+8=0x23
 * 0x2B and 0x23 are indices 5 and 4 — this table only had 3 entries
 * (indices 0-2), so the very first `sysretq` would fault trying to load
 * a segment descriptor past the end of the GDT. Index 3 is an
 * intentional unused placeholder — SYSRET's arithmetic anchors off it
 * but never actually loads it; it exists purely so 4 and 5 land where
 * the STAR value above expects. Index 4 = ring-3 data, index 5 = ring-3
 * 64-bit code (same encoding as kernel code/data, DPL bits changed from
 * 00 to 11: access byte 0x92→0xF2, 0x9A→0xFA). */
.align 16
gdt64:
    .quad 0x0000000000000000    /* 0x00 – null              */
    .quad 0x00AF9A000000FFFF    /* 0x08 – kernel 64-bit code */
    .quad 0x00CF92000000FFFF    /* 0x10 – kernel 64-bit data */
    .quad 0x0000000000000000    /* 0x18 – unused (SYSRET arithmetic placeholder, never loaded) */
    .quad 0x00CFF2000000FFFF    /* 0x20 – ring-3 data (DPL=3) */
    .quad 0x00AFFA000000FFFF    /* 0x28 – ring-3 64-bit code (DPL=3) */
gdt64_end:

gdt64_pointer:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

/* ── BSS (page tables + stack) ───────────────────────────────────────── */
.section .bss

.align 4096
pml4_table:
    .skip 4096

.align 4096
pdpt_table:
    .skip 4096

/* FIX 7 – four PD tables (2 MiB pages × 512 entries × 4 = 4 GiB total),
 * laid out contiguously so build_pd_loop can index across all of them
 * with a single pd_table0-relative offset. */
.align 4096
pd_table0:
    .skip 4096

.align 4096
pd_table1:
    .skip 4096

.align 4096
pd_table2:
    .skip 4096

.align 4096
pd_table3:
    .skip 4096

.align 16
stack_bottom:
    .skip 16384
stack_top:

/* ── Data (saved multiboot registers) ───────────────────────────────── */
.section .data

multiboot_magic:
    .long 0

multiboot_info:
    .long 0