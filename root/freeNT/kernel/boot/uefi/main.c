#include "efi.h"
#include "trbl_settings.h"

/* =============================================================================
 * MAIN.C — TRBLU, the UEFI half of TRBL (Toriginal OS Runtime Boot Loader)
 *
 * Mirrors what kernel/boot/loader/stage1.s + stage2.s + stage2_pm.s do
 * for legacy BIOS, using UEFI's own services instead of INT13h/INT10h:
 *   1. print progress (console messages - same "tell you exactly what
 *      happened" philosophy as the BIOS path's msg_* strings)
 *   2. find the raw disk Block I/O protocol for the device we booted
 *      from (NOT the FAT ESP partition - TRBLU only uses the ESP to
 *      hold itself; everything else reads the same raw LBAs the BIOS
 *      path uses, so both boot paths share one on-disk layout)
 *   3. read the TRBL settings sector (LBA 2000) - see trbl_settings.h
 *   4. set the GOP mode matching the saved width/height
 *   5. read the kernel.elf spare copy (LBA 65-1088) and load its
 *      PT_LOAD segments to their p_vaddr addresses, matching exactly
 *      what stage2_pm.s's elf_load_segments does for BIOS
 *   6. locate the _start64_uefi symbol in the loaded kernel's own
 *      symbol table (boot64.s) - NOT the ELF entry point, which stays
 *      pinned to the 32-bit BIOS entry _start for multiboot2/GRUB
 *      compatibility
 *   7. synthesize the same multiboot2-format info block
 *      build_multiboot2_info builds for BIOS (framebuffer tag + end
 *      tag - see stage2_pm.s's comment for the exact layout), so
 *      kernel_main() needs zero changes to accept either boot path
 *   8. GetMemoryMap + ExitBootServices, then jump directly into
 *      _start64_uefi - already 64-bit long mode, no protected-mode
 *      detour needed (see boot64.s's _start64_uefi comment)
 *
 * Everything from here down is TRBLU's own code end to end - no
 * gnu-efi, no vendored bootloader. Built as a freestanding PE32+ via
 * build_uefi.sh (ld --oformat pe-x86-64 direct from ELF object files,
 * no objcopy step - GNU ld can emit PE directly from freestanding
 * ms_abi C, which is the whole reason efi.h marks every UEFI call
 * with EFIAPI/ms_abi rather than assuming System V).
 * ========================================================================= */

#define KERNEL_LBA_START   65      /* matches installer.c's write_bootloader_to_disk() */
/* FIX: was 1024 (512 KiB) - too small once the kernel started
 * embedding its own boot assets; must match stage2.s's
 * KERNEL_MAX_SECTORS and installer.c's KERNEL_MAX_SECTORS_FOR_BOOTLOADER
 * exactly (see ata.h's ATA_BOOT_RESERVED_SECTORS comment). */
#define KERNEL_MAX_SECTORS 65536   /* 32 MiB budget, same as the BIOS path */
/* FIX: was 0x300000. That comment's own stated safe window
 * (0x200000-0x500000) stopped being true the moment the kernel
 * started embedding its boot assets - kernel64.ld now allows the
 * kernel to load anywhere in 0x200000-0x2000000 (32 MiB), which
 * swallows 0x300000 whole. Since load_kernel_and_find_uefi_entry()
 * below copies each PT_LOAD segment from this scratch buffer to its
 * real final p_vaddr, having the scratch buffer *inside* the
 * destination range means an earlier segment's write (including its
 * own zero-filled .bss tail) can clobber a later segment's still-
 * unread source bytes sitting in the same scratch memory - silent
 * kernel corruption, not a crash you'd immediately trace back here.
 * Moved well clear of the kernel's entire load range instead -
 * matches stage2.s's high staging address for the same reason. */
#define KERNEL_LOAD_SCRATCH 0x4000000ULL /* where TRBLU stages the raw ELF file
                                          * before parsing it - clear of both
                                          * the kernel's full 0x200000-0x2000000
                                          * load window (see kernel64.ld) and
                                          * the MB2 info scratch buffer below */
#define MB2_INFO_ADDR 0x30000ULL

