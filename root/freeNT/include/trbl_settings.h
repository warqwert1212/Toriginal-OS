#ifndef _TRBL_SETTINGS_H
#define _TRBL_SETTINGS_H

#include <stdint.h>

/* =============================================================================
 * TRBL_SETTINGS.H — Toriginal OS Runtime Boot Loader, dynamic settings
 *
 * Replaces the old install-time byte-patch (installer.c used to reach
 * into the assembled stage2.bin and overwrite saved_width/saved_height
 * at a build-generated offset - see stage2_offsets.h, now removed).
 * That meant the ONLY way to change boot-time resolution was to
 * re-run the installer and rewrite all 64 sectors of stage2. There was
 * no boot-time network config at all - nowhere for it to live.
 *
 * This header defines one flat, versioned, checksummed 512-byte sector
 * that TRBL's stage2 reads at every boot (kernel/boot/loader/stage2.s)
 * and that the running kernel can rewrite at any time via
 * trbl_settings.c - a real read/write settings store on disk, not a
 * one-shot install-time patch. stage2.s is now install-independent:
 * every install writes byte-identical stage1/stage2 images, and only
 * this sector (plus its backup copy) ever changes.
 *
 * Disk layout (see ata.h's ATA_BOOT_RESERVED_SECTORS = 70048, and
 * installer.c's write_bootloader_to_disk() for the authoritative
 * picture):
 *   LBA 0            stage1 (512-byte MBR)
 *   LBA 1-64         stage2 (64 sectors, 32 KiB budget)
 *   LBA 65-65600     spare kernel.elf copy (65536 sectors, 32 MiB
 *                    budget - FIX: was 1024 sectors/512 KiB, too
 *                    small once the kernel started embedding its own
 *                    boot assets; see ata.h's ATA_BOOT_RESERVED_SECTORS
 *                    comment. Matches kernel64.ld's own 32 MiB
 *                    __kernel_end ASSERT ceiling, so this never needs
 *                    revisiting unless that ceiling does too)
 *   LBA 65601-69999  unused, reserved for future bootloader growth
 *   LBA 70000        TRBL_SETTINGS_LBA - primary settings sector
 *   LBA 70001        TRBL_SETTINGS_BACKUP_LBA - mirror, written second,
 *                    read as fallback if the primary fails its checksum
 *                    (protects against a torn/interrupted write - e.g.
 *                    power loss mid-write - corrupting the only copy)
 *   LBA 70048+       TRPFS filesystem starts here (unchanged)
 * ========================================================================= */

#define TRBL_SETTINGS_LBA        70000
#define TRBL_SETTINGS_BACKUP_LBA 70001

#define TRBL_SETTINGS_MAGIC   0x4C425254u   /* "TRBL" little-endian */
#define TRBL_SETTINGS_VERSION 1

/* Network boot-config modes for net_mode below. */
#define TRBL_NET_OFF    0   /* no network bring-up at boot */
#define TRBL_NET_DHCP   1   /* bring up the NIC and DHCP at boot */
#define TRBL_NET_STATIC 2   /* bring up the NIC with the static fields below */

/* Boot flag bits for boot_flags below. */
#define TRBL_FLAG_VERBOSE    (1u << 0)  /* print each TRBL stage's progress */
#define TRBL_FLAG_SAFE_MODE  (1u << 1)  /* skip optional driver bring-up */

/* Memory-allocation modes for mem_mode below (see the shell's `mem`
 * command and kernel/pmm.c's pmm_apply_ram_config()). A fresh install
 * seeds MAX (see installer.c's write_bootloader_to_disk()) - the OS
 * uses every byte of whatever RAM it's actually booted on, on every
 * boot, rather than freezing in whatever amount happened to be
 * present at install time. */
#define TRBL_MEM_MODE_MAX    0   /* use every byte of detected physical RAM */
#define TRBL_MEM_MODE_CUSTOM 1   /* cap usable RAM at an exact KB amount    */
#define TRBL_MEM_MODE_LEAST  2   /* bare minimum the kernel can run in      */

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         /* TRBL_SETTINGS_MAGIC - identifies a valid sector */
    uint16_t version;       /* TRBL_SETTINGS_VERSION - for future field additions */
    uint16_t struct_size;   /* sizeof(trbl_settings_t) at write time, sanity check */

    /* ── Display (read by stage2.s's vbe_set_mode, real mode, before
     * the kernel or any filesystem exists) ────────────────────────── */
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  _pad0;

    /* ── Network (staged here at boot time; read by the kernel's
     * network bring-up code post-boot - see trbl_settings.c) ──────── */
    uint8_t  net_mode;      /* TRBL_NET_OFF / TRBL_NET_DHCP / TRBL_NET_STATIC */
    uint8_t  static_ip[4];
    uint8_t  static_mask[4];
    uint8_t  static_gateway[4];
    uint8_t  static_dns[4];

    /* ── Boot behavior ───────────────────────────────────────────── */
    uint32_t boot_flags;    /* TRBL_FLAG_* bitmask */
    uint8_t  boot_drive;    /* last-known boot drive, informational */

    /* ── Memory allocation (see kernel/pmm.c and the shell's `mem`/
     * `hardware` commands). Read by pmm_apply_ram_config() once ATA
     * is up (kernel_init(), after installer_try_automount() - too
     * late for pmm_init() itself, which runs before ATA exists, so
     * the OS boots at MAX/full RAM and this narrows it down a moment
     * later if the sector asks for CUSTOM/LEAST). ────────────────── */
    uint8_t  mem_mode;      /* TRBL_MEM_MODE_* */
    uint8_t  _pad1[3];
    uint32_t ram_limit_kb;  /* only meaningful when mem_mode == TRBL_MEM_MODE_CUSTOM */

    uint8_t  _reserved[464]; /* pad out to 512 bytes total, room to grow
                               * without bumping struct_size/breaking the
                               * fixed sector size; new fields go here,
                               * shrinking _reserved, not after it */

    uint32_t checksum;      /* sum of all preceding bytes as uint32_t words,
                              * this field itself excluded - see
                              * trbl_settings_checksum() in trbl_settings.c
                              * and the equivalent loop in stage2.s */
} trbl_settings_t;
#pragma pack(pop)

/* The whole point of the fixed-512 layout: it must exactly fill one
 * disk sector, no more, no less, with no compiler-inserted padding
 * TRBL's own hand-rolled 16-bit checksum loop doesn't know about. */
_Static_assert(sizeof(trbl_settings_t) == 512,
    "trbl_settings_t must be exactly 512 bytes - adjust _reserved[]");

/* kernel/trbl_settings.c */
int trbl_settings_read(trbl_settings_t *out);
int trbl_settings_write(trbl_settings_t *s);
int trbl_settings_set_resolution(uint16_t width, uint16_t height);
int trbl_settings_set_network(uint8_t mode,
                               const uint8_t static_ip[4],
                               const uint8_t static_mask[4],
                               const uint8_t static_gateway[4],
                               const uint8_t static_dns[4]);
int trbl_settings_set_memory(uint8_t mode, uint32_t custom_kb);

#endif /* _TRBL_SETTINGS_H */
