#ifndef _ATA_H
#define _ATA_H

#include <stdint.h>
#include "trpfs.h"

/* Probe for a primary-master ATA disk. Returns 1 if present, 0 if not. */
int ata_detect(void);
int ata_is_present(void);

/* Build a trpfs_blkdev_t backed by the real ATA disk, exposing the first
 * total_bytes of it. Returns NULL if no disk is present (caller should
 * fall back to the RAM disk in that case). */
trpfs_blkdev_t *ata_init_blkdev(uint64_t total_bytes);

#endif
