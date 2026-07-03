/* =============================================================================
 * TRPFS.C — Toriginal RAM/Removable Package File System (v1)
 *
 * Implements the fs.h API on top of a simple, modern, 64-bit on-disk layout.
 * See trpfs.h for the on-disk format description.
 *
 * This file replaces the always-failing fs_* stubs that used to live in
 * runtime_stubs.c (those have been removed — see runtime_stubs.c).
 * ========================================================================= */

#include "trpfs.h"
#include "string.h"
#include "memory.h"
#include "pit.h"
#include "serial.h"

/* ── Scratch buffers ───────────────────────────────────────────────────────
 * The kernel is single-threaded/cooperative, so a small set of dedicated
 * block-sized static buffers avoids per-call kmalloc() (kfree() is a no-op
 * bump allocator in memory.c — repeated 4 KiB allocs would leak forever).
 * Rule: a function may use its buffer across calls to helpers that use a
 * *different* buffer, but must finish with its own buffer before calling a
 * helper that reuses the same one. ------------------------------------- */
static uint8_t g_data_buf[TRPFS_BLOCK_SIZE];   /* file data blocks          */
static uint8_t g_dir_buf[TRPFS_BLOCK_SIZE];    /* directory entry blocks    */
static uint8_t g_ptr_buf[TRPFS_BLOCK_SIZE];    /* indirect pointer blocks   */

/* ── Mounted volume state ────────────────────────────────────────────────── */
static trpfs_blkdev_t   *g_dev    = NULL;
static trpfs_superblock_t g_sb;
static uint8_t          *g_bitmap = NULL;  /* g_sb.bitmap_blocks * BLOCK_SIZE */

typedef struct {
    int      in_use;
    uint64_t ino;
    int      flags;
    uint64_t offset;
} trpfs_ofile_t;

static trpfs_ofile_t g_files[TRPFS_MAX_OPEN_FILES];

/* ── Small helpers ───────────────────────────────────────────────────────── */

static inline uint8_t inode_type(uint32_t mode)  { return (uint8_t)((mode >> 16) & 0xFF); }
static inline uint32_t inode_perm(uint32_t mode) { return mode & 0xFFFFu; }
static inline uint32_t make_mode(uint8_t type, uint32_t perm) {
    return ((uint32_t)type << 16) | (perm & 0xFFFFu);
}

static uint64_t now_ticks(void) { return pit_get_ticks(); }

static int blk_read(uint64_t lba, void *buf) {
    if (!g_dev) return -1;
    return g_dev->read_block(g_dev, lba, buf);
}

static int blk_write(uint64_t lba, const void *buf) {
    if (!g_dev) return -1;
    return g_dev->write_block(g_dev, lba, buf);
}

/* ── Bitmap allocator ────────────────────────────────────────────────────── */

static int flush_bitmap(void) {
    for (uint64_t i = 0; i < g_sb.bitmap_blocks; i++) {
        if (blk_write(g_sb.bitmap_block + i, g_bitmap + i * TRPFS_BLOCK_SIZE) != 0)
            return -1;
    }
    return 0;
}

static int flush_superblock(void) {
    uint8_t buf[TRPFS_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, &g_sb, sizeof(g_sb));
    return blk_write(0, buf);
}

static inline int bit_test(uint64_t bit)  { return (g_bitmap[bit >> 3] >> (bit & 7)) & 1; }
static inline void bit_set(uint64_t bit)  { g_bitmap[bit >> 3] |=  (uint8_t)(1u << (bit & 7)); }
static inline void bit_clear(uint64_t bit){ g_bitmap[bit >> 3] &= (uint8_t)~(1u << (bit & 7)); }

/* Allocate one free data block. Returns block number, or 0 on failure. */
static uint64_t alloc_block(void) {
    for (uint64_t b = g_sb.data_start_block; b < g_sb.total_blocks; b++) {
        if (!bit_test(b)) {
            bit_set(b);
            g_sb.free_blocks--;
            flush_bitmap();
            flush_superblock();
            return b;
        }
    }
    return 0;
}

static void free_block(uint64_t b) {
    if (b < g_sb.data_start_block || b >= g_sb.total_blocks) return;
    if (bit_test(b)) {
        bit_clear(b);
        g_sb.free_blocks++;
        flush_bitmap();
        flush_superblock();
    }
}