/* Elf64 offsets - kept in sync with stage2_pm.s's identical constants
 * (see that file's comment) since both loaders parse the same kernel
 * image the same way. */
#define EHDR_E_ENTRY     24
#define EHDR_E_SHOFF     40
#define EHDR_E_PHOFF     32
#define EHDR_E_PHENTSIZE 54
#define EHDR_E_PHNUM     56
#define EHDR_E_SHENTSIZE 58
#define EHDR_E_SHNUM     60
#define EHDR_E_SHSTRNDX  62

#define PHDR_P_TYPE   0
#define PHDR_P_OFFSET 8
#define PHDR_P_VADDR  16
#define PHDR_P_FILESZ 32
#define PHDR_P_MEMSZ  40
#define PT_LOAD 1

/* Elf64_Shdr / Elf64_Sym offsets - only needed here (not in the BIOS
 * path, which never has to look up a symbol by name - it always jumps
 * to the fixed multiboot2 entry _start). TRBLU needs this because
 * _start64_uefi's address depends on however boot64.s happens to lay
 * things out at link time, and hardcoding that address here would
 * silently drift out of sync the moment boot64.s changes - exactly
 * the class of bug stage2_offsets.h's removal (see trbl_settings.h)
 * was fixing elsewhere. */
#define SHDR_SH_TYPE   4
#define SHDR_SH_OFFSET 24
#define SHDR_SH_SIZE   32
#define SHDR_SH_LINK   40
#define SHDR_SH_ENTSIZE 56
#define SHT_SYMTAB 2

#define SYM_ST_NAME  0
#define SYM_ST_VALUE 8
#define SYM_SIZE     24

static EFI_SYSTEM_TABLE *gST;

/* ── Console output ────────────────────────────────────────────────────── */
static void efi_print(const char *s) {
    CHAR16 buf[128];
    UINTN i = 0;
    while (s[i] && i < 126) {
        buf[i] = (CHAR16)(unsigned char)s[i];
        if (buf[i] == '\n') {
            /* UEFI's console wants CRLF, not bare LF */
            buf[i] = L'\r';
            i++;
            if (i >= 126) break;
            buf[i] = L'\n';
        }
        i++;
    }
    buf[i] = 0;
    gST->ConOut->OutputString(gST->ConOut, buf);
}

/* Minimal decimal printer - avoids needing a freestanding *printf. */
static void efi_print_uint(uint64_t v) {
    char digits[24];
    int n = 0;
    if (v == 0) { efi_print("0"); return; }
    while (v > 0 && n < 23) { digits[n++] = (char)('0' + (v % 10)); v /= 10; }
    char out[25];
    for (int i = 0; i < n; i++) out[i] = digits[n - 1 - i];
    out[n] = 0;
    efi_print(out);
}

static void efi_fatal(const char *msg) {
    efi_print("TRBLU: FATAL - ");
    efi_print(msg);
    efi_print("\n");
    for (;;) { __asm__ volatile("hlt"); }
}

/* ── Locate the raw (non-partition) Block I/O device we booted from ─────── */
static EFI_BLOCK_IO_PROTOCOL *find_boot_disk(EFI_HANDLE ImageHandle) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = 0;
    EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_STATUS st = gST->BootServices->HandleProtocol(ImageHandle, &li_guid, (void **)&loaded_image);
    if (EFI_ERROR(st) || !loaded_image) {
        efi_fatal("could not get LoadedImageProtocol");
    }

    /* TRBLU's own device handle (the ESP it booted from) almost always
     * exposes Block I/O too, but as a logical partition - we need the
     * underlying raw disk instead, since that's where TRBL's settings
     * sector / kernel spare copy / everything else actually live (see
     * this file's top comment). Ask for Block I/O on our own handle
     * first purely to confirm the protocol exists on this platform,
     * then defer to the caller's own device search. */
    (void)loaded_image;

    EFI_GUID bio_guid = EFI_BLOCK_IO_PROTOCOL_GUID;
    EFI_BLOCK_IO_PROTOCOL *bio = 0;
    st = gST->BootServices->HandleProtocol(loaded_image->DeviceHandle, &bio_guid, (void **)&bio);
    if (EFI_ERROR(st) || !bio) {
        efi_fatal("could not get BlockIoProtocol on boot device handle");
    }

    /* If our own handle's Block I/O is the raw disk already (some
     * firmware exposes the whole disk as the boot handle directly),
     * use it as-is. If it's a partition (the common case - booting
     * off a FAT ESP), TRBLU still needs the parent disk. Full
     * partition-table walking to find the parent handle is real
     * follow-up work; for now TRBLU requires the whole-disk handle to
     * be reachable directly (true for a disk partitioned as: ESP +
     * TRBL's own raw reserved region, with no such region hidden
     * behind a GPT entry) and reports plainly if it isn't rather than
     * silently reading the wrong device. */
    if (bio->Media->LogicalPartition) {
        efi_fatal("boot device Block I/O is a partition, not the raw disk - "
                   "TRBLU needs direct raw-disk access to LBA 65+/2000 "
                   "(see trbl_settings.h); parent-disk lookup is not yet "
                   "implemented");
    }

    return bio;
}

