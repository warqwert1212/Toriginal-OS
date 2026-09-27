
.code16
.section .text
.global _stage2_start

.set ELF_STAGE_ADDR,   0x4000000   /* FIX: was 0x20000 - a real-mode
                                      * segment:offset pair (what BIOS
                                      * INT 13h reads are always
                                      * limited to) can only reach just
                                      * over 1 MiB, nowhere near enough
                                      * for a kernel now budgeted up to
                                      * 32 MiB (see ata.h's
                                      * ATA_BOOT_RESERVED_SECTORS
                                      * comment). load_kernel_elf below
                                      * now reads each chunk into the
                                      * small low KERNEL_CHUNK_BUF_ADDR
                                      * buffer and copies it up here via
                                      * "unreal mode" instead of reading
                                      * the whole file directly to one
                                      * ever-growing real-mode segment.
                                      * Must match stage2_pm.s's
                                      * ELF_STAGE_ADDR exactly - see
                                      * that file's comment. */
.set KERNEL_LOAD_LBA,  65           /* kernel copy starts right after stage2's
                                      * 64 reserved sectors (1 for stage1 + 64
                                      * for stage2 = sector 65) - see
                                      * installer.c's write_bootloader_to_disk()
                                      * for the authoritative layout, which this
                                      * constant must always match exactly. */
.set KERNEL_MAX_SECTORS, 65536     /* 32 MiB ceiling for the staged kernel copy -
                                     * FIX: was 1024 (512 KiB), too small once
                                     * the kernel started embedding its own
                                     * boot assets. Matches kernel64.ld's own
                                     * __kernel_end ASSERT ceiling, and fits
                                     * comfortably inside the reserved boot
                                     * region (sectors 65..65600 fit within
                                     * the 70048-sector reservation - see
                                     * ata.h's ATA_BOOT_RESERVED_SECTORS - so
                                     * this never spills into TRPFS's
                                     * territory, which starts right at
                                     * sector 70048) */
.set KERNEL_CHUNK_BUF_ADDR, 0x20000 /* fixed low real-mode-reachable scratch
                                      * buffer each disk-read chunk lands in
                                      * (reused for every chunk - previously
                                      * this address was ELF_STAGE_ADDR itself
                                      * and grew across the whole read; now
                                      * each chunk gets copied out to high
                                      * memory immediately afterward, so more
                                      * than one chunk's worth of real-mode-
                                      * reachable memory here is never needed) */
.set KERNEL_CHUNK_MAX_SECTORS, 127  /* matches the conservative INT13h
                                      * extensions transfer-size limit already
                                      * used below */
.set MB2_INFO_ADDR,    0x30000     /* scratch buffer for the synthesized
                                     * multiboot2 info block, built in step 6 */
.set MB2_MAGIC,        0x36D76289
.set KERNEL_ENTRY_LINK_ADDR, 0x201000  /* must match kernel.elf's e_entry -
                                         * installer.c verifies this at
                                         * install time and refuses to
                                         * install a kernel that disagrees,
                                         * rather than silently jumping
                                         * into the wrong address */

/* Must match include/trbl_settings.h exactly - LBA, magic, and every
 * byte offset below are hand-kept in sync with that header (see its
 * comment block); there's no way for 16-bit real-mode asm to #include
 * a packed C struct, so this is the one place that duplication has to
 * live. If trbl_settings.h's layout ever changes, these constants
 * must change with it. */
.set TRBL_SETTINGS_LBA,    70000
.set TRBL_SETTINGS_MAGIC,  0x4C425254   /* "TRBL" little-endian */
.set TRBL_SETTINGS_VERSION, 1
.set TRBL_SETTINGS_SIZE,   512
.set TRBL_OFF_WIDTH,       8
.set TRBL_OFF_HEIGHT,      10
.set TRBL_OFF_CHECKSUM,    508          /* also = payload length to sum */
.set TRBL_SETTINGS_ADDR,   0x1B000      /* scratch buffer, real-mode reachable;
                                          * sits between stage2's own code+stack
                                          * (loaded at linear 0x9000, budget 32 KiB
                                          * of code + 0xFFF0 stack, so it never
                                          * runs past ~0x19000) and
                                          * KERNEL_CHUNK_BUF_ADDR (0x20000) below
                                          * - safely clear of both */

_stage2_start:
    /* Stage1 far-jumped here with CS=0x0900, so first thing: get every
     * segment register consistent with that same base, and set up our
     * own stack safely below us. */
    movw    %cs, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0xFFF0, %sp

    /* Preserve the boot drive BIOS handed to stage1 in DL; stage2 later
     * uses this saved value for its INT 13h LBA reads. The stage1->stage2
     * transition preserves registers across the far jump, so this is the
     * correct handoff, not a local zero-initialized copy. */
    movb    %dl, boot_drive

    movw    $msg_stage2, %si
    call    print_string

    /* ── Step 1: enable A20 (fast method) ─────────────────────────────── */
    inb     $0x92, %al
    testb   $0x02, %al
    jnz     a20_done
    orb     $0x02, %al
    andb    $0xFE, %al          /* never touch bit 0 (fast reset) */
    outb    %al, $0x92
