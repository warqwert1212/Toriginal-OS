#ifndef _ATA_H
#define _ATA_H

#include <stdint.h>
#include "trpfs.h"

int ata_detect(void);
int ata_is_present(void);
int ata_detect_all(void);
int ata_drive_present(int drive_index);

trpfs_blkdev_t *ata_init_blkdev(uint64_t total_bytes);
trpfs_blkdev_t *ata_init_blkdev_drive(int drive_index, uint64_t total_bytes);

#endif