/* ── Inode table I/O ─────────────────────────────────────────────────────── */

static int read_inode(uint64_t ino, trpfs_inode_raw_t *out) {
    if (ino == 0 || ino > g_sb.inode_count) return -1;
    uint64_t idx   = ino - 1;
    uint64_t blk   = g_sb.inode_table_block + (idx / TRPFS_INODES_PER_BLK);
    uint64_t off   = (idx % TRPFS_INODES_PER_BLK) * TRPFS_INODE_SIZE;

    static uint8_t buf[TRPFS_BLOCK_SIZE];
    if (blk_read(blk, buf) != 0) return -1;
    memcpy(out, buf + off, sizeof(trpfs_inode_raw_t));
    return 0;
}

static int write_inode(uint64_t ino, const trpfs_inode_raw_t *in) {
    if (ino == 0 || ino > g_sb.inode_count) return -1;
    uint64_t idx   = ino - 1;
    uint64_t blk   = g_sb.inode_table_block + (idx / TRPFS_INODES_PER_BLK);
    uint64_t off   = (idx % TRPFS_INODES_PER_BLK) * TRPFS_INODE_SIZE;

    static uint8_t buf[TRPFS_BLOCK_SIZE];
    if (blk_read(blk, buf) != 0) return -1;
    memcpy(buf + off, in, sizeof(trpfs_inode_raw_t));
    return blk_write(blk, buf);
}

/* Find a free inode slot (mode == 0). Returns inode number (>=1), or 0. */
static uint64_t alloc_inode(void) {
    static uint8_t buf[TRPFS_BLOCK_SIZE];
    for (uint64_t blk = 0; blk < g_sb.inode_table_blocks; blk++) {
        if (blk_read(g_sb.inode_table_block + blk, buf) != 0) return 0;
        for (uint64_t i = 0; i < TRPFS_INODES_PER_BLK; i++) {
            trpfs_inode_raw_t *cand = (trpfs_inode_raw_t *)(buf + i * TRPFS_INODE_SIZE);
            uint64_t ino = blk * TRPFS_INODES_PER_BLK + i + 1;
            if (ino > g_sb.inode_count) return 0;
            if (cand->mode == 0) {
                g_sb.free_inodes--;
                flush_superblock();
                return ino;
            }
        }
    }
    return 0;
}

static void free_inode_blocks(trpfs_inode_raw_t *node) {
    for (int i = 0; i < TRPFS_DIRECT_BLOCKS; i++) {
        if (node->direct[i]) { free_block(node->direct[i]); node->direct[i] = 0; }
    }
    if (node->indirect) {
        if (blk_read(node->indirect, g_ptr_buf) == 0) {
            uint64_t *ptrs = (uint64_t *)g_ptr_buf;
            for (uint64_t i = 0; i < TRPFS_PTRS_PER_BLOCK; i++) {
                if (ptrs[i]) free_block(ptrs[i]);
            }
        }
        free_block(node->indirect);
        node->indirect = 0;
    }
}

static void free_inode(uint64_t ino) {
    trpfs_inode_raw_t node;
    if (read_inode(ino, &node) != 0) return;
    free_inode_blocks(&node);
    memset(&node, 0, sizeof(node));
    write_inode(ino, &node);
    g_sb.free_inodes++;
    flush_superblock();
}

/* ── Logical block mapping ──────────────────────────────────────────────── */

/* Returns the physical block number for logical block `index` of `node`.
 * If `allocate` is non-zero, missing blocks (and the indirect block, if
 * needed) are allocated and `*dirty` is set so the caller re-writes the
 * inode.  Returns 0 if `index` is out of range or allocation failed. */
