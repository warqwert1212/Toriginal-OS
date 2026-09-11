#ifndef _ATA_H
#define _ATA_H

#include <stdint.h>
#include "trpfs.h"

/* Sectors reserved at the start of the disk for the bootloader
 * (stage1 MBR + stage2 + a spare copy of kernel.elf + the TRBL
 * settings sector/backup) - TRPFS blocks are offset past this in
 * ata_blk_read/write (see ata.c). Kept here, not just in ata.c, so
 * installer.c's raw-sector bootloader write and ata.c's TRPFS offset
 * can never drift apart.
 *
 * FIX: was 2048 (1 MiB total), sized for a kernel.elf small enough to
 * fit in the old 512 KiB spare-copy budget (see stage2.s's
 * KERNEL_MAX_SECTORS). Once the kernel started embedding its boot
 * assets directly (asset_blob.s/trbl_logo_blob.s), kernel.elf grew
 * past that ceiling entirely, and the installer correctly refused to
 * write a bootloader it couldn't fit ("kernel.elf is too large for
 * the reserved boot region"). Raised to comfortably cover the new
 * 65536-sector (32 MiB) kernel budget - see trbl_settings.h's disk
 * layout comment for the exact sector map this now reserves. */
#define ATA_BOOT_RESERVED_SECTORS 70048

int ata_detect(void);
int ata_is_present(void);
int ata_detect_all(void);
int ata_drive_present(int drive_index);

/* Raw, absolute-LBA sector I/O on drive 0 - bypasses TRPFS and its
 * ATA_BOOT_RESERVED_SECTORS offset entirely (see ata.c's
 * ata_blk_read/write, which add that offset; these two do not).
 * Used only by installer.c to write the bootloader itself (stage1,
 * stage2, and a spare kernel.elf copy) into the reserved boot region
 * at the very start of the disk - everything else on this codebase
 * goes through TRPFS and never needs raw sector access. Returns 0 on
 * success, -1 on failure (no drive 0 present, or the I/O itself
 * failed). */
int ata_raw_read_sector(uint32_t lba, void *buf);
int ata_raw_write_sector(uint32_t lba, const void *buf);

trpfs_blkdev_t *ata_init_blkdev(uint64_t total_bytes);
trpfs_blkdev_t *ata_init_blkdev_drive(int drive_index, uint64_t total_bytes);

#endif

