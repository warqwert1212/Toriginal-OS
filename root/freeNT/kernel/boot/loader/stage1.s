
.code16
.section .text
.global _start

.set STAGE2_LOAD_SEG,  0x0900   /* load stage2 at linear 0x9000 */
.set STAGE2_LOAD_OFF,  0x0000
.set STAGE2_START_LBA, 1        /* stage2 begins at sector 1, right after the MBR */
.set STAGE2_SECTORS,   64       /* 32 KiB - generous headroom for stage2's code */

_start:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7C00, %sp        /* stack grows down from right below us */
    sti

    /* BIOS is *supposed* to pass the boot drive number in DL, and
     * stage2 needs that value to keep reading the kernel from the
     * right drive - but not every BIOS reliably hands 0x80 here for
     * a hard-disk boot (observed in testing: VirtualBox's BIOS logged
     * "int13_diskette_function: unsupported AH=42" right after
     * "Booting from Hard Disk...", meaning our very first INT 13h
     * call landed with DL<0x80 - a floppy-range drive number - and
     * got routed into the floppy INT13h handler instead of the hard-
     * disk one). Since this bootloader only ever targets the disk it
     * was installed to (the first/only hard disk), don't trust DL at
     * all - hardcode 0x80, the standard fix real single-disk
     * bootloaders use for exactly this BIOS inconsistency. */
    movb    $0x80, boot_drive

    /* Force standard 80x25 16-color text mode (INT 10h AH=0x00,
     * AL=0x03) before printing anything. Every print_string call in
     * this bootloader assumes AH=0x0E teletype output is landing on a
     * visible text-mode screen - true after a real cold power-on, but
     * NOT guaranteed after a warm/soft reset: this OS's own desktop
     * switches the video adapter into a VBE linear-framebuffer
     * graphics mode, and BIOS soft resets don't reliably re-POST
     * video hardware back to text mode the way a full power cycle
     * does. Without this, stage1/stage2 can run and "print"
     * perfectly correctly into a text buffer that isn't what's
     * actually being scanned out - producing a genuinely black
     * screen with zero visible output despite nothing being wrong
     * with the code. This makes the screen state deterministic
     * regardless of what the previous boot left behind. */
    movb    $0x00, %ah
    movb    $0x03, %al
    int     $0x10

    movw    $msg_stage1, %si
    call    print_string

    /* Check Int 13h extensions are present before ever using them
     * (AH=0x42/0x43). Not strictly mandated by every version of the
     * spec, but every real-world bootloader reference does this
     * handshake first, and this code skipped it entirely - a real
     * gap worth closing regardless of whether it's the exact cause
     * of the "int13_diskette_function: unsupported AH=42" failure
     * seen in testing. */
    movw    $0x55AA, %bx
    movb    $0x41, %ah
    movb    boot_drive, %dl
    int     $0x13
    jc      disk_error
    cmpw    $0xAA55, %bx
    jne     disk_error

    /* ── Load stage2 via INT 13h extensions (AH=0x42, LBA packet) ────── */
    movw    $dap, %si
    movb    $0x42, %ah
    movb    boot_drive, %dl
    int     $0x13
    jc      disk_error

    movw    $msg_jump, %si
    call    print_string

    /* Far jump into stage2 at STAGE2_LOAD_SEG:STAGE2_LOAD_OFF */
    ljmp    $STAGE2_LOAD_SEG, $STAGE2_LOAD_OFF

disk_error:
    movw    $msg_disk_err, %si
    call    print_string
halt:
    hlt
    jmp     halt

/* ── BIOS teletype string print (16-bit real mode) ────────────────────────
 * SI = pointer to NUL-terminated string */
print_string:
    pusha
print_string_loop:
    lodsb
    testb   %al, %al
    jz      print_string_done
    movb    $0x0E, %ah
    movw    $0x0007, %bx
    int     $0x10
    jmp     print_string_loop
print_string_done:
    popa
    ret

boot_drive:   .byte 0

/* ── INT 13h extended-read Disk Address Packet ───────────────────────────── */
.align 4
dap:
    .byte 0x10                     /* packet size */
    .byte 0                        /* reserved */
    .word STAGE2_SECTORS           /* sectors to read */
    .word STAGE2_LOAD_OFF          /* destination offset */
    .word STAGE2_LOAD_SEG          /* destination segment */
    .quad STAGE2_START_LBA         /* starting LBA (64-bit) */

msg_stage1:   .asciz "TRBL stage1: loading stage2...\r\n"
msg_jump:     .asciz "TRBL stage1: jumping to stage2\r\n"
msg_disk_err: .asciz "TRBL stage1: DISK READ ERROR\r\n"

/* ── MBR partition table (bytes 446-509 / 0x1BE-0x1FD) ────────────────────
 * Some real BIOS/firmware implementations validate that a disk has at
 * least one active (bootable) partition table entry before treating
 * it as bootable at all - checking only the 0x55AA signature two
 * bytes later isn't universally sufficient, even though this project
 * doesn't otherwise use real MBR partitioning anywhere (TRPFS and the
 * reserved boot region use a fixed sector-offset convention instead -
 * see ata.h's ATA_BOOT_RESERVED_SECTORS - not partition entries). This
 * single entry exists purely so BIOS's own partition-table sanity
 * check passes; nothing in this OS's boot path or filesystem code
 * ever reads it. Marked active (0x80), type 0x83 (a generic/Linux-
 * style filesystem type byte - arbitrary, since nothing interprets
 * it), spanning the whole disk from LBA 0. CHS fields are dummy
 * values (0x00/0x01/0x00 and 0xFF/0xFF/0xFF) since every BIOS made in
 * the last 25+ years uses the LBA fields instead - real installers
 * (e.g. any modern Linux distro's) do exactly the same thing for
 * this same reason. */
.org 446
    .byte 0x80                  /* status: active/bootable */
    .byte 0x00, 0x01, 0x00      /* CHS start (dummy, LBA fields used instead) */
    .byte 0x83                  /* partition type (arbitrary, unread) */
    .byte 0xFF, 0xFF, 0xFF      /* CHS end (dummy) */
    .long 0                     /* LBA of first sector: 0 (whole disk) */
    .long 0x00FFFFFF            /* sector count: a large placeholder - real
                                  * size varies per install and nothing reads
                                  * this field either, so an oversized-but-
                                  * plausible value is fine; BIOS validates
                                  * the entry's presence/shape, not that this
                                  * number matches the disk's real geometry */

/* Remaining 3 partition table slots: all-zero (unused) entries, which
 * is the standard, correct way to mark "no partition" for slots 2-4. */
.org 462
    .fill 48, 1, 0

/* ── Pad to 510 bytes, then the mandatory 0x55AA boot signature ───────────
 * '. - _stage1_start' is this section's length so far; pad up to byte
 * 510 (0x1FE), then emit the 2-byte signature BIOS requires at
 * exactly offset 510-511 for this sector to be recognized as
 * bootable at all. */
.org 510
.word 0xAA55
