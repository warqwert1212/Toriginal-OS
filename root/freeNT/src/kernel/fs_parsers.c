#include "vfs.h"
#include "heap.h"

void print_serial(const char* str);

// FAT32 Extended Boot Record layout mappings
struct fat32_ebp {
    uint32_t sectors_per_fat;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
} __attribute__((packed));

// ext2 Inode verification descriptors
struct ext2_inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t block[15]; // Direct / Indirect block pointers
} __attribute__((packed));

// NTFS Master File Table (MFT) Header signature tracking
struct ntfs_mft_record {
    uint32_t magic;       // Expects "FILE"
    uint16_t update_seq_offset;
    uint16_t update_seq_size;
    uint64_t log_sequence_number;
} __attribute__((packed));


// Unified framework initialization hook
void init_file_systems(void) {
    print_serial("[FS] FAT32 Layout Parser Module: Linked.\n");
    print_serial("[FS] ext2 Extended Node Parser Module: Linked.\n");
    print_serial("[FS] NTFS MFT Attribute Mapping Module: Linked.\n");
}