a20_done:

    /* ── Step 2: load TRBL settings sector, apply width/height ─────────
     * Replaces the old install-time byte patch (installer.c used to
     * overwrite saved_width/saved_height directly inside the
     * assembled stage2.bin at a build-generated offset - see
     * stage2_offsets.h, now removed). stage2.bin is now install-
     * independent: this reads real, rewritable settings off disk at
     * every boot instead, so the shell's `settings resolution`
     * command takes effect on the very next boot with zero bootloader
     * reinstall. If the sector is missing/corrupt (fresh disk, or a
     * write interrupted by power loss), saved_width/saved_height keep
     * their compiled-in 1024x768 defaults below and boot continues -
     * never fatal, matching this loader's "degrade, don't halt on
     * anything but a real disk error" policy. */
    call    load_trbl_settings

    /* ── Step 3: load kernel ELF from disk ────────────────────────────── */
    /* FIX: this used to run AFTER VBE mode-set below. That's exactly
     * backwards: once vbe_set_mode succeeds, INT 10h AH=0x0E teletype
     * output stops being visible at all - there's no text-mode
     * character buffer to write into once the display is a linear
     * pixel framebuffer, so it silently does nothing. Every
     * print_string call after that point - "loading kernel...", any
     * disk error, the new BIOS-status/LBA diagnostics - was running
     * and (on failure) genuinely printing, just into a screen that
     * could no longer show it. That's what "shows text for a second
     * then black" actually was. Kernel loading now happens first,
     * while we can still see if it goes wrong. */
    call    load_kernel_elf
    jc      fatal_disk_error

    /* ── Step 4: VBE mode set + boot logo (framebuffer test) ──────────────
     * Runs last, right before the jump into the kernel - exactly the
     * "is the framebuffer actually working" check this was meant to
     * be, with no more text-mode diagnostics after it that could get
     * silently swallowed by the mode switch. */
    call    vbe_set_mode
    call    draw_logo

    /* ── Step 5: enter 32-bit protected mode ──────────────────────────── */
    cli
    lgdt    gdt32_pointer

    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0

    ljmp    $0x08, $(STAGE2_LINEAR_BASE + protected_mode_entry)

fatal_disk_error:
    movw    $msg_disk_err, %si
    call    print_string

    /* DIAGNOSTIC (temporary): print the BIOS status code and the LBA
     * being read when load_kernel_elf failed - see its comment for
     * why. disk_err_code/disk_err_lba are 0 if the failure was
     * anything else (e.g. load_trbl_settings), which is still useful
     * information. Remove once diagnosed. */
    movw    $msg_err_code, %si
    call    print_string
    movb    disk_err_code, %al
    call    print_hex_byte
    movw    $msg_err_lba, %si
    call    print_string
    movl    disk_err_lba, %eax
    call    print_hex_dword
    movw    $msg_crlf, %si
    call    print_string
stage2_halt:
    hlt
    jmp     stage2_halt

/* ---- print_hex_byte: prints AL as 2 hex digits via INT 10h teletype. */
print_hex_byte:
    pusha
    movb    %al, %bl
    shrb    $4, %al
    call    print_hex_nibble
    movb    %bl, %al
    andb    $0x0F, %al
    call    print_hex_nibble
    popa
    ret

/* ---- print_hex_dword: prints EAX as 8 hex digits via INT 10h teletype.
 * Explicit pushl/popl %ebx around the internal work below, in
 * addition to pusha/popa: this is .code16, so pusha/popa only saves
 * the 16-bit half of each register - not enough, since this function
 * does full 32-bit work in %ebx internally. Without this, a caller's
 * EBX would come back with its low 16 bits correctly restored but its
 * high 16 bits silently clobbered - exactly the bug class that broke
 * boot64.s's multiboot handoff earlier; worth closing here too rather
 * than leaving it dormant for whoever calls this next. */
print_hex_dword:
    pusha
    pushl   %ebx
    movl    %eax, %ebx
    movl    $8, %ecx
print_hex_dword_loop:
    movl    %ebx, %eax
    shll    $4, %ebx             /* shift next nibble into position for   */
    shrl    $28, %eax            /* the loop after, while this one reads  */
    andl    $0x0F, %eax          /* the current top nibble out of eax     */
    call    print_hex_nibble
    decl    %ecx
    jnz     print_hex_dword_loop
    popl    %ebx
    popa
    ret

