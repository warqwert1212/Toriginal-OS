

.code32
.section .text
.global build_multiboot2_info

.set MB2_INFO_ADDR,  0x30000

/* Same link-origin-vs-runtime-load mismatch as stage2.ld documents
 * for gdt32_pointer: this file links at address 0 (see stage2.ld)
 * but stage2 actually runs loaded at linear 0x9000. fb_found/
 * fb_addr/fb_pitch/fb_width/fb_height/fb_bpp are real-mode
 * variables stage2.s's vbe_set_mode wrote using DS=0x0900 segment
 * addressing (so their real runtime location is 0x9000+offset) -
 * reading them here as bare symbols would use the link-time
 * offset-from-0 value instead, landing on unrelated low memory and
 * picking up zeros/garbage instead of the real VBE-computed values.
 * Every read below adds STAGE2_LINEAR_BASE for exactly that reason. */
.set STAGE2_LINEAR_BASE, 0x9000

/* Elf64_Ehdr field offsets used here */
.set EHDR_E_ENTRY, 24
.set EHDR_E_PHOFF, 32
.set EHDR_E_PHENTSIZE, 54
.set EHDR_E_PHNUM, 56

/* Elf64_Phdr field offsets */
.set PHDR_P_TYPE,   0
.set PHDR_P_OFFSET, 8
.set PHDR_P_VADDR,  16
.set PHDR_P_FILESZ, 32
.set PHDR_P_MEMSZ,  40
.set PT_LOAD, 1

/* pm_serial_putc/pm_serial_puts live in stage2.s (same .code32 flat
 * binary, linked together) - that's the whole diagnostic system: one
 * putc for a single byte, one puts for a fixed string. Safe to use
 * freely in here even though esi is a live loop register below -
 * every checkpoint sits at a point where esi is about to be
 * recomputed anyway (see the "ESI is no longer the ELF base..."
 * comment further down), so pm_serial_puts clobbering it costs
 * nothing. */
.extern pm_serial_putc
.extern pm_serial_puts

/* ── multiboot2 info block builder ─────────────────────────────────────────
 * Layout (matches kernel.c's mb2_tag_t/mb2_tag_fb_t exactly):
 *   [0]  total_size (u32)   - filled in last, once the real size is known
 *   [4]  reserved (u32)     = 0
 *   [8]  boot loader name tag: type=2 size=16, "TRBL\0\0\0\0"
 *   [..] framebuffer tag (only emitted if fb_found != 0):
 *          type=8 size=32 (padded from 27 to the next 8-byte boundary)
 *          framebuffer_addr (u64), pitch (u32), width (u32),
 *          height (u32), bpp (u8), fb_type=1 (u8), reserved (u8),
 *          + 5 bytes padding to reach the 32-byte aligned tag size
 *   [..] end tag: type=0 size=8
 */
build_multiboot2_info:
    pushal

    movl    $MB2_INFO_ADDR, %edi
    addl    $8, %edi                   /* leave room for total_size+reserved */

    /* Boot-loader-name tag (type=2) - "TRBL", NUL-padded to the next
     * 8-byte boundary. GRUB never sets this to "TRBL" (it identifies
     * itself, e.g. "GRUB 2.06"), so the kernel can tell a direct TRBL
     * boot apart from booting via the install ISO's GRUB - see
     * kernel.c's show_boot_logo() gating, which should only run the
     * framebuffer-test logo on a real TRBL boot, not during install. */
    movl    $2, (%edi)                 /* type = MB2_TAG_BOOT_LOADER_NAME */
    movl    $16, 4(%edi)               /* size = 16 (8 hdr + 5 str + 3 pad) */
    movb    $'T', 8(%edi)
    movb    $'R', 9(%edi)
    movb    $'B', 10(%edi)
    movb    $'L', 11(%edi)
    movb    $0, 12(%edi)
    movb    $0, 13(%edi)
    movb    $0, 14(%edi)
    movb    $0, 15(%edi)
    addl    $16, %edi

    cmpb    $0, fb_found + STAGE2_LINEAR_BASE
    je      no_fb_tag

    movl    $8, (%edi)                 /* type = MB2_TAG_FB */
    movl    $32, 4(%edi)               /* size = 32 (27 rounded to 8) */
    movl    fb_addr + STAGE2_LINEAR_BASE, %eax
    movl    %eax, 8(%edi)              /* framebuffer_addr low 32 bits */
    movl    $0, 12(%edi)               /* framebuffer_addr high 32 bits */
    movl    fb_pitch + STAGE2_LINEAR_BASE, %eax
    movl    %eax, 16(%edi)
    movl    fb_width + STAGE2_LINEAR_BASE, %eax
    movl    %eax, 20(%edi)
    movl    fb_height + STAGE2_LINEAR_BASE, %eax
    movl    %eax, 24(%edi)
    movb    fb_bpp + STAGE2_LINEAR_BASE, %al
    movb    %al, 28(%edi)              /* framebuffer_bpp */
    movb    $1, 29(%edi)               /* framebuffer_type = 1 (direct RGB) */
    movb    $0, 30(%edi)               /* reserved */
    movb    $0, 31(%edi)               /* padding to 32-byte tag size */
    addl    $32, %edi

no_fb_tag:
    /* Memory-map tag (type 6) from the BIOS E820 map collected by
     * stage2.s (real mode, 0x1D000): dword count, then 24-byte entries
     * { base64, len64, type32, reserved32 } - already the multiboot2
     * entry layout, so it is a straight copy. */
    movl    $0x1D000, %ecx
    movl    (%ecx), %ecx
    testl   %ecx, %ecx
    jz      no_mmap_tag
    cmpl    $32, %ecx
    jbe     mmap_count_ok
    movl    $32, %ecx
mmap_count_ok:
    leal    (%ecx,%ecx,2), %eax        /* 3n */
    shll    $3, %eax                   /* 24n */
    addl    $16, %eax                  /* + tag header */
    movl    $6, (%edi)                 /* type = MB2_TAG_MMAP */
    movl    %eax, 4(%edi)              /* size */
    movl    $24, 8(%edi)               /* entry_size */
    movl    $0, 12(%edi)               /* entry_version */
    addl    $16, %edi
    movl    $(0x1D000 + 8), %esi
    leal    (%ecx,%ecx,2), %ecx        /* 3n */
    shll    $1, %ecx                   /* 6n dwords */
    cld
    rep movsl
no_mmap_tag:
    /* End tag: type=0, size=8 */
    movl    $0, (%edi)
    movl    $8, 4(%edi)
    addl    $8, %edi

    /* total_size = EDI - MB2_INFO_ADDR */
    movl    %edi, %eax
    subl    $MB2_INFO_ADDR, %eax
    movl    $MB2_INFO_ADDR, %edi
    movl    %eax, (%edi)
    movl    $0, 4(%edi)

    popal
    ret

/* fb_* variables are defined in stage2.s (vbe_set_mode fills them in
 * real mode); referenced here by name since both files are assembled
 * and linked together into one flat binary. */
.extern fb_found
.extern fb_addr
.extern fb_pitch
.extern fb_width
.extern fb_height
.extern fb_bpp
