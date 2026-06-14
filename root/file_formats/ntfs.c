/* =============================================================================
 * NTFS.C — basic NTFS volume detection and read-only metadata access
 * See ntfs.h for scope.
 * ========================================================================= */

#include "ntfs.h"
#include "string.h"

void serial_puts(const char *s);

/* ── byte-level helpers (NTFS structures are little-endian, unaligned) ──── */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) {
    uint64_t lo = rd32(p);
    uint64_t hi = rd32(p + 4);
    return lo | (hi << 32);
}

/* Read up to TRPFS_BLOCK_SIZE bytes starting at an arbitrary byte offset,
 * possibly spanning two device blocks. */
static int read_bytes(trpfs_blkdev_t *dev, uint64_t byte_off, void *out, uint32_t len) {
    if (len > TRPFS_BLOCK_SIZE) return -1;

    uint64_t blk        = byte_off / TRPFS_BLOCK_SIZE;
    uint32_t off_in_blk = (uint32_t)(byte_off % TRPFS_BLOCK_SIZE);

    static uint8_t tmp[2 * TRPFS_BLOCK_SIZE];
    if (dev->read_block(dev, blk, tmp) != 0) return -1;

    if (off_in_blk + len > TRPFS_BLOCK_SIZE) {
        if (blk + 1 >= dev->total_blocks) return -1;
        if (dev->read_block(dev, blk + 1, tmp + TRPFS_BLOCK_SIZE) != 0) return -1;
    }

    memcpy(out, tmp + off_in_blk, len);
    return 0;
}

/* ── Boot sector ─────────────────────────────────────────────────────────── */

int ntfs_detect(trpfs_blkdev_t *dev, ntfs_volume_t *out) {
    memset(out, 0, sizeof(*out));

    uint8_t sec[512];
    if (read_bytes(dev, 0, sec, sizeof(sec)) != 0) return -1;

    /* OEM ID lives at byte offset 3, length 8 */
    if (memcmp(sec + 3, "NTFS    ", 8) != 0) {
        out->detected = 0;
        return 0;
    }

    out->detected             = 1;
    out->bytes_per_sector     = rd16(sec + 0x0B);
    out->sectors_per_cluster  = sec[0x0D];
    out->bytes_per_cluster    = out->bytes_per_sector * out->sectors_per_cluster;
    out->total_sectors        = rd64(sec + 0x28);
    out->mft_lcn              = rd64(sec + 0x30);
    out->mft_mirror_lcn       = rd64(sec + 0x38);
    out->volume_serial        = rd64(sec + 0x48);

    int8_t cpr = (int8_t)sec[0x40];
    if (cpr > 0) {
        out->mft_record_size = (uint32_t)cpr * out->bytes_per_cluster;
    } else {
        /* negative: record size = 2^|cpr| bytes */
        uint32_t shift = (uint32_t)(-cpr);
        out->mft_record_size = (shift < 16) ? (1u << shift) : 1024;
    }
    if (out->mft_record_size == 0 || out->mft_record_size > TRPFS_BLOCK_SIZE)
        out->mft_record_size = 1024; /* sane default */

    return 0;
}

/* ── MFT record reading with USA fixup ───────────────────────────────────── */

int ntfs_read_mft_record(trpfs_blkdev_t *dev, const ntfs_volume_t *vol,
                          uint64_t record, uint8_t *buf) {
    if (!vol->detected) return -1;
    if (vol->bytes_per_cluster == 0 || vol->mft_record_size == 0) return -1;

    uint64_t mft_base = vol->mft_lcn * vol->bytes_per_cluster;
    uint64_t off       = mft_base + record * vol->mft_record_size;

    if (read_bytes(dev, off, buf, vol->mft_record_size) != 0) return -1;

    /* Validate "FILE" signature */
    if (memcmp(buf, "FILE", 4) != 0) return -1;

    /* Apply Update Sequence Array fixups: the last 2 bytes of every
     * bytes_per_sector chunk hold a USN that must be restored from the
     * USA table before the record can be parsed safely. */
    uint16_t usa_ofs = rd16(buf + 0x04);
    uint16_t usa_cnt = rd16(buf + 0x06);
    uint32_t bps     = vol->bytes_per_sector ? vol->bytes_per_sector : 512;

    if (usa_ofs + (uint32_t)usa_cnt * 2 <= vol->mft_record_size) {
        for (uint32_t i = 1; i < usa_cnt; i++) {
            uint32_t sector_end = i * bps - 2;
            if (sector_end + 2 <= vol->mft_record_size) {
                buf[sector_end]     = buf[usa_ofs + i * 2];
                buf[sector_end + 1] = buf[usa_ofs + i * 2 + 1];
            }
        }
    }

    return 0;
}

/* ── $VOLUME_NAME extraction ─────────────────────────────────────────────── */

void ntfs_read_volume_label(trpfs_blkdev_t *dev, ntfs_volume_t *vol) {
    vol->volume_label[0] = '\0';
    if (!vol->detected) return;

    uint8_t rec[TRPFS_BLOCK_SIZE];
    if (ntfs_read_mft_record(dev, vol, 3 /* $Volume */, rec) != 0) {
        serial_puts("[NTFS] could not read $Volume MFT record\n");
        return;
    }

    uint16_t attrs_off = rd16(rec + 0x14);
    uint32_t pos = attrs_off;

    while (pos + 8 <= vol->mft_record_size) {
        uint32_t type   = rd32(rec + pos);
        uint32_t length = rd32(rec + pos + 4);

        if (type == NTFS_ATTR_END || length == 0) break;
        if (pos + length > vol->mft_record_size) break;

        if (type == NTFS_ATTR_VOLUME_NAME) {
            uint8_t non_resident = rec[pos + 8];
            if (!non_resident) {
                uint32_t value_len = rd32(rec + pos + 0x10);
                uint16_t value_off = rd16(rec + pos + 0x14);

                if (pos + value_off + value_len <= vol->mft_record_size) {
                    const uint8_t *u16name = rec + pos + value_off;
                    uint32_t chars = value_len / 2;
                    uint32_t out_i = 0;
                    uint32_t max_out = sizeof(vol->volume_label) - 1;

                    for (uint32_t i = 0; i < chars && out_i < max_out; i++) {
                        uint16_t ch = rd16(u16name + i * 2);
                        vol->volume_label[out_i++] = (ch < 0x80) ? (char)ch : '?';
                    }
                    vol->volume_label[out_i] = '\0';
                }
            }
            return;
        }

        pos += length;
    }

    serial_puts("[NTFS] $VOLUME_NAME attribute not found\n");
}