/* ── Read `count` 512-byte sectors starting at `lba` into `dest` ────────── */
static void read_sectors(EFI_BLOCK_IO_PROTOCOL *bio, uint64_t lba, uint64_t count, void *dest) {
    uint32_t block_size = bio->Media->BlockSize;
    if (block_size == 0) block_size = 512;

    /* TRBL's on-disk layout (trbl_settings.h) is defined in 512-byte
     * sectors throughout - if the underlying media reports a
     * different native block size (4Kn drives are common on real
     * hardware), translate here so every other constant in this
     * codebase (TRBL_SETTINGS_LBA, KERNEL_LBA_START, etc.) keeps
     * meaning "512-byte sector N" regardless of the physical device. */
    uint64_t byte_offset = lba * 512ULL;
    uint64_t byte_count  = count * 512ULL;
    uint64_t dev_lba      = byte_offset / block_size;
    uint64_t dev_lba_end  = (byte_offset + byte_count + block_size - 1) / block_size;
    uint64_t dev_count    = dev_lba_end - dev_lba;

    if (block_size == 512) {
        EFI_STATUS st = bio->ReadBlocks(bio, bio->Media->MediaId, lba, byte_count, dest);
        if (EFI_ERROR(st)) efi_fatal("ReadBlocks failed (512-byte media)");
        return;
    }

    /* Non-512 media: read whole native blocks into a scratch buffer
     * then copy out just the requested byte range. */
    uint64_t scratch_bytes = dev_count * block_size;
    uint64_t scratch_addr = 0x2F0000ULL; /* clear of every other fixed
                                           * scratch region in this file */
    EFI_STATUS st = bio->ReadBlocks(bio, bio->Media->MediaId, dev_lba, scratch_bytes, (void *)scratch_addr);
    if (EFI_ERROR(st)) efi_fatal("ReadBlocks failed (non-512-byte media)");

    uint64_t skip = byte_offset - dev_lba * block_size;
    uint8_t *src = (uint8_t *)scratch_addr + skip;
    uint8_t *d = (uint8_t *)dest;
    for (uint64_t i = 0; i < byte_count; i++) d[i] = src[i];
}

/* ── Set the GOP mode closest to (want_w, want_h) ────────────────────────
 * "Closest" because unlike VBE, GOP does not let you request an
 * arbitrary custom mode - only enumerate and pick from whatever modes
 * the platform actually offers. Falls back to whatever mode is
 * already active if nothing matches, rather than failing boot over a
 * cosmetic mismatch - same "degrade, don't halt" policy stage2.s's
 * VBE step follows. */
