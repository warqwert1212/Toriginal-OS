#ifndef MBR_H
#define MBR_H

#include <stdint.h>

#pragma pack(push, 1)

// Individual entry structure inside the partition table array
typedef struct {
    uint8_t  boot_indicator; // 0x80 for active bootable partition
    uint8_t  start_chs[3];   // Cylinder-Head-Sector legacy mapping
    uint8_t  partition_type; // 0x83 for a native Linux/Custom filesystem
    uint8_t  end_chs[3];
    uint32_t start_lba;      // The physical sector where partition begins
    uint32_t sector_count;   // Total block length assigned
} partition_entry_t;

// Full 512-Byte Master Boot Record Layout
typedef struct {
    uint8_t            bootstrap_code[446]; // Bootstrapping space
    partition_entry_t  partitions[4];       // Table space for 4 entries
    uint16_t           boot_signature;      // Mandatory validation indicator (0xAA55)
} mbr_t;

#pragma pack(pop)

#endif // MBR_H
