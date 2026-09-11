/* =========================================================================
 * kernel64.ld  —  freeNT / Toriginal OS linker script
 * Target : x86-64 bare-metal, loaded by GRUB multiboot2
 *
 * IMPROVEMENTS vs original:
 *   1. Added __kernel_start / __kernel_end symbols so the kernel can report
 *      its own code size and mark its own pages as used in the PMM.
 *   2. Per-section _start / _end symbols (__text_start, __rodata_start,
 *      __data_start, __bss_start, __bss_end) — used by memset-BSS code and
 *      future page-attribute changes (NX on rodata, etc.).
 *   3. .data section now explicitly includes .data.rel.ro (read-only after
 *      relocation — GOT, vtables) and the stub_table array from
 *      interrupts.c which must live in a writable + loaded section.
 *   4. .bss section size is exposed via __bss_size for the BSS-zeroing loop
 *      that runs before kernel_main (if boot64.s ever adds one).
 *   5. PROVIDE() guards on stack_top / stack_bottom so boot64.s symbols
 *      take priority but the linker does not error when they are absent.
 *   6. /DISCARD/ is expanded to include DWARF debug sections in release
 *      builds, keeping the ISO image small.  To retain debug info, comment
 *      out the .debug_* lines.
 *   7. ASSERT() at the end verifies the image does not overflow the 16 MiB
 *      window that the identity-mapped huge pages cover before the heap
 *      begins.
 * ========================================================================= */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

/* ── Explicit program headers ────────────────────────────────────────────
 * Gives precise control over segment permissions (R+X text, R-only
 * rodata, R+W data — proper W^X separation instead of one big RWX blob).
 *
 * NOTE: the original bug that kept .multiboot out of any PT_LOAD segment
 * (and therefore ~48 KiB into the file, past GRUB's 8 KiB header-scan
 * window) was NOT a PHDRS problem — it was that kernel/boot/boot64.s
 * declared ".section .multiboot" with no flags, so the assembler never
 * marked it ALLOC, and a non-ALLOC section is excluded from every
 * PT_LOAD segment's address range regardless of linker-script segment
 * assignment. The real fix is boot64.s now using
 * ".section .multiboot, \"ax\"" (alloc + executable). PHDRS stays for
 * the permission-separation benefit, not as the fix for that bug. */
PHDRS
{
    text    PT_LOAD FLAGS(5) ;   /* R+X: .multiboot + .text             */
    rodata  PT_LOAD FLAGS(4) ;   /* R                                    */
    data    PT_LOAD FLAGS(6) ;   /* R+W: .data + .bss                    */
    /* FIX (overlap bug): .asset_blobs/.kernel_blob are placed AFTER
     * .trp_code_area, i.e. after every section already assigned to
     * `data` below - reusing `:rodata` for them made that PT_LOAD's
     * address range (first .rodata section -> last .kernel_blob
     * section) fully swallow the `data` PT_LOAD's range in between,
     * since a PHDR segment's bounds are just its first/last assigned
     * section, not literal contiguous membership. GRUB's multiboot2
     * loader allocates one chunk per PT_LOAD and refuses to load two
     * that overlap ("error: overlap detected."). Giving these two
     * sections their own PT_LOAD keeps every segment's address range
     * disjoint again. */
    assets  PT_LOAD FLAGS(4) ;   /* R: .asset_blobs + .kernel_blob       */
    stack   PT_GNU_STACK FLAGS(6) ;
}

