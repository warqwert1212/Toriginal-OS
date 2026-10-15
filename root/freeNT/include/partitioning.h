#ifndef _KERNEL_PARTITIONING_H
#define _KERNEL_PARTITIONING_H

#include "mbr.h"
#include "trpfs.h"

/* Reads the real MBR sector (LBA 0) into `out`. Returns 0 on success,
 * -1 on a real disk read failure. */
int partitioning_read_mbr(mbr_t *out);

/* Writes `mbr` to the real MBR sector (LBA 0). DESTRUCTIVE: overwrites
 * whatever bootstrap code and partition table currently exist there -
 * the installer is responsible for writing TRBL's own stage1 bytes
 * into mbr->bootstrap_code (bytes 0-445) separately, normally via
 * write_bootloader_to_disk() in installer.c, AFTER calling this with
 * a zeroed bootstrap_code (see partitioning_make_single()'s own
 * comment on why this function doesn't do that itself). */
int partitioning_write_mbr(const mbr_t *mbr);

/* True iff mbr->boot_signature is the real, required 0xAA55 value. */
int partitioning_is_valid(const mbr_t *mbr);

/* Fills `out` with a single-partition layout: one active, type-0x83
 * partition spanning [start_lba, start_lba+sector_count), bootstrap
 * code left zeroed (see partitioning_write_mbr()'s comment on why).
 * This is the "simple/guided" install path's partition layout - one
 * partition, the whole usable disk. */
void partitioning_make_single(mbr_t *out, uint32_t start_lba, uint32_t sector_count);

typedef struct {
    uint32_t start_lba;
    uint32_t sector_count;
    uint8_t  partition_type; /* 0x83 for a TRPFS volume - see
                               * partitioning_make_single()'s comment on
                               * why that specific byte value */
    int      bootable;
} partition_spec_t;

/* Fills `out` with up to 4 partitions as described by `specs` - the
 * "expert mode" path: a user-chosen multi-partition layout (e.g. for
 * running multiple TRPFS volumes, or reserving space for a future
 * dual-boot partition) instead of the single-partition default.
 * Returns 0 on success, -1 if count is outside 1-4 (an MBR has
 * exactly 4 primary partition slots - a real, fixed hardware/format
 * limit, not an arbitrary cap this function invented). */
int partitioning_make_multi(mbr_t *out, const partition_spec_t *specs, int count);

/* Returns a trpfs_blkdev_t backed by partition `partition_index`
 * (0-3) of `mbr`, or NULL if that slot is empty (sector_count == 0)
 * or the index is out of range. The returned pointer is to static
 * storage (one slot per partition index, matching mbr_t's own fixed
 * 4-partition layout) - valid for the lifetime of the OS, same
 * lifetime convention ata_init_blkdev()'s own g_ata_devs[] already
 * uses in kernel/ata.c. */
trpfs_blkdev_t *partitioning_open_blkdev(const mbr_t *mbr, int partition_index);

#endif /* _KERNEL_PARTITIONING_H */
