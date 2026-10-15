/* partitioning.c - real MBR partition table read/write, and a
 * partition-aware blkdev wrapper, for the installer's partitioning
 * step.
 *
 * WHY THIS FILE EXISTS: mbr.h already defines mbr_t/partition_entry_t
 * matching the real on-disk MBR layout (446-byte bootstrap code, 4
 * partition entries, 0x55AA signature at bytes 510-511 - the SAME
 * layout TRBL's own stage1.s writes and reads, confirmed consistent
 * during this engagement's earlier TRBL review) - but those types were
 * NEVER ACTUALLY USED anywhere in the kernel (confirmed by grep: zero
 * real references outside the header itself). And ata_blk_read()/
 * ata_blk_write() (kernel/ata.c) hardcode a single fixed offset
 * (ATA_BOOT_RESERVED_SECTORS) as the start of "the" TRPFS volume -
 * there was no real partition CONCEPT anywhere, just one implicit
 * region after a reserved area. This file is the real thing: it
 * reads/writes an actual MBR, and provides a partition-aware blkdev
 * whose offset comes from a chosen partition table entry, not a
 * single hardcoded constant.
 *
 * TESTING LIMITATION, stated plainly: this file's byte-level logic
 * (MBR field packing, CHS placeholder values, signature placement)
 * was verified via a hosted-C mirror test (see this engagement's
 * actual test run), the same way the TSS/GDT descriptor math was
 * verified earlier - but the REAL ata_raw_read_sector()/
 * ata_raw_write_sector() calls this file makes have NOT been
 * exercised against real disk I/O, because this sandbox has no QEMU/
 * real disk to boot against. The partition math is verified; the
 * actual disk read/write behavior is not. */
#include "partitioning.h"
#include "mbr.h"
#include "ata.h"
#include "string.h"
#include "serial.h"
#include "heap.h"

#define ATA_SECTOR_SIZE 512

/* CHS (Cylinder-Head-Sector) fields in a partition_entry_t are legacy
 * BIOS-era addressing that modern code (including TRBL's own stage1/
 * stage2 - confirmed during this engagement's TRBL review, which
 * found stage2 uses LBA addressing, not CHS) ignores in favor of the
 * LBA fields. A fixed, standard "don't care, but valid-looking"
 * placeholder (0xFE, 0xFF, 0xFF - the conventional "CHS overflow,
 * use LBA instead" marker many real partitioning tools write) is used
 * here rather than computing real CHS values neither this kernel nor
 * TRBL ever reads. */
static void set_chs_placeholder(uint8_t chs[3]) {
    chs[0] = 0xFE; chs[1] = 0xFF; chs[2] = 0xFF;
}

int partitioning_read_mbr(mbr_t *out) {
    uint8_t sector[ATA_SECTOR_SIZE];
    _Static_assert(sizeof(mbr_t) == ATA_SECTOR_SIZE,
        "mbr_t must be exactly one 512-byte sector - mbr.h's #pragma pack(1) "
        "guarantees no padding, so this only fails if a field was added/removed "
        "without updating this assumption");

    if (ata_raw_read_sector(0, sector) != 0) {
        serial_puts("[PART] Failed to read MBR sector (LBA 0).\n");
        return -1;
    }
    memcpy(out, sector, sizeof(mbr_t));
    return 0;
}

int partitioning_write_mbr(const mbr_t *mbr) {
    uint8_t sector[ATA_SECTOR_SIZE];
    memcpy(sector, mbr, sizeof(mbr_t));
    if (ata_raw_write_sector(0, sector) != 0) {
        serial_puts("[PART] Failed to write MBR sector (LBA 0).\n");
        return -1;
    }
    return 0;
}

int partitioning_is_valid(const mbr_t *mbr) {
    return mbr->boot_signature == 0xAA55;
}

void partitioning_make_single(mbr_t *out, uint32_t start_lba, uint32_t sector_count) {
    memset(out, 0, sizeof(mbr_t));
    /* bootstrap_code left zeroed - this MBR is for a DATA disk layout
     * only; it is NEVER the thing a BIOS boots from directly in this
     * OS's design (TRBL's own stage1.s - written and verified during
     * this engagement's earlier TRBL review - IS the real boot sector,
     * written separately to LBA 0 by write_bootloader_to_disk() in
     * installer.c). A real stage1 install overwrites bytes 0-445 of
     * this same sector with real bootstrap code immediately after;
     * this function only owns the partition table portion. */

    out->partitions[0].boot_indicator = 0x80; /* active/bootable */
    set_chs_placeholder(out->partitions[0].start_chs);
    out->partitions[0].partition_type = 0x83; /* matches mbr.h's own
        * comment: "0x83 for a native Linux/Custom filesystem" - TRPFS
        * has no officially-assigned partition type byte of its own
        * (it's a from-scratch format, not registered anywhere), so
        * this reuses the conventional "generic Unix-like filesystem"
        * byte rather than inventing an arbitrary, meaningless-to-
        * anyone-else value. */
    set_chs_placeholder(out->partitions[0].end_chs);
    out->partitions[0].start_lba = start_lba;
    out->partitions[0].sector_count = sector_count;

    /* partitions[1..3] stay zeroed - unused, matching a real "no
     * partition here" entry (all-zero is the standard convention an
     * MBR parser checks for, not a special case this format invented). */

    out->boot_signature = 0xAA55;
}

