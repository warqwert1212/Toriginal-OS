#ifndef _TRPFS_H
#define _TRPFS_H

/* =============================================================================
 * TRPFS — Toriginal RAM/Removable Package File System
 *
 * A small, modern, 64-bit filesystem for Toriginal OS.  Design goals:
 *
 *   - 64-bit block numbers, sizes, inode numbers, timestamps everywhere
 *   - 4096-byte blocks (matches PAGE_SIZE)
 *   - Simple bitmap allocator for blocks and inodes
 *   - Direct + single-indirect block mapping per inode
 *       12 direct blocks   = 48 KiB
 *       1  indirect block  = 512 pointers * 4096 = 2 MiB
 *       -> max file size ~2 MiB + 48 KiB (double/triple indirect fields
 *          are reserved on disk for a future v2 without breaking format)
 *   - Flat directory entries (fixed-size 256-byte records, 16 per block)
 *   - Pluggable block device — works on a RAM disk today, a real AHCI/disk
 *     driver can be plugged in later without changing fs_* callers.
 *
 * This implements the fs.h API (fs_init/open/read/write/close/seek/mkdir/
 * stat) directly, replacing the always-failing stubs in runtime_stubs.c.
 * ========================================================================== */

#include "types.h"
#include "fs.h"

#define TRPFS_MAGIC          0x3153465052545254ULL /* "TRPRTFS1" little endian */
#define TRPFS_VERSION        1
#define TRPFS_BLOCK_SIZE     4096
#define TRPFS_MAX_NAME       235
#define TRPFS_ROOT_INO       1

#define TRPFS_DIRECT_BLOCKS  12
#define TRPFS_PTRS_PER_BLOCK (TRPFS_BLOCK_SIZE / 8)   /* 512 */
#define TRPFS_INODE_SIZE     256
#define TRPFS_INODES_PER_BLK (TRPFS_BLOCK_SIZE / TRPFS_INODE_SIZE) /* 16 */
#define TRPFS_DIRENT_SIZE    256
#define TRPFS_DIRENTS_PER_BLK (TRPFS_BLOCK_SIZE / TRPFS_DIRENT_SIZE) /* 16 */

#define TRPFS_MAX_OPEN_FILES 64
#define TRPFS_MAX_PATH       256

/* ── On-disk structures ──────────────────────────────────────────────────── */

/* Block 0 of every TRPFS volume */
typedef struct {
    uint64_t magic;             /* TRPFS_MAGIC                              */
    uint64_t version;           /* TRPFS_VERSION                           */
    uint64_t block_size;        /* always TRPFS_BLOCK_SIZE                 */
    uint64_t total_blocks;      /* size of the volume in blocks            */
    uint64_t free_blocks;       /* updated on alloc/free                   */
    uint64_t inode_count;       /* total inode slots                       */
    uint64_t free_inodes;       /* updated on alloc/free                   */
    uint64_t bitmap_block;      /* first block of the block-bitmap          */
    uint64_t bitmap_blocks;     /* number of blocks the bitmap occupies     */
    uint64_t inode_table_block; /* first block of the inode table           */
    uint64_t inode_table_blocks;/* number of blocks the inode table occupies*/
    uint64_t data_start_block;  /* first block usable for file data         */
    uint64_t root_ino;          /* TRPFS_ROOT_INO                            */
    uint8_t  label[32];         /* volume label, NUL-terminated             */
} trpfs_superblock_t;

/* On-disk inode — TRPFS_INODE_SIZE (256) bytes, 16 per block.
 * 16+32+96+24 = 168 bytes used; 88 bytes reserved for v2 (xattrs, etc). */
typedef struct {
    uint32_t mode;       /* FILE_TYPE_REGULAR / FILE_TYPE_DIR in low bits  */
    uint32_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint64_t direct[TRPFS_DIRECT_BLOCKS]; /* 12 * 8 = 96 bytes              */
    uint64_t indirect;          /* single indirect block (2 MiB)           */
    uint64_t double_indirect;   /* reserved for v2, must be 0 in v1        */
    uint64_t triple_indirect;   /* reserved for v2, must be 0 in v1        */
    uint8_t  reserved[TRPFS_INODE_SIZE - 168];
} __attribute__((packed)) trpfs_inode_raw_t;

/* Directory entry — TRPFS_DIRENT_SIZE (256) bytes, 16 per block */
typedef struct {
    uint64_t ino;                  /* 0 == free / unused slot             */
    uint8_t  type;                 /* FILE_TYPE_REGULAR / FILE_TYPE_DIR    */
    uint8_t  name_len;
    char     name[TRPFS_MAX_NAME]; /* not necessarily NUL-terminated       */
} __attribute__((packed)) trpfs_dirent_t;

/* ── Block device abstraction ────────────────────────────────────────────── */

typedef struct trpfs_blkdev trpfs_blkdev_t;

struct trpfs_blkdev {
    int (*read_block)(trpfs_blkdev_t *dev, uint64_t lba, void *buf);
    int (*write_block)(trpfs_blkdev_t *dev, uint64_t lba, const void *buf);
    uint64_t total_blocks;  /* capacity in TRPFS_BLOCK_SIZE units          */
    void *ctx;
};

/* RAM-disk backend — useful until a real AHCI/NVMe driver is wired in.
 * Allocates size_bytes via kmalloc and serves read/write_block from it. */
int trpfs_ramdisk_init(trpfs_blkdev_t *dev, uint64_t size_bytes);

/* ── Volume management ───────────────────────────────────────────────────── */

/* Lay down a fresh TRPFS volume on dev (destroys any existing contents). */
int trpfs_format(trpfs_blkdev_t *dev, const char *label);

/* Mount dev as the single active TRPFS volume used by the fs_* API below. */
int trpfs_mount(trpfs_blkdev_t *dev);

/* Flush all cached metadata back to the block device. */
int trpfs_sync(void);

/* Returns 1 if a volume is currently mounted. */
int trpfs_is_mounted(void);

/* ── fs.h API implementation ─────────────────────────────────────────────── */

void    fs_init(void);
fd_t    fs_open(const char *path, int flags, int mode);
ssize_t fs_read(fd_t fd, void *buf, size_t count);
ssize_t fs_write(fd_t fd, const void *buf, size_t count);
int     fs_close(fd_t fd);
int     fs_seek(fd_t fd, int64_t offset, int whence);
int     fs_mkdir(const char *path, int mode);
int     fs_stat(const char *path, inode_t *stat);

#endif /* _TRPFS_H */