/* ---- print_hex_nibble: prints the low nibble of AL as one hex digit. */
print_hex_nibble:
    pusha
    andb    $0x0F, %al
    cmpb    $10, %al
    jb      print_hex_nibble_digit
    addb    $('A' - 10), %al
    jmp     print_hex_nibble_out
print_hex_nibble_digit:
    addb    $'0', %al
print_hex_nibble_out:
    movb    $0x0E, %ah
    movw    $0, %bx
    int     $0x10
    popa
    ret

/* ── BIOS teletype string print (16-bit real mode) ───────────────────────
 * SI = pointer to NUL-terminated string. Same helper as stage1's -
 * duplicated rather than shared because stage1 and stage2 are two
 * independent flat binaries with no linking between them. */
/* Also mirrors every character to COM1 - see stage1.s's comment at
 * _start for why. The UART is already initialized by stage1 (its
 * state persists across the far jump into stage2), so no re-init
 * needed here. */
print_string:
    pusha
print_string_loop:
    lodsb
    testb   %al, %al
    jz      print_string_done
    pushw   %ax
    movb    $0x0E, %ah
    movw    $0x0007, %bx
    int     $0x10
    popw    %ax
    call    serial_putc
    jmp     print_string_loop
print_string_done:
    popa
    ret

/* ---- serial_putc: sends AL as one byte to COM1, polling the line
 * status register until the transmit holding register is empty. */
serial_putc:
    pushw   %dx
    pushw   %ax
    movw    $0x3FD, %dx
serial_putc_wait:
    inb     %dx, %al
    testb   $0x20, %al
    jz      serial_putc_wait
    popw    %ax
    movw    $0x3F8, %dx
    outb    %al, %dx
    popw    %dx
    ret

/* ── VBE mode set ─────────────────────────────────────────────────────────
 * Reads the resolution installer.c stamped into this stage2 image at
 * `saved_width`/`saved_height` (defaults to 1024x768 if never patched
 * - matching this codebase's original hardcoded GRUB request tag),
 * asks the BIOS's VBE interface for the mode list (function 00),
 * scans it for a mode matching that exact width/height/32bpp with
 * LFB support, sets it with LFB enabled (function 02, mode |
 * 0x4000), then re-queries the now-active mode (function 01) to read
 * back the real LFB physical address/pitch actually granted - never
 * assumed, always read back, same principle as vbe_dispi.c's
 * "confirm what the card actually accepted" check. Results are left
 * in the fb_* variables for step 6 to build the multiboot2 tag from.
 * Falls through leaving fb_found=0 if VBE isn't available or no mode
 * matches - kernel.c's parse_multiboot() already treats a missing
 * framebuffer tag as "skip graphics", not fatal. */
vbe_set_mode:
    pusha

    movw    $vbe_info_block, %di
    movw    $0x4F00, %ax
    int     $0x10
    cmpw    $0x004F, %ax
    jne     vbe_done_unavailable

    /* Mode list pointer is a real-mode far pointer at offset 0x0E
     * into the returned VBE info block. */
    movw    vbe_info_block+0x0E, %si
    movw    vbe_info_block+0x10, %ax
    movw    %ax, %es                 /* ES:SI now -> mode list */

vbe_scan_loop:
    movw    %es:(%si), %cx
    addw    $2, %si
    cmpw    $0xFFFF, %cx
    je      vbe_done_unavailable      /* end of list, nothing matched */

    pushw   %si
    pushw   %es
    movw    %cx, %bx
    movw    $vbe_mode_info, %di
    pushw   %ds
    popw    %es
    movw    $0x4F01, %ax
    int     $0x10
    popw    %es
    popw    %si
    cmpw    $0x004F, %ax
    jne     vbe_scan_loop

    /* Want: LFB-capable (attributes bit 7), exact width/height match,
     * 32 bits per pixel. */
    movw    vbe_mode_info+0x00, %ax
    testw   $0x0080, %ax
    jz      vbe_scan_loop
    movw    vbe_mode_info+0x12, %ax   /* x_resolution */
    cmpw    saved_width, %ax
    jne     vbe_scan_loop
    movw    vbe_mode_info+0x14, %ax   /* y_resolution */
    cmpw    saved_height, %ax
    jne     vbe_scan_loop
    movb    vbe_mode_info+0x19, %al   /* bits_per_pixel */
    cmpb    $32, %al
    jne     vbe_scan_loop

    movw    %cx, %bx
    orw     $0x4000, %bx
    movw    $0x4F02, %ax
    int     $0x10
    cmpw    $0x004F, %ax
    jne     vbe_done_unavailable

    movw    %cx, %bx
    movw    $vbe_mode_info, %di
    pushw   %ds
    popw    %es
    movw    $0x4F01, %ax
    int     $0x10
    cmpw    $0x004F, %ax
    jne     vbe_done_unavailable

    movl    vbe_mode_info+0x28, %eax  /* phys_base_ptr */
    movl    %eax, fb_addr
    movzwl  vbe_mode_info+0x10, %eax  /* bytes_per_scanline (pitch) */
    movl    %eax, fb_pitch
    movzwl  vbe_mode_info+0x12, %eax
    movl    %eax, fb_width
    movzwl  vbe_mode_info+0x14, %eax
    movl    %eax, fb_height
    movb    $32, fb_bpp
    movb    $1, fb_found

    movw    $msg_vbe_ok, %si
    call    print_string
    popa
    ret