static uint64_t inode_block(trpfs_inode_raw_t *node, uint64_t index, int allocate, int *dirty) {
    if (index < TRPFS_DIRECT_BLOCKS) {
        if (node->direct[index] == 0 && allocate) {
            uint64_t b = alloc_block();
            if (!b) return 0;
            memset(g_data_buf, 0, sizeof(g_data_buf));
            blk_write(b, g_data_buf);
            node->direct[index] = b;
            *dirty = 1;
        }
        return node->direct[index];
    }

    uint64_t iidx = index - TRPFS_DIRECT_BLOCKS;
    if (iidx >= TRPFS_PTRS_PER_BLOCK) return 0; /* beyond v1 max file size */

    if (node->indirect == 0) {
        if (!allocate) return 0;
        uint64_t b = alloc_block();
        if (!b) return 0;
        memset(g_ptr_buf, 0, sizeof(g_ptr_buf));
        blk_write(b, g_ptr_buf);
        node->indirect = b;
        *dirty = 1;
    }

    if (blk_read(node->indirect, g_ptr_buf) != 0) return 0;
    uint64_t *ptrs = (uint64_t *)g_ptr_buf;

    if (ptrs[iidx] == 0) {
        if (!allocate) return 0;
        uint64_t b = alloc_block();
        if (!b) return 0;
        memset(g_data_buf, 0, sizeof(g_data_buf));
        blk_write(b, g_data_buf);
        ptrs[iidx] = b;
        blk_write(node->indirect, g_ptr_buf);
    }
    return ptrs[iidx];
}

/* ── Directory operations ────────────────────────────────────────────────── */

#define DIRENTS_IN(node) (((node)->size + TRPFS_BLOCK_SIZE - 1) / TRPFS_BLOCK_SIZE)

static int dir_lookup(uint64_t dir_ino, const char *name, uint64_t name_len,
                       uint64_t *out_ino, uint8_t *out_type) {
    trpfs_inode_raw_t node;
    if (read_inode(dir_ino, &node) != 0) return -1;
    if (inode_type(node.mode) != FILE_TYPE_DIR) return -1;

    uint64_t nblocks = DIRENTS_IN(&node);
    for (uint64_t b = 0; b < nblocks; b++) {
        int dirty = 0;
        uint64_t pb = inode_block(&node, b, 0, &dirty);
        if (!pb) continue;
        if (blk_read(pb, g_dir_buf) != 0) continue;

        trpfs_dirent_t *ents = (trpfs_dirent_t *)g_dir_buf;
        for (uint64_t i = 0; i < TRPFS_DIRENTS_PER_BLK; i++) {
            if (ents[i].ino == 0) continue;
            if (ents[i].name_len != name_len) continue;
            if (memcmp(ents[i].name, name, name_len) == 0) {
                *out_ino = ents[i].ino;
                if (out_type) *out_type = ents[i].type;
                return 0;
            }
        }
    }
    return -1;
}

static int dir_add_entry(uint64_t dir_ino, const char *name, uint64_t name_len,
                          uint64_t ino, uint8_t type) {
    if (name_len > TRPFS_MAX_NAME) return -1;

    trpfs_inode_raw_t node;
    if (read_inode(dir_ino, &node) != 0) return -1;
    if (inode_type(node.mode) != FILE_TYPE_DIR) return -1;

    uint64_t nblocks = DIRENTS_IN(&node);

    /* Look for a free slot in an existing block */
    for (uint64_t b = 0; b < nblocks; b++) {
        int dirty = 0;
        uint64_t pb = inode_block(&node, b, 0, &dirty);
        if (!pb) continue;
        if (blk_read(pb, g_dir_buf) != 0) continue;

        trpfs_dirent_t *ents = (trpfs_dirent_t *)g_dir_buf;
        for (uint64_t i = 0; i < TRPFS_DIRENTS_PER_BLK; i++) {
            if (ents[i].ino == 0) {
                memset(&ents[i], 0, sizeof(trpfs_dirent_t));
                ents[i].ino      = ino;
                ents[i].type     = type;
                ents[i].name_len = (uint8_t)name_len;
                memcpy(ents[i].name, name, name_len);
                blk_write(pb, g_dir_buf);
                node.links++;
                node.mtime = now_ticks();
                write_inode(dir_ino, &node);
                return 0;
            }
        }
    }

    /* No free slot — allocate a new directory block */
    int dirty = 0;
    uint64_t pb = inode_block(&node, nblocks, 1, &dirty);
    if (!pb) return -1;

    memset(g_dir_buf, 0, sizeof(g_dir_buf));
    trpfs_dirent_t *ents = (trpfs_dirent_t *)g_dir_buf;
    ents[0].ino      = ino;
    ents[0].type     = type;
    ents[0].name_len = (uint8_t)name_len;
    memcpy(ents[0].name, name, name_len);
    blk_write(pb, g_dir_buf);

    node.size = (nblocks + 1) * TRPFS_BLOCK_SIZE;
    node.links++;
    node.mtime = now_ticks();
    write_inode(dir_ino, &node);
    return 0;
}