SECTIONS
{
    /* ── Load address ────────────────────────────────────────────────────
     * GRUB multiboot2 loads the kernel binary at 2 MiB (0x200000).
     * Every subsequent section is aligned to a 4 KiB page boundary so the
     * kernel can set per-page attributes in the PMM and page tables. */
    . = 0x200000;

    __kernel_start = .;

    /* ── .multiboot — must be within first 8 KiB of the ELF ────────────
     * GRUB searches the first 8 KiB of the image for the multiboot2
     * header.  KEEP() prevents the linker from discarding it as unused.
     * Explicitly assigned to the `text` PT_LOAD segment (see PHDRS above)
     * so it is guaranteed to be first in the file, not just first in
     * virtual-address order. */
    .multiboot ALIGN(8) : 
    {
        KEEP(*(.multiboot))
    } :text

    /* ── .text — executable code ─────────────────────────────────────── */
    .text ALIGN(4K) :
    {
        __text_start = .;
        *(.text._start)         /* guarantee _start is first                */
        *(.text*)
        *(.gnu.linkonce.t*)
        __text_end = .;
    } :text

    /* ── .rodata — read-only data (strings, constant tables) ─────────── */
    .rodata ALIGN(4K) :
    {
        __rodata_start = .;
        *(.rodata*)
        *(.gnu.linkonce.r*)
        __rodata_end = .;
    } :rodata

    /* ── .data — initialised read-write data ────────────────────────── */
    .data ALIGN(4K) :
    {
        __data_start = .;

        *(.data*)
        *(.gnu.linkonce.d*)

        /* Read-only after relocation (GOT entries, C++ vtables, etc.).
         * Placed in .data because the kernel does not have a dynamic
         * linker to write-protect them after startup. */
        *(.data.rel.ro*)
        *(.data.rel*)

        /* The interrupt stub table is a writable, non-BSS array that
         * interrupts.c fills at runtime with machine code.  It must land
         * in a section that is both present in the binary AND writable. */
        *(.stub_table)

        __data_end = .;
    } :data

    /* ── .bss — zero-initialised data ───────────────────────────────── *
     * The boot stub (boot64.s) does not explicitly zero BSS today.
     * These symbols let you add a zeroing loop cheaply in the future:
     *
     *   extern char __bss_start[], __bss_end[];
     *   memset(__bss_start, 0, __bss_end - __bss_start);
     *
     * In the meantime, QEMU / real hardware clears physical memory on
     * cold boot, so this is safe for v1.                                */
    .bss ALIGN(4K) :
    {
        __bss_start = .;
        *(COMMON)               /* tentative definitions (old-style C)      */
        *(.bss*)
        *(.gnu.linkonce.b*)
        __bss_end = .;
    } :data

    __bss_size   = __bss_end - __bss_start;

    /* ── .trp_code_area — fixed load address for .trp binary payloads ──
     * trploader.c's trp_exec_bin() always copies a package's compiled
     * binary payload here and jumps to this exact address, every time.
     * Pinned to a literal, permanent address rather than "wherever .bss
     * happens to end", because that would shift every time the kernel
     * is rebuilt with different code — silently invalidating every
     * .trp binary compiled against the old address.
     *
     * Moved from 0x300000 to 0x500000 (5 MiB) after the GUI/graphics
     * work pushed __bss_end past the old boundary — the ASSERT below
     * caught it at build time exactly as intended, rather than
     * silently corrupting memory at runtime. Generous headroom this
     * time (kernel would need to grow past 5 MiB before hitting this
     * again) since graphics/GUI code is exactly the kind of thing that
     * keeps growing. The ASSERT stays in place as a tripwire regardless.
     *
     * This must match TRP_CODE_MAX_SIZE in trploader.c, and the trpc
     * host-side compiler's -Ttext=0x500000 so compiled addresses and
     * the runtime load address always agree. */
    . = 0x500000;
    .trp_code_area ALIGN(4K) (NOLOAD) :
    {
        __trp_code_area_start = .;
        . += 0x100000; /* 1 MiB */
        __trp_code_area_end = .;
    } :data

    /* ── .asset_blobs — embedded wallpapers/start-menu-images/cursors/
     * boot logo (kernel/boot/asset_blob.s, kernel/boot/trbl_logo_blob.s)
     * ──────────────────────────────────────────────────────────────────
     * Deliberately placed AFTER .trp_code_area, not folded into the
     * ordinary .rodata section above with everything else - that
     * section sits packed right up against .trp_code_area's pinned
     * 0x500000 boundary, so anything added there directly risks
     * pushing the kernel over that boundary and forcing a breaking
     * TRP_CODE_LOAD_ADDR change (see the ASSERT below, and the
     * comment on .trp_code_area itself, for why that address must
     * never move casually - every already-compiled .trp binary is
     * linked against it). These assets can only grow as more/larger
     * wallpapers get added, so they get their own section here,
     * clear of that constraint, growing harmlessly toward the 16 MiB
     * heap-window ceiling instead. */
    .asset_blobs ALIGN(4K) :
    {
        __asset_blobs_start = .;
        *(.asset_blobs)
        __asset_blobs_end = .;
    } :assets

    /* ── .kernel_blob — TRBL stage1/stage2 + the embedded PASS-1 kernel
     * copy (kernel/boot/bootloader_blobs.s) ─────────────────────────────
     * Same reasoning as .asset_blobs directly above: bootloader_blobs.o
     * is PASS-2-only and embeds a full copy of PASS 1's own kernel.elf
     * (installer.c writes these bytes straight to disk) - by
     * definition at least as large as the kernel image itself, so it
     * can never safely share the tightly-packed .rodata sitting right
     * up against .trp_code_area's pinned boundary. Placed after
     * .asset_blobs for no reason other than declaration order - both
     * sections are equally safe out here, clear of every fixed
     * address anything else depends on. */
    .kernel_blob ALIGN(4K) :
    {
        __kernel_blob_start = .;
        *(.kernel_blob)
        __kernel_blob_end = .;
    } :assets

    __kernel_end = .;

    /* ── Stack ───────────────────────────────────────────────────────── *
     * boot64.s defines stack_bottom / stack_top in its own .bss block.
     * PROVIDE() makes these symbols available to C code (e.g. for stack
     * overflow detection) without conflicting when boot64.s already
     * defines them.                                                      */
    PROVIDE(stack_bottom = __bss_end);
    PROVIDE(stack_top    = __bss_end + 16K);

    /* ── Discard ─────────────────────────────────────────────────────── *
     * Keep the ISO small.  Comment out .debug_* lines if you need GDB
     * symbols (also remove the /DISCARD/ wrapper for those lines).      */
    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note*)               /* build-id, ABI tags, GNU stack note       */
        *(.debug_info)
        *(.debug_abbrev)
        *(.debug_aranges)
        *(.debug_ranges)
        *(.debug_str)
        *(.debug_line)
        *(.debug_loc)
        *(.debug_frame)
        *(.debug_pubnames)
        *(.debug_pubtypes)
    }
}

/* ── Sanity checks ───────────────────────────────────────────────────────── *
 * The identity-mapped region boot64.s sets up covers 0x0 – 0x40000000
 * (1 GiB with 512 × 2 MiB huge pages).  The kernel image must fit inside
 * 32 MiB to leave room for the heap window at 32 MiB – 80 MiB.            */
ASSERT(__bss_end <= 0x500000,
    "ERROR: kernel image has grown into the pinned .trp_code_area address (0x500000) — move it up in kernel64.ld, update TRP_CODE_LOAD_ADDR in trploader.c, update trpc's -Ttext, and recompile every existing .trp binary payload against the new address")
ASSERT(__kernel_end <= 0x2000000,
    "ERROR: kernel image exceeds 32 MiB — it would overlap the heap window")
ASSERT(__kernel_end > 0x200000,
    "ERROR: kernel image appears empty")