vbe_done_unavailable:
    movw    $msg_vbe_fail, %si
    call    print_string
    popa
    ret

/* ── Load and apply the TRBL settings sector ──────────────────────────────
 * Reads LBA TRBL_SETTINGS_LBA into trbl_settings_buf via the same
 * INT 13h extended-read DAP pattern as load_kernel_elf, then:
 *   1. checks magic/version/size
 *   2. sums the first TRBL_OFF_CHECKSUM (508) bytes as 32-bit little-
 *      endian words (32-bit ADD, allowed to wrap - same rule
 *      trbl_settings.c's trbl_settings_checksum() follows in C, so
 *      both sides compute identically)
 *   3. compares against the stored checksum at that offset
 * On any mismatch, bails out immediately leaving saved_width/
 * saved_height at their compiled-in defaults - deliberately silent
 * beyond the one log line, since "no settings sector yet" is the
 * normal state for a disk nobody has customized. Never sets the carry
 * flag / never treated as a boot-fatal error by its caller. */
load_trbl_settings:
    pusha

    movw    $1, dap_count
    movl    $TRBL_SETTINGS_LBA, dap_lba
    movl    $0, dap_lba+4
    movw    $(TRBL_SETTINGS_ADDR >> 4), %ax
    movw    %ax, dap_seg

    movw    $dap, %si
    movb    $0x42, %ah
    movb    boot_drive, %dl
    int     $0x13
    jc      trbl_settings_absent        /* disk error reading it - treat as absent */

    pushw   %ds
    movw    $(TRBL_SETTINGS_ADDR >> 4), %ax
    movw    %ax, %ds

    cmpl    $TRBL_SETTINGS_MAGIC, (0)
    jne     trbl_settings_bad
    cmpw    $TRBL_SETTINGS_VERSION, (4)
    jne     trbl_settings_bad
    cmpw    $TRBL_SETTINGS_SIZE, (6)
    jne     trbl_settings_bad

    /* Checksum: sum bytes [0, TRBL_OFF_CHECKSUM) as 32-bit words. */
    xorl    %eax, %eax                  /* running sum */
    xorw    %si, %si                    /* byte offset within sector */
trbl_checksum_loop:
    cmpw    $TRBL_OFF_CHECKSUM, %si
    jae     trbl_checksum_done
    addl    (%si), %eax
    addw    $4, %si
    jmp     trbl_checksum_loop
trbl_checksum_done:
    cmpl    (TRBL_OFF_CHECKSUM), %eax
    jne     trbl_settings_bad

    /* Valid. Pull width/height into this file's own saved_width/
     * saved_height - popping %ds back to stage2's own segment first
     * so the write lands in *our* variables, not the settings buffer. */
    movw    (TRBL_OFF_WIDTH), %ax
    movw    (TRBL_OFF_HEIGHT), %bx
    popw    %ds
    movw    %ax, saved_width
    movw    %bx, saved_height

    movw    $msg_settings_ok, %si
    call    print_string
    popa
    ret

trbl_settings_bad:
    popw    %ds
trbl_settings_absent:
    movw    $msg_settings_default, %si
    call    print_string
    popa
    ret