/* ── Path resolution ─────────────────────────────────────────────────────── */

static int path_resolve(const char *path, uint64_t *out_ino, uint8_t *out_type) {
    uint64_t cur_ino  = g_sb.root_ino;
    uint8_t  cur_type = FILE_TYPE_DIR;

    const char *p = path;
    while (*p == '/') p++;

    while (*p) {
        const char *start = p;
        while (*p && *p != '/') p++;
        uint64_t len = (uint64_t)(p - start);

        if (cur_type != FILE_TYPE_DIR) return -1;
        if (len == 0 || len > TRPFS_MAX_NAME) return -1;

        uint64_t next_ino; uint8_t next_type;
        if (dir_lookup(cur_ino, start, len, &next_ino, &next_type) != 0) return -1;
        cur_ino  = next_ino;
        cur_type = next_type;

        while (*p == '/') p++;
    }

    *out_ino = cur_ino;
    if (out_type) *out_type = cur_type;
    return 0;
}

/* Resolve the parent directory of `path` and copy the final component into
 * leaf[] (capacity leaf_cap). Returns 0 on success. */
static int path_resolve_parent(const char *path, uint64_t *parent_ino,
                                char *leaf, int leaf_cap) {
    char tmp[TRPFS_MAX_PATH];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);

    /* Strip trailing slashes (but keep a single leading '/') */
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';

    /* Find last '/' */
    int slash = -1;
    for (int i = (int)len - 1; i >= 0; i--) {
        if (tmp[i] == '/') { slash = i; break; }
    }

    const char *leaf_src;
    if (slash < 0) {
        /* No slash at all: parent is root */
        if (path_resolve("/", parent_ino, NULL) != 0) return -1;
        leaf_src = tmp;
    } else if (slash == 0) {
        /* "/name" -> parent is root */
        if (path_resolve("/", parent_ino, NULL) != 0) return -1;
        leaf_src = tmp + 1;
    } else {
        tmp[slash] = '\0';
        uint8_t ptype;
        if (path_resolve(tmp, parent_ino, &ptype) != 0) return -1;
        if (ptype != FILE_TYPE_DIR) return -1;
        leaf_src = tmp + slash + 1;
    }

    size_t leaf_len = strlen(leaf_src);
    if (leaf_len == 0 || leaf_len >= (size_t)leaf_cap || leaf_len > TRPFS_MAX_NAME)
        return -1;

    memcpy(leaf, leaf_src, leaf_len + 1);
    return 0;
}

/* ── RAM disk backend ────────────────────────────────────────────────────── */

static int ramdisk_read(trpfs_blkdev_t *dev, uint64_t lba, void *buf) {
    if (lba >= dev->total_blocks) return -1;
    uint8_t *base = (uint8_t *)dev->ctx;
    memcpy(buf, base + lba * TRPFS_BLOCK_SIZE, TRPFS_BLOCK_SIZE);
    return 0;
}

static int ramdisk_write(trpfs_blkdev_t *dev, uint64_t lba, const void *buf) {
    if (lba >= dev->total_blocks) return -1;
    uint8_t *base = (uint8_t *)dev->ctx;
    memcpy(base + lba * TRPFS_BLOCK_SIZE, buf, TRPFS_BLOCK_SIZE);
    return 0;
}

int trpfs_ramdisk_init(trpfs_blkdev_t *dev, uint64_t size_bytes) {
    uint64_t blocks = size_bytes / TRPFS_BLOCK_SIZE;
    if (blocks == 0) return -1;

    uint8_t *mem = (uint8_t *)kmalloc(blocks * TRPFS_BLOCK_SIZE);
    if (!mem) return -1;
    memset(mem, 0, blocks * TRPFS_BLOCK_SIZE);

    dev->read_block  = ramdisk_read;
    dev->write_block = ramdisk_write;
    dev->total_blocks = blocks;
    dev->ctx = mem;
    return 0;
}

/* ── Volume management ───────────────────────────────────────────────────── */

int trpfs_is_mounted(void) { return g_dev != NULL; }

