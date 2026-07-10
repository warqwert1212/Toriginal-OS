/* ── Multiboot2 magic numbers ─────────────────────────────────────────── */
.set MB2_MAGIC,      0xe85250d6
.set MB2_ARCH,       0          /* i386 protected-mode */
.set MB2_LENGTH,     (multiboot_end - multiboot_header)
.set MB2_CHECKSUM,   -(MB2_MAGIC + MB2_ARCH + MB2_LENGTH)

/* ── Multiboot2 header ────────────────────────────────────────────────── */
/* FIX: ".section .multiboot" alone has NO section flags, which means the
 * linker does not mark it ALLOC — without ALLOC, the section is excluded
 * from every PT_LOAD segment's virtual-address range entirely (even
 * with an explicit PHDRS assignment), so it gets shoved to an arbitrary
 * file offset far past the first 8 KiB GRUB scans for the multiboot2
 * header, and the image silently fails to boot ("no bootable medium").
 * "a" = allocatable (occupies memory at runtime), "x" = executable
 * (matches the surrounding .text segment so it shares the same PT_LOAD
 * region instead of forcing a second loadable segment). */
.section .multiboot, "ax"
.align 8

multiboot_header:
    .long  MB2_MAGIC
    .long  MB2_ARCH
    .long  MB2_LENGTH
    .long  MB2_CHECKSUM

    /* ── Framebuffer request tag (type=5) ─────────────────────────────
     * Asks GRUB to switch to a linear VESA/VBE framebuffer BEFORE
     * jumping to our entry point, and to hand us a framebuffer info
     * tag (type=8) in the multiboot info struct so kernel.c can read
     * the physical address/pitch/depth without touching real-mode
     * VBE calls ourselves. Requested size is a *hint* — GRUB picks
     * the closest mode it can actually provide and reports the real
     * dimensions back in the type=8 tag, so graphics_init() must
     * trust that tag's values, not these requested ones.
     * flags=0 (not optional): if GRUB can't provide a framebuffer at
     * all it should fall back to EGA text (handled defensively in
     * graphics_init() by checking framebuffer_type). */
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

/* ── 32-bit entry point ──────────────────────────────────────────────── */
.section .text
.code32

.global _start
.extern kernel_main

_start:
    cli
    cld

    /* Save multiboot registers before we clobber eax/ebx */
    movl %eax, multiboot_magic
    movl %ebx, multiboot_info

    movl $stack_top, %esp

    call check_cpuid
    call check_long_mode

    call clear_page_tables
    call build_page_tables

    /* Enable PAE (bit 5) + OSFXSR (bit 9) + OSXMMEXCPT (bit 10).
     *
     * OSFXSR/OSXMMEXCPT are required before any fxsave/fxrstor
     * instruction can execute without raising #UD - added here
     * because scheduler_yield() (process.c) now does a real context
     * switch that fxsave/fxrstor's each process's FPU/SSE state on
     * every timer tick. Without these bits set, the very first timer
     * interrupt after the scheduler starts running processes would
     * execute fxsave, take #UD (vector 6), and panic() the kernel -
     * this is exactly the class of "worked in isolation, silently
     * broke on first real preemption" bug worth preventing at the
     * source rather than debugging after the fact. */
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

    /* Enable Paging + Protected Mode (existing), and additionally:
     *   - clear CR0.EM (bit 2): EM=1 would make every FPU/SSE
     *     instruction raise #UD ("emulate in software" mode) - must
     *     be 0 for fxsave/fxrstor/SSE to execute on real hardware.
     *   - set CR0.MP (bit 1): required alongside TS-based lazy FPU
     *     context switching conventions; without it, WAIT/FWAIT
     *     instructions won't respect CR0.TS the way the ISA expects.
     * Net effect vs the original 0x80000001 mask: bit 1 (MP) is
     * newly set, bit 2 (EM) is explicitly left clear (it already
     * defaults to 0 out of reset in practice, but we clear it
     * explicitly via AND rather than relying on that assumption). */
    movl %cr0, %eax
    orl  $0x80000003, %eax   /* PG | PE | MP */
    andl $0xFFFFFFFB, %eax   /* clear EM (bit 2) explicitly */
    movl %eax, %cr0

    lgdt gdt64_pointer
    ljmp $0x08, $long_mode_start

/* ── Hang forever ────────────────────────────────────────────────────── */
hang:
    cli
1:
    hlt
    jmp 1b

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
    jz    hang
    ret

/* ── Long-mode availability check ───────────────────────────────────── */
check_long_mode:
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb   hang
    movl $0x80000001, %eax
    cpuid
    testl $0x20000000, %edx
    jz   hang
    ret

/* ── FIX 2 – Zero ALL three page-table levels ───────────────────────── */
/*   The original only zeroed pml4_table; pdpt_table and pd_table        */
/*   remained uninitialised, producing random present-bit patterns and   */
/*   an immediate triple fault on first TLB miss.                        */
clear_page_tables:
    movl  $pml4_table, %edi
    xorl  %eax, %eax
    movl  $(3 * 4096 / 4), %ecx   /* zero pml4 + pdpt + pd = 3 pages */
    rep   stosl
    ret

/* ── Build identity-map page tables ─────────────────────────────────── */
/*   FIX 3 – Use leal+orl so the full 32-bit (here: < 4 GB) address     */
/*   is stored correctly; the upper 32 bits of each 64-bit entry remain  */
/*   zero (physical addresses are < 4 GB at boot).                      */
build_page_tables:
    /* PML4[0] -> pdpt_table | PRESENT | WRITE */
    leal  pdpt_table, %eax
    orl   $3, %eax
    movl  %eax, pml4_table

    /* PDPT[0] -> pd_table | PRESENT | WRITE */
    leal  pd_table, %eax
    orl   $3, %eax
    movl  %eax, pdpt_table

    /* PD[0..511] -> 512 × 2 MiB huge pages covering first 1 GiB */
    xorl  %ecx, %ecx
build_pd_loop:
    movl  %ecx, %eax
    shll  $21, %eax
    orl   $0x83, %eax       /* PRESENT | WRITE | HUGE */
    movl  %eax, pd_table(,%ecx,8)
    movl  $0,   pd_table+4(,%ecx,8)   /* upper 32 bits = 0 */
    incl  %ecx
    cmpl  $512, %ecx
    jne   build_pd_loop
    ret

/* ── 64-bit long-mode entry ──────────────────────────────────────────── */
.code64

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

.align 4096
pd_table:
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