/* ── Load the kernel ELF from disk into ELF_STAGE_ADDR (high memory) ──────
 * BIOS INT 13h extended reads (AH=0x42) can only target a destination
 * reachable by a plain real-mode segment:offset pair - a hard ceiling
 * just over 1 MiB, regardless of how large the disk transfer itself
 * is allowed to be. That's nowhere near enough now that the kernel
 * can be budgeted up to 32 MiB (see ata.h's ATA_BOOT_RESERVED_SECTORS
 * comment) - the old version of this routine grew its destination
 * segment across the *whole* read and would silently wrap/overrun
 * into the video memory hole at 0xA0000 well before finishing a
 * multi-MB kernel.
 *
 * Fixed by decoupling "where the BIOS writes" from "where the kernel
 * ends up": each chunk (still capped at 127 sectors - the same
 * conservative INT 13h extensions transfer-size limit as before)
 * lands in the small, fixed, low KERNEL_CHUNK_BUF_ADDR buffer, then
 * gets copied up to its real destination (ELF_STAGE_ADDR, growing by
 * one chunk's worth of bytes each time) using "unreal mode" (see
 * enter_unreal/leave_unreal below): DS/ES briefly get a flat, base-0,
 * 4 GiB-limit descriptor so a 32-bit `rep movsl` can address anywhere
 * in the low 4 GiB, then get restored to normal real-mode segments
 * before the next BIOS call. This is the standard "big real mode"
 * technique used by many real-mode bootloaders for exactly this
 * problem - reused here rather than invented, but hand-adapted to
 * this file and not yet boot-tested in an emulator (none was
 * available while writing this). If boot stalls or hangs somewhere
 * around "loading kernel...", that print not being followed by
 * "kernel loaded..." pinpoints this routine - report exactly what's
 * on screen at that point.
 *
 * IMPORTANT: DS must be a normal real-mode segment (matching CS)
 * whenever INT 13h runs - AH=0x42 addresses the DAP via DS:SI, so if
 * DS is still carrying unreal mode's flat (base-0) descriptor at that
 * point, the BIOS dereferences completely the wrong linear address
 * for the DAP and reads garbage LBA/count/segment fields instead of
 * the ones set up below. (An earlier version of this routine entered
 * unreal mode once before the whole loop and left it there straight
 * through every INT 13h call - exactly this bug - which is why
 * enter_unreal/leave_unreal now bracket only the copy, per chunk,
 * every iteration.)
 *
 * Sets CF on any read failure, matching the convention the caller
 * checks. */

/* ---- enter_unreal: give DS/ES gdt32's flat, base-0, 4 GiB-limit
 * descriptor. Call right before code that needs to address memory
 * above 1 MiB; always pair with leave_unreal before the next BIOS
 * interrupt - see the IMPORTANT note above for why. ---- */
enter_unreal:
    cli
    lgdt    gdt32_pointer
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    movw    $0x10, %ax              /* gdt32's flat 4 GiB data descriptor */
    movw    %ax, %ds
    movw    %ax, %es
    movl    %cr0, %eax
    andl    $0xFFFFFFFE, %eax
    movl    %eax, %cr0
    sti
    ret

/* ---- leave_unreal: restore DS/ES to normal real-mode segments
 * matching CS, undoing enter_unreal. Safe to call even if unreal mode
 * was never entered. ---- */
leave_unreal:
    movw    %cs, %ax
    movw    %ax, %ds
    movw    %ax, %es
    ret

/* ── Boot-time logo + progress bar ────────────────────────────────────────
 * Purely a visual "is it actually loading" signal - drawn straight to
 * the VBE linear framebuffer using the same unreal-mode technique as
 * load_kernel_elf's chunk copy (see enter_unreal/leave_unreal above),
 * since fb_addr is always well above the 1 MiB real-mode ceiling. Both
 * routines are no-ops if vbe_set_mode never found a usable mode
 * (fb_found==0) - falls back to text-only exactly as before. Not
 * boot-tested in an emulator (none was available while writing this),
 * same caveat as the rest of this file's unreal-mode code. */

.set LOGO_WIDTH,  120
.set LOGO_HEIGHT, 38
.set BAR_WIDTH,   300
.set BAR_HEIGHT,  24

/* ---- draw_logo: blits the embedded TRBL logo, horizontally centered,
 * a bit above the framebuffer's vertical center (leaving room for the
 * progress bar below it - see draw_progress_bar's bar_y). Call with
 * DS/ES normal (real-mode segments) - this brackets its own copy in
 * enter_unreal/leave_unreal, unlike draw_progress_bar below which
 * expects to already be inside such a bracket (see its own comment). */
draw_logo:
    pusha
    cmpb    $0, fb_found
    je      draw_logo_done

    call    enter_unreal

    /* edi = fb_addr + logo_y*fb_pitch + logo_x*4 */
    movl    fb_width, %eax
    subl    $LOGO_WIDTH, %eax
    shrl    $1, %eax                  /* eax = logo_x */
    imull   $4, %eax, %eax
    movl    fb_height, %ecx
    shrl    $2, %ecx                  /* ecx = logo_y = fb_height/4 */
    imull   fb_pitch, %ecx
    addl    %ecx, %eax
    addl    fb_addr, %eax
    movl    %eax, %edi

    movl    $LOGO_DATA, %esi
    movl    $LOGO_HEIGHT, %ecx
draw_logo_row:
    pushl   %ecx
    movl    $LOGO_WIDTH, %ecx
    cld
    rep movsl                          /* copies LOGO_WIDTH dwords, src->dst */
    popl    %ecx
    movl    fb_pitch, %eax
    subl    $(LOGO_WIDTH*4), %eax      /* %edi already advanced LOGO_WIDTH*4
                                         * bytes by rep movsl above - this
                                         * covers the rest of the stride to
                                         * reach the next row */
    addl    %eax, %edi
    decl    %ecx
    jnz     draw_logo_row

    call    leave_unreal