int trpfs_format(trpfs_blkdev_t *dev, const char *label) {
    if (!dev || dev->total_blocks < 16) return -1;

    memset(&g_sb, 0, sizeof(g_sb));
    g_sb.magic        = TRPFS_MAGIC;
    g_sb.version      = TRPFS_VERSION;
    g_sb.block_size   = TRPFS_BLOCK_SIZE;
    g_sb.total_blocks = dev->total_blocks;
    g_sb.root_ino     = TRPFS_ROOT_INO;

    g_sb.bitmap_blocks =
        (g_sb.total_blocks + (TRPFS_BLOCK_SIZE * 8) - 1) / (TRPFS_BLOCK_SIZE * 8);
    if (g_sb.bitmap_blocks == 0) g_sb.bitmap_blocks = 1;
    g_sb.bitmap_block = 1;

    /* One inode per 4 data blocks, at least 64, at most 65536 */
    g_sb.inode_count = g_sb.total_blocks / 4;
    if (g_sb.inode_count < 64)    g_sb.inode_count = 64;
    if (g_sb.inode_count > 65536) g_sb.inode_count = 65536;

    g_sb.inode_table_blocks =
        (g_sb.inode_count + TRPFS_INODES_PER_BLK - 1) / TRPFS_INODES_PER_BLK;
    g_sb.inode_table_block = g_sb.bitmap_block + g_sb.bitmap_blocks;

    g_sb.data_start_block = g_sb.inode_table_block + g_sb.inode_table_blocks;
    if (g_sb.data_start_block >= g_sb.total_blocks) return -1;

    if (label) {
        size_t n = strlen(label);
        if (n > sizeof(g_sb.label) - 1) n = sizeof(g_sb.label) - 1;
        memset(g_sb.label, 0, sizeof(g_sb.label));
        memcpy(g_sb.label, label, n);
    }

    g_dev = dev;

    /* Bitmap: mark every block before data_start_block as used, rest free */
    g_bitmap = (uint8_t *)kmalloc(g_sb.bitmap_blocks * TRPFS_BLOCK_SIZE);
    if (!g_bitmap) { g_dev = NULL; return -1; }
    memset(g_bitmap, 0, g_sb.bitmap_blocks * TRPFS_BLOCK_SIZE);
    for (uint64_t b = 0; b < g_sb.data_start_block; b++) bit_set(b);
    g_sb.free_blocks = g_sb.total_blocks - g_sb.data_start_block;

    /* Zero the inode table */
    memset(g_data_buf, 0, sizeof(g_data_buf));
    for (uint64_t b = 0; b < g_sb.inode_table_blocks; b++)
        blk_write(g_sb.inode_table_block + b, g_data_buf);
    g_sb.free_inodes = g_sb.inode_count;

    if (flush_bitmap() != 0) return -1;

    /* Create the root directory inode (empty) */
    trpfs_inode_raw_t root;
    memset(&root, 0, sizeof(root));
    root.mode  = make_mode(FILE_TYPE_DIR, FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X);
    root.links = 1;
    root.size  = 0;
    root.atime = root.mtime = root.ctime = now_ticks();
    g_sb.free_inodes--; /* root consumes inode #1 */
    if (write_inode(TRPFS_ROOT_INO, &root) != 0) return -1;

    return flush_superblock();
}

int trpfs_mount(trpfs_blkdev_t *dev) {
    if (!dev) return -1;

    uint8_t buf[TRPFS_BLOCK_SIZE];
    if (dev->read_block(dev, 0, buf) != 0) return -1;

    trpfs_superblock_t sb;
    memcpy(&sb, buf, sizeof(sb));

    if (sb.magic != TRPFS_MAGIC)        return -1;
    if (sb.block_size != TRPFS_BLOCK_SIZE) return -1;
    if (sb.total_blocks != dev->total_blocks) return -1;
    if (sizeof(trpfs_inode_raw_t) != TRPFS_INODE_SIZE) return -1; /* layout sanity */

    g_sb  = sb;
    g_dev = dev;

    if (g_bitmap) kfree(g_bitmap);
    g_bitmap = (uint8_t *)kmalloc(g_sb.bitmap_blocks * TRPFS_BLOCK_SIZE);
    if (!g_bitmap) { g_dev = NULL; return -1; }

    for (uint64_t i = 0; i < g_sb.bitmap_blocks; i++) {
        if (blk_read(g_sb.bitmap_block + i, g_bitmap + i * TRPFS_BLOCK_SIZE) != 0) {
            g_dev = NULL;
            return -1;
        }
    }

    memset(g_files, 0, sizeof(g_files));
    return 0;
}