static void set_gop_mode(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, uint32_t want_w, uint32_t want_h,
                          uint64_t *out_fb_addr, uint32_t *out_pitch_px,
                          uint32_t *out_w, uint32_t *out_h, uint8_t *out_bpp) {
    uint32_t best_mode = gop->Mode->Mode;
    int found_exact = 0;

    for (uint32_t m = 0; m < gop->Mode->MaxMode && !found_exact; m++) {
        UINTN info_size;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;
        if (EFI_ERROR(gop->QueryMode(gop, m, &info_size, &info)) || !info) continue;
        if (info->HorizontalResolution == want_w && info->VerticalResolution == want_h &&
            (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ||
             info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)) {
            best_mode = m;
            found_exact = 1;
        }
    }

    if (found_exact) {
        if (EFI_ERROR(gop->SetMode(gop, best_mode))) {
            efi_print("TRBLU: SetMode failed, continuing with current mode\n");
        }
    } else {
        efi_print("TRBLU: no exact GOP mode for requested resolution, using current\n");
    }

    *out_fb_addr  = gop->Mode->FrameBufferBase;
    *out_pitch_px = gop->Mode->Info->PixelsPerScanLine;
    *out_w        = gop->Mode->Info->HorizontalResolution;
    *out_h        = gop->Mode->Info->VerticalResolution;
    *out_bpp      = 32; /* both RGB/BGR 8-bit-per-channel GOP formats are 32bpp */
}

/* ── Build the same multiboot2-format info block stage2_pm.s's
 * build_multiboot2_info produces, so kernel_main() needs no UEFI-
 * specific parsing path at all - see this file's top comment. ─────────── */
static void build_multiboot2_info(uint64_t fb_addr, uint32_t pitch_px, uint32_t w, uint32_t h, uint8_t bpp) {
    uint8_t *p = (uint8_t *)MB2_INFO_ADDR;
    uint8_t *start = p;
    p += 8; /* total_size + reserved, filled in last */

    /* framebuffer tag: type=8 size=32 */
    *(uint32_t *)(p + 0)  = 8;
    *(uint32_t *)(p + 4)  = 32;
    *(uint64_t *)(p + 8)  = fb_addr;
    *(uint32_t *)(p + 16) = pitch_px * (bpp / 8);   /* pitch in BYTES, matching
                                                       * mb2_tag_fb_t's field -
                                                       * GOP's PixelsPerScanLine
                                                       * is in PIXELS, not bytes */
    *(uint32_t *)(p + 20) = w;
    *(uint32_t *)(p + 24) = h;
    *(p + 28) = bpp;
    *(p + 29) = 1; /* fb_type = 1, direct RGB */
    *(p + 30) = 0;
    *(p + 31) = 0;
    p += 32;

    /* end tag: type=0 size=8 */
    *(uint32_t *)(p + 0) = 0;
    *(uint32_t *)(p + 4) = 8;
    p += 8;

    *(uint32_t *)(start + 0) = (uint32_t)(p - start);
    *(uint32_t *)(start + 4) = 0;
}