draw_logo_done:
    popa
    ret

/* ---- draw_progress_bar: fills a horizontal bar showing kernel-load
 * progress. IN: %eax = sectors done, %ebx = sectors total. No-op if
 * fb_found==0. Unlike draw_logo, this does NOT call enter_unreal
 * itself - its only caller (load_kernel_elf) already brackets its
 * chunk copy in unreal mode, and this runs inside that same bracket,
 * so no extra CR0.PE toggle is spent redrawing this every chunk. */
draw_progress_bar:
    pusha
    cmpb    $0, fb_found
    je      draw_bar_done

    /* fill_width = (done * BAR_WIDTH) / total, clamped to BAR_WIDTH */
    movl    %eax, %ecx
    imull   $BAR_WIDTH, %ecx, %ecx
    movl    %ecx, %eax
    xorl    %edx, %edx
    divl    %ebx
    cmpl    $BAR_WIDTH, %eax
    jbe     bar_width_ok
    movl    $BAR_WIDTH, %eax
bar_width_ok:
    movl    %eax, %esi                 /* esi = fill_width, pixels */

    /* edi = fb_addr + bar_y*fb_pitch + bar_x*4 */
    movl    fb_width, %eax
    subl    $BAR_WIDTH, %eax
    shrl    $1, %eax                   /* eax = bar_x */
    imull   $4, %eax, %eax
    movl    fb_height, %ecx
    shrl    $2, %ecx
    addl    $(LOGO_HEIGHT + 20), %ecx  /* ecx = bar_y, just under the logo */
    imull   fb_pitch, %ecx
    addl    %ecx, %eax
    addl    fb_addr, %eax
    movl    %eax, %edi

    movl    $BAR_HEIGHT, %ecx
draw_bar_row:
    pushl   %ecx
    pushl   %edi

    movl    %esi, %ecx
    cmpl    $0, %ecx
    je      bar_fill_done
    movl    $0x00CC7A29, %eax          /* BGRX: filled portion (accent) */
bar_fill_loop:
    movl    %eax, (%edi)
    addl    $4, %edi
    decl    %ecx
    jnz     bar_fill_loop
bar_fill_done:
    movl    $BAR_WIDTH, %ecx
    subl    %esi, %ecx
    cmpl    $0, %ecx
    je      bar_row_done
    movl    $0x00303030, %eax          /* BGRX: unfilled track */
bar_track_loop:
    movl    %eax, (%edi)
    addl    $4, %edi
    decl    %ecx
    jnz     bar_track_loop
bar_row_done:
    popl    %edi
    popl    %ecx
    movl    fb_pitch, %eax
    addl    %eax, %edi
    decl    %ecx
    jnz     draw_bar_row

draw_bar_done:
    popa
    ret

/* Raw BGRX32 pixel data, 120x38, pre-flattened onto a black background
 * and pre-converted from trbl_logo.png (see kernel/boot/assets/) to
 * exactly match graphics_rgb()'s in-memory byte layout - no per-pixel
 * channel shuffling needed at blit time, just a straight row copy. */
LOGO_DATA:
.incbin "trbl_logo_boot.bin"
LOGO_DATA_END:

load_kernel_elf:
    pusha
    movw    $msg_loading_kernel, %si
    call    print_string

    /* FIX: removed the AH=0x41 "check extensions present" handshake
     * that used to sit here - see stage1.s's matching comment. This
     * was the actual cause of "KERNEL DISK READ ERROR" reappearing:
     * load_trbl_settings, called just before this in the exact same
     * boot session, reads a sector with plain AH=0x42 and no such
     * check, and it works - proof this handshake was never needed on
     * this BIOS and was actively breaking the one read that had it. */

    movl    $KERNEL_MAX_SECTORS, %ecx   /* sectors still to read           */
    movl    $KERNEL_LOAD_LBA, %ebx      /* next LBA to read from           */
    movl    $ELF_STAGE_ADDR, %edi       /* next high-memory destination    */

load_loop:
    cmpl    $0, %ecx
    je      load_done

    movl    %ecx, %edx
    cmpl    $KERNEL_CHUNK_MAX_SECTORS, %edx
    jbe     load_chunk_size_ok
    movl    $KERNEL_CHUNK_MAX_SECTORS, %edx