int trpfs_sync(void) {
    if (!g_dev) return -1;
    if (flush_bitmap() != 0) return -1;
    return flush_superblock();
}

/* ── fs.h API ─────────────────────────────────────────────────────────────
 *
 * NOTE: fs_init() does NOT format/mount anything on its own — Toriginal OS
 * boots from a RAM disk that the installer (installer.c) sets up and mounts
 * explicitly via trpfs_ramdisk_init()+trpfs_format()/trpfs_mount(). This
 * keeps "no filesystem yet" an explicit, debuggable state rather than a
 * silent always-fails one.
 * ------------------------------------------------------------------------ */

void fs_init(void) {
    memset(g_files, 0, sizeof(g_files));
    serial_puts("[TRPFS] fs_init: waiting for a volume to be mounted.\n");
}

fd_t fs_open(const char *path, int flags, int mode) {
    if (!g_dev || !path) return -1;

    uint64_t ino; uint8_t type;
    int found = (path_resolve(path, &ino, &type) == 0);

    if (!found) {
        if (!(flags & O_CREAT)) return -1;

        uint64_t parent_ino;
        char leaf[TRPFS_MAX_NAME + 1];
        if (path_resolve_parent(path, &parent_ino, leaf, sizeof(leaf)) != 0) return -1;

        uint64_t new_ino = alloc_inode();
        if (!new_ino) return -1;

        trpfs_inode_raw_t node;
        memset(&node, 0, sizeof(node));
        node.mode  = make_mode(FILE_TYPE_REGULAR, (uint32_t)mode);
        node.links = 1;
        node.atime = node.mtime = node.ctime = now_ticks();
        if (write_inode(new_ino, &node) != 0) return -1;

        if (dir_add_entry(parent_ino, leaf, strlen(leaf), new_ino, FILE_TYPE_REGULAR) != 0) {
            free_inode(new_ino);
            return -1;
        }

        ino  = new_ino;
        type = FILE_TYPE_REGULAR;
    }

    if (type == FILE_TYPE_DIR && (flags & (O_WRONLY | O_RDWR))) return -1;

    if (flags & O_TRUNC) {
        trpfs_inode_raw_t node;
        if (read_inode(ino, &node) == 0 && type == FILE_TYPE_REGULAR) {
            free_inode_blocks(&node);
            node.size  = 0;
            node.mtime = now_ticks();
            write_inode(ino, &node);
        }
    }

    for (int i = 0; i < TRPFS_MAX_OPEN_FILES; i++) {
        if (!g_files[i].in_use) {
            g_files[i].in_use = 1;
            g_files[i].ino    = ino;
            g_files[i].flags  = flags;
            g_files[i].offset = 0;
            return i;
        }
    }
    return -1; /* too many open files */
}

ssize_t fs_read(fd_t fd, void *buf, size_t count) {
    if (fd < 0 || fd >= TRPFS_MAX_OPEN_FILES || !g_files[fd].in_use) return -1;
    trpfs_ofile_t *f = &g_files[fd];

    trpfs_inode_raw_t node;
    if (read_inode(f->ino, &node) != 0) return -1;

    if (f->offset >= node.size) return 0;

    uint64_t remain = node.size - f->offset;
    if ((uint64_t)count > remain) count = (size_t)remain;

    uint8_t *out = (uint8_t *)buf;
    size_t done = 0;

    while (done < count) {
        uint64_t blk_index  = (f->offset + done) / TRPFS_BLOCK_SIZE;
        uint64_t blk_offset = (f->offset + done) % TRPFS_BLOCK_SIZE;
        uint64_t chunk = TRPFS_BLOCK_SIZE - blk_offset;
        if (chunk > (count - done)) chunk = count - done;

        int dirty = 0;
        uint64_t pb = inode_block(&node, blk_index, 0, &dirty);
        if (!pb) {
            memset(out + done, 0, chunk); /* sparse hole */
        } else {
            if (blk_read(pb, g_data_buf) != 0) break;
            memcpy(out + done, g_data_buf + blk_offset, chunk);
        }
        done += chunk;
    }

    f->offset += done;
    node.atime = now_ticks();
    write_inode(f->ino, &node);
    return (ssize_t)done;
}

