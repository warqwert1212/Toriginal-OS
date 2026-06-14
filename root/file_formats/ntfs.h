#ifndef _NTFS_H
#define _NTFS_H

/* =============================================================================
 * NTFS.H — basic NTFS volume detection and read-only metadata access
 *
 * Scope (v1, intentionally limited):
 *   - Detect an NTFS volume from its boot sector (OEM ID "NTFS    ")
 *   - Parse boot-sector geometry (bytes/sector, sectors/cluster, $MFT LCN,
 *     MFT record size, volume serial number)
 *   - Read a raw MFT record by index, with correct Update Sequence Array
 *     (USA) fixup application
 *   - Extract the volume label from $Volume (MFT record 3, attribute
 *     0x60 $VOLUME_NAME)
 *
 * NOT implemented (out of scope for v1): writing to NTFS, general file
 * lookup/reading, $MFT bitmap/runlist parsing for non-resident attributes,
 * journal ($LogFile) replay.  This is enough for the installer to detect an
 * existing Windows install and offer a "dual boot" layout — it does not make
 * Toriginal OS a general-purpose NTFS driver.
 * ========================================================================= */

#include "types.h"
#include "trpfs.h"

#define NTFS_ATTR_VOLUME_NAME 0x60u
#define NTFS_ATTR_END         0xFFFFFFFFu

typedef struct {
    int      detected;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint64_t total_sectors;
    uint64_t mft_lcn;
    uint64_t mft_mirror_lcn;
    uint32_t mft_record_size;
    uint64_t volume_serial;
    char     volume_label[64]; /* best-effort, ASCII-reduced from UTF-16LE */
} ntfs_volume_t;

/* Reads block 0 of dev and parses the boot sector. out->detected is set to
 * 1 only if the OEM ID field reads "NTFS    ". Returns 0 if the read
 * succeeded (regardless of whether NTFS was detected); -1 on I/O error. */
int ntfs_detect(trpfs_blkdev_t *dev, ntfs_volume_t *out);

/* Read MFT record `record` (vol->mft_record_size bytes) into buf, applying
 * USA fixups. buf must be at least vol->mft_record_size bytes (<= 4096).
 * Returns 0 on success. */
int ntfs_read_mft_record(trpfs_blkdev_t *dev, const ntfs_volume_t *vol,
                          uint64_t record, uint8_t *buf);

/* Convenience: read MFT record 3 ($Volume) and fill out->volume_label.
 * Safe to call even if it fails — volume_label is left as an empty string. */
void ntfs_read_volume_label(trpfs_blkdev_t *dev, ntfs_volume_t *vol);

#endif /* _NTFS_H */