load_chunk_size_ok:
    movw    %dx, dap_count

    movl    %ebx, dap_lba
    movl    $0, dap_lba+4
    movw    $(KERNEL_CHUNK_BUF_ADDR >> 4), dap_seg

    /* Record what we're about to ask for BEFORE the BIOS call, not
     * after. A real BIOS's extended-read handler (VirtualBox's
     * included - see stage1.s's comment on its AH=0x42 quirks) isn't
     * guaranteed to leave every register untouched on an error path;
     * if it clobbers EBX internally while reporting the failure,
     * reading EBX back afterward would show BIOS-side garbage instead
     * of the LBA we actually requested. This way the error report is
     * always accurate regardless of what the BIOS does. */
    movl    %ebx, disk_err_lba

    /* DS is a normal real-mode segment here (matching CS) - either
     * because we've never called enter_unreal yet this iteration, or
     * because the previous iteration's leave_unreal already restored
     * it - so this correctly addresses the real dap fields above. */
    movw    $dap, %si
    movb    $0x42, %ah
    movb    boot_drive, %dl
    int     $0x13
    jc      load_kernel_elf_read_failed

    /* Copy this chunk from the low buffer up to %edi using a flat
     * 32-bit copy (this is the actual unreal-mode payoff: %edi can be
     * anywhere in the low 4 GiB here, not just under 1 MiB). Bracketed
     * tightly around just this copy, per chunk - see the IMPORTANT
     * note above load_kernel_elf for why it can't just wrap the whole
     * loop. */
    call    enter_unreal
    movzwl  dap_count, %eax
    imull   $512, %eax, %ebp        /* bytes read this chunk (a multiple
                                      * of 4 - it's a sector count * 512 -
                                      * so the dword shift below is exact) */
    pushl   %ecx
    movl    %ebp, %ecx
    shrl    $2, %ecx
    movl    $KERNEL_CHUNK_BUF_ADDR, %esi
    cld
    rep movsl                       /* DS:ESI -> ES:EDI, flat, 32-bit */
    popl    %ecx
    movzwl  dap_count, %eax
    subl    %eax, %ecx              /* ecx = sectors still remaining, updated */

    /* Progress bar - still in unreal mode here, so this costs no extra
     * CR0.PE toggle: draw_progress_bar writes straight to the
     * framebuffer. Save %ebx/%ecx/%edi first (full 32-bit, via the
     * stack) since we're about to call into a routine that only
     * wraps itself in pusha/popa - in .code16 that only protects each
     * register's low 16 bits, and draw_progress_bar does real 32-bit
     * work internally on eax/ecx/esi/edi (framebuffer pointer math)
     * once fb_found=1. Right now this call is a no-op (fb_found is
     * still 0 here - VBE mode-set runs after kernel load, see step
     * 3/4 reordering above), so none of that code path actually
     * executes yet - but %edi in particular is this loop's live
     * kernel-copy destination pointer, and leaving it unprotected
     * would mean the instant fb_found becomes 1 here (a future
     * reorder, or any other caller), the next chunk would get copied
     * to a silently wrong address with no error message at all. Cheap
     * to just close now rather than leave it as a landmine. */
    pushl   %ebx
    pushl   %ecx
    pushl   %edi
    movl    $KERNEL_MAX_SECTORS, %eax
    movl    %eax, %ebx
    subl    %ecx, %eax
    call    draw_progress_bar
    popl    %edi
    popl    %ecx
    popl    %ebx

    call    leave_unreal

    movzwl  dap_count, %eax
    addl    %eax, %ebx
    addl    %ebp, %edi

    jmp     load_loop

load_done:
    movw    $msg_kernel_loaded, %si
    call    print_string
    popa
    clc
    ret

load_kernel_elf_fail:
    /* DS/ES are already normal here (see the comment at the top of
     * load_loop) - nothing to restore before returning. */
    popa
    stc
    ret

load_kernel_elf_read_failed:
    /* disk_err_lba was already recorded right before the int13 call
     * above (see that comment for why it isn't captured here, from
     * EBX, instead: EBX after a failing BIOS call isn't trustworthy).
     * disk_err_code just needs AH, captured here before popa
     * overwrites it. */
    movb    %ah, disk_err_code
    jmp     load_kernel_elf_fail


boot_drive:   .byte 0

.align 4
dap:
    .byte 0x10
    .byte 0
dap_count: .word 0
           .word 0        /* destination offset, always 0 - we page the segment instead */
dap_seg:   .word 0
dap_lba:   .quad 0

msg_stage2:          .asciz "TRBL stage2: real-mode init...\r\n"
msg_vbe_ok:          .asciz "TRBL stage2: VBE mode set\r\n"
msg_vbe_fail:        .asciz "TRBL stage2: VBE mode unavailable - continuing without graphics\r\n"
msg_loading_kernel:  .asciz "TRBL stage2: loading kernel...\r\n"
msg_kernel_loaded:   .asciz "TRBL stage2: kernel loaded, entering protected mode...\r\n"
msg_disk_err:        .asciz "TRBL stage2: KERNEL DISK READ ERROR\r\n"
/* DIAGNOSTIC (temporary) - see fatal_disk_error's comment. */
msg_err_code:        .asciz "  BIOS status (AH): 0x"
msg_err_lba:         .asciz "\r\n  attempted LBA:   0x"
msg_crlf:            .asciz "\r\n"
disk_err_code:       .byte 0
disk_err_lba:        .long 0
msg_settings_ok:     .asciz "TRBL stage2: settings sector loaded\r\n"
msg_settings_default:.asciz "TRBL stage2: no valid settings sector, using defaults\r\n"