/* ── Load kernel.elf's PT_LOAD segments and locate _start64_uefi ────────── */
static uint64_t load_kernel_and_find_uefi_entry(EFI_BLOCK_IO_PROTOCOL *bio) {
    uint8_t *raw = (uint8_t *)KERNEL_LOAD_SCRATCH;
    read_sectors(bio, KERNEL_LBA_START, KERNEL_MAX_SECTORS, raw);

    if (raw[0] != 0x7F || raw[1] != 'E' || raw[2] != 'L' || raw[3] != 'F') {
        efi_fatal("kernel spare copy at LBA 65 is not a valid ELF image");
    }

    uint16_t phentsize = *(uint16_t *)(raw + EHDR_E_PHENTSIZE);
    uint16_t phnum      = *(uint16_t *)(raw + EHDR_E_PHNUM);
    uint64_t phoff       = *(uint64_t *)(raw + EHDR_E_PHOFF);

    for (uint16_t i = 0; i < phnum; i++) {
        uint8_t *ph = raw + phoff + (uint64_t)i * phentsize;
        uint32_t p_type = *(uint32_t *)(ph + PHDR_P_TYPE);
        if (p_type != PT_LOAD) continue;

        uint64_t p_offset = *(uint64_t *)(ph + PHDR_P_OFFSET);
        uint64_t p_vaddr  = *(uint64_t *)(ph + PHDR_P_VADDR);
        uint64_t p_filesz = *(uint64_t *)(ph + PHDR_P_FILESZ);
        uint64_t p_memsz  = *(uint64_t *)(ph + PHDR_P_MEMSZ);

        uint8_t *dst = (uint8_t *)p_vaddr;
        uint8_t *src = raw + p_offset;
        for (uint64_t b = 0; b < p_filesz; b++) dst[b] = src[b];
        for (uint64_t b = p_filesz; b < p_memsz; b++) dst[b] = 0;
    }

    /* Walk section headers looking for SHT_SYMTAB, then its symbols
     * for "_start64_uefi" by name via the linked string table
     * section. Hand-rolled strcmp since this file has no libc. */
    uint16_t shentsize = *(uint16_t *)(raw + EHDR_E_SHENTSIZE);
    uint16_t shnum      = *(uint16_t *)(raw + EHDR_E_SHNUM);
    uint64_t shoff       = *(uint64_t *)(raw + EHDR_E_SHOFF);

    for (uint16_t i = 0; i < shnum; i++) {
        uint8_t *sh = raw + shoff + (uint64_t)i * shentsize;
        if (*(uint32_t *)(sh + SHDR_SH_TYPE) != SHT_SYMTAB) continue;

        uint64_t symtab_off = *(uint64_t *)(sh + SHDR_SH_OFFSET);
        uint64_t symtab_size = *(uint64_t *)(sh + SHDR_SH_SIZE);
        uint32_t strtab_idx = *(uint32_t *)(sh + SHDR_SH_LINK);
        uint8_t *strsh = raw + shoff + (uint64_t)strtab_idx * shentsize;
        uint64_t strtab_off = *(uint64_t *)(strsh + SHDR_SH_OFFSET);
        const char *strtab = (const char *)(raw + strtab_off);

        uint64_t nsyms = symtab_size / SYM_SIZE;
        for (uint64_t s = 0; s < nsyms; s++) {
            uint8_t *sym = raw + symtab_off + s * SYM_SIZE;
            uint32_t name_off = *(uint32_t *)(sym + SYM_ST_NAME);
            const char *name = strtab + name_off;
            const char *want = "_start64_uefi";
            int match = 1;
            for (int k = 0; ; k++) {
                if (name[k] != want[k]) { match = 0; break; }
                if (want[k] == '\0') break;
            }
            if (match) {
                return *(uint64_t *)(sym + SYM_ST_VALUE);
            }
        }
    }

    efi_fatal("could not find _start64_uefi symbol in kernel.elf - "
              "was it built without symbols, or does boot64.s no longer "
              "define that entry point?");
    return 0; /* unreachable */
}