ssize_t fs_write(fd_t fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= TRPFS_MAX_OPEN_FILES || !g_files[fd].in_use) return -1;
    trpfs_ofile_t *f = &g_files[fd];
    if (!(f->flags & (O_WRONLY | O_RDWR))) return -1;

    trpfs_inode_raw_t node;
    if (read_inode(f->ino, &node) != 0) return -1;

    const uint8_t *in = (const uint8_t *)buf;
    size_t done = 0;
    int dirty = 0;

    while (done < count) {
        uint64_t blk_index  = (f->offset + done) / TRPFS_BLOCK_SIZE;
        uint64_t blk_offset = (f->offset + done) % TRPFS_BLOCK_SIZE;
        uint64_t chunk = TRPFS_BLOCK_SIZE - blk_offset;
        if (chunk > (count - done)) chunk = count - done;

        uint64_t pb = inode_block(&node, blk_index, 1, &dirty);
        if (!pb) break; /* out of space or beyond v1 max file size */

        if (chunk != TRPFS_BLOCK_SIZE) {
            if (blk_read(pb, g_data_buf) != 0) break;
        }
        memcpy(g_data_buf + blk_offset, in + done, chunk);
        if (blk_write(pb, g_data_buf) != 0) break;

        done += chunk;
    }

    f->offset += done;
    if (f->offset > node.size) { node.size = f->offset; dirty = 1; }
    node.mtime = now_ticks();
    if (dirty || done > 0) write_inode(f->ino, &node);

    return (ssize_t)done;
}

int fs_close(fd_t fd) {
    if (fd < 0 || fd >= TRPFS_MAX_OPEN_FILES || !g_files[fd].in_use) return -1;
    g_files[fd].in_use = 0;
    return 0;
}

int fs_seek(fd_t fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= TRPFS_MAX_OPEN_FILES || !g_files[fd].in_use) return -1;
    trpfs_ofile_t *f = &g_files[fd];

    trpfs_inode_raw_t node;
    if (read_inode(f->ino, &node) != 0) return -1;

    int64_t base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (int64_t)f->offset; break;
        case SEEK_END: base = (int64_t)node.size; break;
        default: return -1;
    }

    int64_t result = base + offset;
    if (result < 0) return -1;

    f->offset = (uint64_t)result;
    return 0;
}

int fs_mkdir(const char *path, int mode) {
    if (!g_dev || !path) return -1;

    uint64_t existing;
    if (path_resolve(path, &existing, NULL) == 0) return -1; /* already exists */

    uint64_t parent_ino;
    char leaf[TRPFS_MAX_NAME + 1];
    if (path_resolve_parent(path, &parent_ino, leaf, sizeof(leaf)) != 0) return -1;

    uint64_t new_ino = alloc_inode();
    if (!new_ino) return -1;

    trpfs_inode_raw_t node;
    memset(&node, 0, sizeof(node));
    node.mode  = make_mode(FILE_TYPE_DIR, (uint32_t)mode);
    node.links = 1;
    node.atime = node.mtime = node.ctime = now_ticks();
    if (write_inode(new_ino, &node) != 0) return -1;

    if (dir_add_entry(parent_ino, leaf, strlen(leaf), new_ino, FILE_TYPE_DIR) != 0) {
        free_inode(new_ino);
        return -1;
    }

    return 0;
}

int fs_stat(const char *path, inode_t *stat) {
    if (!g_dev || !path || !stat) return -1;

    uint64_t ino; uint8_t type;
    if (path_resolve(path, &ino, &type) != 0) return -1;

    trpfs_inode_raw_t node;
    if (read_inode(ino, &node) != 0) return -1;

    memset(stat, 0, sizeof(*stat));
    stat->ino           = ino;
    stat->mode          = node.mode;
    stat->uid           = node.uid;
    stat->gid           = node.gid;
    stat->size          = node.size;
    stat->accessed_time = node.atime;
    stat->modified_time = node.mtime;
    stat->created_time  = node.ctime;
    stat->link_count    = node.links;

    uint32_t blocks = 0;
    for (int i = 0; i < TRPFS_DIRECT_BLOCKS; i++) {
        stat->blocks[i] = node.direct[i];
        if (node.direct[i]) blocks++;
    }
    stat->indirect_block   = node.indirect;
    stat->double_indirect  = node.double_indirect;
    stat->triple_indirect  = node.triple_indirect;
    stat->block_count      = blocks;
    stat->data             = NULL;

    return 0;
}