/* Fallback resolution, used only when load_trbl_settings finds no
 * valid settings sector on disk (fresh install, or disk error) - the
 * normal path now overwrites these at runtime from the TRBL settings
 * sector (see load_trbl_settings above), not an install-time byte
 * patch into this binary. */
.align 2
saved_width:  .word 1024
saved_height: .word 768

.global fb_found
.global fb_addr
.global fb_pitch
.global fb_width
.global fb_height
.global fb_bpp

fb_found:  .byte 0
fb_addr:   .long 0
fb_pitch:  .long 0
fb_width:  .long 0
fb_height: .long 0
fb_bpp:    .byte 0

.align 16
vbe_info_block: .fill 512, 1, 0
vbe_mode_info:  .fill 256, 1, 0

/* ── 32-bit flat GDT for the protected-mode transition ────────────────────
 * Standard flat code/data descriptors, base 0 limit 4GiB - intentionally
 * NOT the kernel's own 64-bit GDT (boot64.s's gdt64, loaded later by
 * _start itself); this only needs to get _start running in valid
 * 32-bit protected mode, exactly what GRUB used to hand it. _start
 * never touches segment registers before loading its own
 * gdt64_pointer, so this GDT's job is done the moment we jump. */
.align 8
gdt32:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF         /* 0x08: flat 32-bit code, base 0, limit 4G */
    .quad 0x00CF92000000FFFF         /* 0x10: flat 32-bit data, base 0, limit 4G */
gdt32_end:

.set STAGE2_LINEAR_BASE, 0x9000   /* (STAGE2_LOAD_SEG << 4) = (0x0900 << 4) */

gdt32_pointer:
    .word gdt32_end - gdt32 - 1
    .long gdt32 + STAGE2_LINEAR_BASE  /* GDTR needs a linear address, but every
                                        * label in this file assembles relative
                                        * to offset 0 (no .org/link-base set for
                                        * this flat binary) - add stage2's actual
                                        * load base back in to get the real
                                        * linear address the CPU needs. */

/* ---- pm_serial_putc / pm_serial_puts: 32-bit-protected-mode COM1
 * byte/string send - the whole diagnostic system for this file.
 * IN (pm_serial_putc): %al  = byte to send.
 * IN (pm_serial_puts): %esi = pointer to a NUL-terminated string.
 * Both fully preserve every register they use. ---- */
.global pm_serial_putc
pm_serial_putc:
    pushl   %edx
    pushl   %eax
pm_serial_putc_wait:
    movw    $0x3FD, %dx
    inb     %dx, %al
    testb   $0x20, %al
    jz      pm_serial_putc_wait
    popl    %eax
    movw    $0x3F8, %dx
    outb    %al, %dx
    popl    %edx
    ret

.global pm_serial_puts
pm_serial_puts:
    pushl   %esi
    pushl   %eax
pm_serial_puts_loop:
    movb    (%esi), %al
    testb   %al, %al
    jz      pm_serial_puts_done
    call    pm_serial_putc
    incl    %esi
    jmp     pm_serial_puts_loop
pm_serial_puts_done:
    popl    %eax
    popl    %esi
    ret

msg_p1: .asciz "P1: entered protected mode, loading ELF segments\n"
msg_p2: .asciz "P2: ELF segments loaded, building multiboot2 info\n"
msg_p3: .asciz "P3: multiboot2 info built\n"
msg_p4: .asciz "P4: jumping to kernel entry\n"

/* =============================================================================
 * 32-bit protected mode continuation
 * ============================================================================= */
.code32
protected_mode_entry:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    $0x0007FFF0, %esp        /* flat 32-bit stack, well clear of the
                                       * ELF staging buffer (0x20000+) and
                                       * everything below it */

    movl    $msg_p1, %esi
    call    pm_serial_puts

    call    elf_load_segments

    movl    $msg_p2, %esi
    call    pm_serial_puts

    call    build_multiboot2_info

    movl    $msg_p3, %esi
    call    pm_serial_puts

    movl    $MB2_MAGIC, %eax
    movl    $MB2_INFO_ADDR, %ebx

    pushl   %eax
    pushl   %ebx
    movl    $msg_p4, %esi
    call    pm_serial_puts
    popl    %ebx
    popl    %eax

    ljmp    $0x08, $KERNEL_ENTRY_LINK_ADDR