int partitioning_make_multi(mbr_t *out, const partition_spec_t *specs, int count) {
    if (count < 1 || count > 4) {
        serial_puts("[PART] partitioning_make_multi: count must be 1-4.\n");
        return -1;
    }
    memset(out, 0, sizeof(mbr_t));
    for (int i = 0; i < count; i++) {
        out->partitions[i].boot_indicator = specs[i].bootable ? 0x80 : 0x00;
        set_chs_placeholder(out->partitions[i].start_chs);
        out->partitions[i].partition_type = specs[i].partition_type;
        set_chs_placeholder(out->partitions[i].end_chs);
        out->partitions[i].start_lba = specs[i].start_lba;
        out->partitions[i].sector_count = specs[i].sector_count;
    }
    out->boot_signature = 0xAA55;
    return 0;
}

/* ── Partition-aware blkdev ───────────────────────────────────────────────
 * Same read_block/write_block function-pointer shape ata_blk_read/
 * ata_blk_write (kernel/ata.c) already use, but the base LBA offset
 * comes from a chosen partition_entry_t (via ctx) instead of the
 * single hardcoded ATA_BOOT_RESERVED_SECTORS constant - this is what
 * actually lets more than one TRPFS volume exist on the same disk at
 * once (this port's "run multiple different systems" / multi-boot
 * goal, as originally described), each in its own partition. */

typedef struct {
    uint32_t partition_start_lba;
} part_blkdev_ctx_t;

/* Must match TRPFS's own SECTORS_PER_BLOCK - mirrored here rather
 * than included from ata.c (which doesn't expose it via a header)
 * since this is the same fixed relationship ata_blk_read/write
 * already rely on: TRPFS_BLOCK_SIZE / ATA_SECTOR_SIZE. */
#define PART_SECTORS_PER_BLOCK (TRPFS_BLOCK_SIZE / ATA_SECTOR_SIZE)

static int part_blk_read(trpfs_blkdev_t *dev, uint64_t lba, void *buf) {
    part_blkdev_ctx_t *ctx = (part_blkdev_ctx_t *)dev->ctx;
    uint32_t base = ctx->partition_start_lba + (uint32_t)(lba * PART_SECTORS_PER_BLOCK);
    uint8_t *out = (uint8_t *)buf;
    for (uint32_t i = 0; i < PART_SECTORS_PER_BLOCK; i++) {
        if (ata_raw_read_sector(base + i, out + i * ATA_SECTOR_SIZE) != 0) return -1;
    }
    return 0;
}

static int part_blk_write(trpfs_blkdev_t *dev, uint64_t lba, const void *buf) {
    part_blkdev_ctx_t *ctx = (part_blkdev_ctx_t *)dev->ctx;
    uint32_t base = ctx->partition_start_lba + (uint32_t)(lba * PART_SECTORS_PER_BLOCK);
    const uint8_t *in = (const uint8_t *)buf;
    for (uint32_t i = 0; i < PART_SECTORS_PER_BLOCK; i++) {
        if (ata_raw_write_sector(base + i, in + i * ATA_SECTOR_SIZE) != 0) return -1;
    }
    return 0;
}

static trpfs_blkdev_t g_part_devs[4];
static part_blkdev_ctx_t g_part_ctxs[4];

trpfs_blkdev_t *partitioning_open_blkdev(const mbr_t *mbr, int partition_index) {
    if (partition_index < 0 || partition_index >= 4) return 0;
    const partition_entry_t *p = &mbr->partitions[partition_index];
    if (p->sector_count == 0) {
        serial_puts("[PART] Requested partition index is empty (sector_count=0).\n");
        return 0;
    }

    g_part_ctxs[partition_index].partition_start_lba = p->start_lba;
    g_part_devs[partition_index].read_block = part_blk_read;
    g_part_devs[partition_index].write_block = part_blk_write;
    g_part_devs[partition_index].total_blocks = p->sector_count / PART_SECTORS_PER_BLOCK;
    g_part_devs[partition_index].ctx = &g_part_ctxs[partition_index];

    return &g_part_devs[partition_index];
}