/* ── fs_unlink — delete a regular file ───────────────────────────────────
 * Finds the entry in the parent directory, zeroes it, decrements the
 * parent's link count, then frees the inode's data blocks and the inode
 * itself. Does NOT remove non-empty directories (use rmdir for that later).
 * Returns 0 on success, -1 on error. */
int fs_unlink(const char *path) {
    if (!g_dev || !path) return -1;

    uint64_t ino; uint8_t type;
    if (path_resolve(path, &ino, &type) != 0) return -1;
    if (type == FILE_TYPE_DIR) return -1; /* refuse to unlink a directory */

    uint64_t parent_ino;
    char leaf[TRPFS_MAX_NAME + 1];
    if (path_resolve_parent(path, &parent_ino, leaf, sizeof(leaf)) != 0) return -1;

    size_t leaf_len = strlen(leaf);

    /* Zero the dirent in the parent directory block. */
    trpfs_inode_raw_t pnode;
    if (read_inode(parent_ino, &pnode) != 0) return -1;

    uint64_t nblocks = DIRENTS_IN(&pnode);
    int found = 0;
    for (uint64_t b = 0; b < nblocks && !found; b++) {
        int dirty = 0;
        uint64_t pb = inode_block(&pnode, b, 0, &dirty);
        if (!pb) continue;
        if (blk_read(pb, g_dir_buf) != 0) continue;

        trpfs_dirent_t *ents = (trpfs_dirent_t *)g_dir_buf;
        for (uint64_t i = 0; i < TRPFS_DIRENTS_PER_BLK; i++) {
            if (ents[i].ino != ino) continue;
            if (ents[i].name_len != (uint8_t)leaf_len) continue;
            if (memcmp(ents[i].name, leaf, leaf_len) != 0) continue;
            memset(&ents[i], 0, sizeof(trpfs_dirent_t));
            blk_write(pb, g_dir_buf);
            found = 1;
            break;
        }
    }
    if (!found) return -1;

    /* Update parent link count and mtime. */
    pnode.links--;
    pnode.mtime = now_ticks();
    write_inode(parent_ino, &pnode);

    /* Free the file's data blocks and inode. */
    free_inode(ino);
    return 0;
}

/* ── fs_readdir — enumerate entries in a directory ───────────────────────
 * Resolves 'path' to a directory inode, then walks every directory block
 * calling cb(name, name_len, type, ctx) for each live (ino != 0) entry.
 * cb return value: 0 = continue, non-zero = stop early.
 * Returns 0 on success, -1 on error (path not found, not a dir, I/O). */
int fs_readdir(const char *path,
               int (*cb)(const char *name, uint8_t name_len,
                         uint8_t type, void *ctx),
               void *ctx)
{
    if (!g_dev) return -1;

    uint64_t dir_ino = 0;
    uint8_t  type    = 0;
    if (path_resolve(path, &dir_ino, &type) != 0) return -1;
    if (type != FILE_TYPE_DIR) return -1;

    trpfs_inode_raw_t node;
    if (read_inode(dir_ino, &node) != 0) return -1;

    uint64_t nblocks = DIRENTS_IN(&node);
    int dummy = 0;

    for (uint64_t bidx = 0; bidx < nblocks; bidx++) {
        uint64_t pb = inode_block(&node, bidx, 0, &dummy);
        if (!pb) continue;
        if (blk_read(pb, g_dir_buf) != 0) continue;

        trpfs_dirent_t *ents = (trpfs_dirent_t *)g_dir_buf;
        for (int i = 0; i < TRPFS_DIRENTS_PER_BLK; i++) {
            if (ents[i].ino == 0) continue; /* unused slot */
            int r = cb(ents[i].name, ents[i].name_len, ents[i].type, ctx);
            if (r != 0) return 0; /* caller asked to stop */
        }
    }
    return 0;
}