/* ── Entry point ──────────────────────────────────────────────────────────
 * MS x64 ABI per the UEFI spec: (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE*). */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    gST = SystemTable;

    efi_print("TRBLU: Toriginal OS Runtime Boot Loader (UEFI)\n");
    efi_print("TRBLU: locating boot disk...\n");

    EFI_BLOCK_IO_PROTOCOL *bio = find_boot_disk(ImageHandle);

    efi_print("TRBLU: reading TRBL settings sector...\n");
    trbl_settings_t settings;
    read_sectors(bio, TRBL_SETTINGS_LBA, 1, &settings);
    uint16_t want_w = 1024, want_h = 768;
    if (settings.magic == TRBL_SETTINGS_MAGIC &&
        settings.version == TRBL_SETTINGS_VERSION &&
        settings.struct_size == sizeof(trbl_settings_t)) {
        /* Same checksum rule as load_trbl_settings in stage2.s and
         * trbl_settings_valid() in trbl_settings.c - sum the first
         * 508 bytes as little-endian u32 words, compare to the
         * trailing checksum field. Kept inline here rather than
         * shared with trbl_settings.c because this file can't link
         * against kernel-side code (different toolchain target -
         * this compiles to a PE, the kernel to an ELF) - the
         * duplication is the same tradeoff stage2.s already accepts
         * for the same reason. */
        uint32_t sum = 0;
        const uint8_t *b = (const uint8_t *)&settings;
        for (int i = 0; i < 508; i += 4) {
            uint32_t word;
            word = (uint32_t)b[i] | ((uint32_t)b[i+1] << 8) |
                   ((uint32_t)b[i+2] << 16) | ((uint32_t)b[i+3] << 24);
            sum += word;
        }
        if (sum == settings.checksum) {
            want_w = settings.width;
            want_h = settings.height;
            efi_print("TRBLU: settings sector valid\n");
        } else {
            efi_print("TRBLU: settings sector checksum mismatch, using defaults\n");
        }
    } else {
        efi_print("TRBLU: no valid settings sector, using defaults\n");
    }

    efi_print("TRBLU: locating GOP...\n");
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    if (EFI_ERROR(gST->BootServices->LocateProtocol(&gop_guid, 0, (void **)&gop)) || !gop) {
        efi_fatal("no Graphics Output Protocol available on this platform");
    }

    uint64_t fb_addr; uint32_t pitch_px, fb_w, fb_h; uint8_t fb_bpp;
    set_gop_mode(gop, want_w, want_h, &fb_addr, &pitch_px, &fb_w, &fb_h, &fb_bpp);
    efi_print("TRBLU: framebuffer at 0x");
    efi_print_uint(fb_addr);
    efi_print(", ");
    efi_print_uint(fb_w);
    efi_print("x");
    efi_print_uint(fb_h);
    efi_print("\n");

    build_multiboot2_info(fb_addr, pitch_px, fb_w, fb_h, fb_bpp);

    efi_print("TRBLU: loading kernel...\n");
    uint64_t entry = load_kernel_and_find_uefi_entry(bio);

    efi_print("TRBLU: exiting boot services, jumping to kernel...\n");

    /* GetMemoryMap/ExitBootServices dance: the map key from
     * GetMemoryMap is only valid until the next allocation, so this
     * has to be the very last thing before ExitBootServices - no
     * more AllocatePages, no more protocol calls that might allocate
     * internally, after this point. */
    UINTN map_size = 0, map_key = 0, desc_size = 0;
    uint32_t desc_ver = 0;
    EFI_MEMORY_DESCRIPTOR *map_buf = (EFI_MEMORY_DESCRIPTOR *)0x2E0000ULL;
    map_size = 0x10000; /* generous fixed scratch, re-queried below */
    EFI_STATUS st = gST->BootServices->GetMemoryMap(&map_size, map_buf, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(st)) efi_fatal("GetMemoryMap failed");

    st = gST->BootServices->ExitBootServices(ImageHandle, map_key);
    if (EFI_ERROR(st)) {
        /* Map key went stale (something allocated between the two
         * calls) - re-fetch once and retry, per the UEFI spec's
         * documented pattern for this exact race. */
        map_size = 0x10000;
        st = gST->BootServices->GetMemoryMap(&map_size, map_buf, &map_key, &desc_size, &desc_ver);
        if (EFI_ERROR(st)) efi_fatal("GetMemoryMap retry failed");
        st = gST->BootServices->ExitBootServices(ImageHandle, map_key);
        if (EFI_ERROR(st)) efi_fatal("ExitBootServices failed after retry");
    }

    /* Past this point: no more boot services, no more console output -
     * firmware may have torn down ConOut already. Jump straight into
     * the kernel's UEFI entry point with the multiboot2-format info
     * pointer/magic in the registers boot64.s's _start64_uefi expects
     * (RDI = info pointer, RSI = magic - see that routine's comment).
     *
     * Explicit sysv_abi here matters: this whole file compiles under
     * MS x64 ABI (EFIAPI/ms_abi, required for every UEFI call), so a
     * bare function-pointer call would default to MS ABI too - first
     * two integer args in RCX/RDX, not RDI/RSI. _start64_uefi (and
     * every other kernel routine, including kernel_main itself) uses
     * System V throughout; without this attribute the two calling
     * conventions would silently disagree about which registers hold
     * which argument and the kernel would read garbage. */
    typedef void (*kernel_entry_t)(uint64_t info, uint64_t magic) __attribute__((sysv_abi));
    kernel_entry_t kernel_entry = (kernel_entry_t)entry;
    kernel_entry(MB2_INFO_ADDR, 0x36D76289ULL);

    /* unreachable */
    for (;;) { __asm__ volatile("hlt"); }
    return EFI_SUCCESS;
}
