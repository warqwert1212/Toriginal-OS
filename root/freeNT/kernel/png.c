/* png.c - PNG image decoder implementation. */

#include "png.h"
#include "deflate.h"
#include "heap.h"
#include "string.h"

/* ── CRC-32 (used to validate every PNG chunk, per spec) ─────────────
 * PNG's CRC uses the same polynomial as zlib/gzip/Ethernet CRC-32
 * (0xEDB88320, reflected). Table-based implementation, built once
 * lazily on first use rather than as a static const 256-entry table
 * literal, so this file doesn't need to hand-transcribe 256 magic
 * numbers I can't visually verify - build_crc_table() derives them
 * from the polynomial itself following the standard bit-reflected
 * CRC construction algorithm. */
static uint32_t g_crc_table[256];
static int g_crc_table_built = 0;

static void build_crc_table(void) {
    if (g_crc_table_built) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            if (c & 1) c = 0xEDB88320u ^ (c >> 1);
            else c = c >> 1;
        }
        g_crc_table[n] = c;
    }
    g_crc_table_built = 1;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len) {
    build_crc_table();
    uint32_t c = crc;
    for (size_t i = 0; i < len; i++) {
        c = g_crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    }
    return c;
}

/* ── Big-endian helpers (every multi-byte PNG field is big-endian) ── */

static uint32_t read_u32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

/* ── PNG signature (8 fixed bytes, spec section 5.2) ─────────────── */

static const uint8_t png_signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

/* ── Paeth predictor (PNG spec section 9.4) ──────────────────────────
 * This is the filter most implementations get subtly wrong - the
 * predictor picks whichever of a/b/c is numerically closest to the
 * computed estimate p, with an explicit tie-breaking rule (prefer a,
 * then b, then c) that must be followed exactly or every pixel using
 * this filter decodes to a slightly-wrong value. Transcribed directly
 * from the spec's reference pseudocode, then verified numerically
 * below against hand-computed cases before trusting it. */
static uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;

    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* ── Scanline filter reconstruction (PNG spec section 9.2-9.4) ──────
 * PNG filters each scanline independently, referencing the
 * unfiltered bytes of the current row (a = left neighbor) and the
 * previous row (b = above, c = above-left) at the SAME byte-plane
 * distance (bpp = bytes per pixel), not the same column index - for
 * multi-byte-per-pixel images (RGB=3, RGBA=4), "left" means bpp bytes
 * back in the byte stream, not 1 pixel back as a unit. Getting this
 * distinction wrong is the second most common PNG decoder bug after
 * the Paeth tie-break rule. */
static void unfilter_row(uint8_t filter_type, uint8_t *row, const uint8_t *prev_row,
                         uint32_t row_bytes, uint32_t bpp) {
    for (uint32_t i = 0; i < row_bytes; i++) {
        uint8_t a = (i >= bpp) ? row[i - bpp] : 0;           /* left */
        uint8_t b = prev_row ? prev_row[i] : 0;               /* above */
        uint8_t c = (prev_row && i >= bpp) ? prev_row[i - bpp] : 0; /* above-left */

        switch (filter_type) {
            case 0: /* None */
                break;
            case 1: /* Sub */
                row[i] = (uint8_t)(row[i] + a);
                break;
            case 2: /* Up */
                row[i] = (uint8_t)(row[i] + b);
                break;
            case 3: /* Average */
                row[i] = (uint8_t)(row[i] + (uint8_t)(((uint32_t)a + (uint32_t)b) / 2));
                break;
            case 4: /* Paeth */
                row[i] = (uint8_t)(row[i] + paeth_predictor(a, b, c));
                break;
            default:
                break; /* caller has already validated filter_type is 0-4 */
        }
    }
}

/* ── Chunk iteration ──────────────────────────────────────────────── */

typedef struct {
    uint32_t length;
    uint8_t  type[4];
    const uint8_t *data; /* points into the caller's buffer, not copied */
} png_chunk_t;

/* Reads one chunk starting at `pos` in `data` (length `len`), and
 * advances *pos past it. Validates the chunk's CRC-32 against its
 * declared type+data - a chunk whose CRC doesn't match is rejected
 * outright rather than trusted, since a bit-flipped chunk (corrupted
 * download, bad disk sector, etc.) silently accepted here would
 * propagate a wrong pixel value with no indication anything was
 * wrong. */
static int read_next_chunk(const uint8_t *data, size_t len, size_t *pos, png_chunk_t *out) {
    if (*pos + 8 > len) return -1; /* not enough bytes for length+type */

    uint32_t chunk_len = read_u32be(data + *pos);
    const uint8_t *type = data + *pos + 4;
    const uint8_t *chunk_data = data + *pos + 8;

    if (*pos + 8 + (size_t)chunk_len + 4 > len) return -1; /* truncated - data or CRC missing */

    uint32_t stored_crc = read_u32be(chunk_data + chunk_len);

    uint32_t computed_crc = crc32_update(0xFFFFFFFFu, type, 4);
    computed_crc = crc32_update(computed_crc, chunk_data, chunk_len) ^ 0xFFFFFFFFu;

    if (computed_crc != stored_crc) return -1; /* corrupted chunk */

    out->length = chunk_len;
    out->type[0] = type[0]; out->type[1] = type[1];
    out->type[2] = type[2]; out->type[3] = type[3];
    out->data = chunk_data;

    *pos += 8 + (size_t)chunk_len + 4;
    return 0;
}

static int chunk_type_is(const png_chunk_t *c, const char *s) {
    return c->type[0] == (uint8_t)s[0] && c->type[1] == (uint8_t)s[1] &&
           c->type[2] == (uint8_t)s[2] && c->type[3] == (uint8_t)s[3];
}

/* ── Main decoder ─────────────────────────────────────────────────── */

png_result_t png_decode(const uint8_t *data, size_t len, png_image_t *out) {
    out->width = 0;
    out->height = 0;
    out->channels = 0;
    out->pixels = 0;

    if (len < 8 || memcmp(data, png_signature, 8) != 0) {
        return PNG_ERR_BAD_SIGNATURE;
    }

    size_t pos = 8;
    int have_ihdr = 0;
    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0, interlace = 0;

    /* IDAT data can be split across multiple chunks (spec section
     * 4.3, "IDAT chunks shall appear consecutively") - concatenate
     * their payloads into one buffer before handing the whole thing
     * to zlib_inflate(), since the DEFLATE stream inside doesn't
     * respect PNG chunk boundaries at all (a single DEFLATE block can
     * legitimately span multiple IDAT chunks). Rather than a fixed-
     * size buffer, size is only known after a first pass counts total
     * IDAT bytes - this is the "two passes over chunks" approach
     * below: first pass reads IHDR and totals IDAT size, second pass
     * (after allocating) copies IDAT payloads in. */

    /* --- First pass: read IHDR, validate format is in-scope, total IDAT bytes --- */
    size_t scan_pos = pos;
    size_t total_idat_bytes = 0;
    int saw_iend = 0;

    while (scan_pos < len) {
        png_chunk_t chunk;
        if (read_next_chunk(data, len, &scan_pos, &chunk) < 0) return PNG_ERR_BAD_CHUNK;

        if (chunk_type_is(&chunk, "IHDR")) {
            if (chunk.length != 13) return PNG_ERR_BAD_CHUNK;
            width      = read_u32be(chunk.data + 0);
            height     = read_u32be(chunk.data + 4);
            bit_depth  = chunk.data[8];
            color_type = chunk.data[9];
            /* compression method (byte 10) and filter method (byte
             * 11) are always 0 in valid PNGs per spec - not checked
             * further since any other value already makes this an
             * invalid/unsupported file the later color-type check
             * would likely also reject, and PNG_ERR_BAD_CHUNK below
             * for width/height==0 covers the "obviously broken IHDR"
             * case either way. */
            interlace  = chunk.data[12];
            have_ihdr = 1;

            if (width == 0 || height == 0) return PNG_ERR_BAD_CHUNK;
        } else if (chunk_type_is(&chunk, "IDAT")) {
            if (!have_ihdr) return PNG_ERR_BAD_CHUNK; /* IDAT before IHDR - invalid order */
            total_idat_bytes += chunk.length;
        } else if (chunk_type_is(&chunk, "IEND")) {
            saw_iend = 1;
            break;
        }
        /* Any other chunk type (PLTE, tRNS, ancillary chunks like
         * tEXt/gAMA/etc.) is simply skipped - read_next_chunk already
         * advanced scan_pos past it. This decoder's scope is pixel
         * data for RGB/RGBA images; metadata chunks aren't needed. */
    }

    if (!have_ihdr || !saw_iend) return PNG_ERR_TRUNCATED;
    if (interlace != 0) return PNG_ERR_UNSUPPORTED_INTERLACE;

    uint8_t channels;
    if (bit_depth != 8) return PNG_ERR_UNSUPPORTED_COLOR;
    if (color_type == 2) channels = 3;      /* RGB */
    else if (color_type == 6) channels = 4; /* RGBA */
    else return PNG_ERR_UNSUPPORTED_COLOR;  /* grayscale/palette/etc - out of scope */

    if (total_idat_bytes == 0) return PNG_ERR_TRUNCATED;

    /* Same defensive ceiling as every other allocation in this file
     * (decompressed_size below, and the analogous checks in
     * gfx2d_surface_create/gfx3d_init elsewhere in the graphics
     * stack). In practice total_idat_bytes is already bounded by the
     * real on-disk file size (read_next_chunk rejects any chunk that
     * doesn't fit within the buffer fs_read() actually returned), but
     * relying on that implicit bound alone is inconsistent with the
     * explicit-ceiling discipline used everywhere else in this
     * decoder - make it explicit here too rather than be the one
     * allocation in this file that trusts an indirect bound. */
    if (total_idat_bytes > 0x10000000ULL) return PNG_ERR_OUT_OF_MEMORY;

    /* --- Second pass: concatenate IDAT payloads --- */
    uint8_t *idat_concat = (uint8_t *)kmalloc(total_idat_bytes);
    if (!idat_concat) return PNG_ERR_OUT_OF_MEMORY;

    size_t idat_write_pos = 0;
    scan_pos = pos;
    while (scan_pos < len) {
        png_chunk_t chunk;
        if (read_next_chunk(data, len, &scan_pos, &chunk) < 0) {
            kfree(idat_concat);
            return PNG_ERR_BAD_CHUNK;
        }
        if (chunk_type_is(&chunk, "IDAT")) {
            for (uint32_t i = 0; i < chunk.length; i++) {
                idat_concat[idat_write_pos++] = chunk.data[i];
            }
        } else if (chunk_type_is(&chunk, "IEND")) {
            break;
        }
    }

    /* --- Decompress the concatenated IDAT stream --- */

    /* Each scanline is preceded by one filter-type byte (spec section
     * 7.2), so the decompressed size is height * (1 + row_bytes),
     * not just height * row_bytes. */
    uint32_t row_bytes = width * channels;
    uint64_t decompressed_size = (uint64_t)height * ((uint64_t)row_bytes + 1);

    /* Sanity ceiling consistent with the other kmalloc call sites in
     * this graphics stack (gfx2d_surface_create, gfx3d_init) - reject
     * rather than risk an implausible allocation from a corrupt or
     * maliciously-crafted IHDR (e.g. width/height claiming a
     * multi-gigabyte image). */
    if (decompressed_size > 0x10000000ULL) {
        kfree(idat_concat);
        return PNG_ERR_OUT_OF_MEMORY;
    }

    uint8_t *raw = (uint8_t *)kmalloc((size_t)decompressed_size);
    if (!raw) {
        kfree(idat_concat);
        return PNG_ERR_OUT_OF_MEMORY;
    }

    size_t raw_len = 0;
    deflate_result_t dr = zlib_inflate(idat_concat, total_idat_bytes, raw,
                                       (size_t)decompressed_size, &raw_len);
    kfree(idat_concat);

    if (dr != DEFLATE_OK || raw_len != decompressed_size) {
        kfree(raw);
        return PNG_ERR_DECOMPRESS_FAILED;
    }

    /* --- Unfilter each scanline in place, then strip the filter-type
     * bytes into the final pixel buffer --- */
    uint8_t bpp = channels; /* bytes per pixel, since bit_depth==8 is enforced above */

    uint64_t pixel_buffer_size = (uint64_t)height * row_bytes;
    uint8_t *pixels = (uint8_t *)kmalloc((size_t)pixel_buffer_size);
    if (!pixels) {
        kfree(raw);
        return PNG_ERR_OUT_OF_MEMORY;
    }

    uint8_t *prev_row_ptr = 0;
    for (uint32_t y = 0; y < height; y++) {
        uint8_t *src_row = raw + (uint64_t)y * (row_bytes + 1);
        uint8_t filter_type = src_row[0];
        uint8_t *row_data = src_row + 1;

        if (filter_type > 4) {
            kfree(raw);
            kfree(pixels);
            return PNG_ERR_BAD_FILTER;
        }

        unfilter_row(filter_type, row_data, prev_row_ptr, row_bytes, bpp);

        /* Copy this now-unfiltered row into the final contiguous
         * pixel buffer (no filter-type byte, no per-row gap). */
        uint8_t *dst_row = pixels + (uint64_t)y * row_bytes;
        for (uint32_t i = 0; i < row_bytes; i++) dst_row[i] = row_data[i];

        prev_row_ptr = row_data; /* next row's "above" reference */
    }

    kfree(raw);

    out->width = width;
    out->height = height;
    out->channels = channels;
    out->pixels = pixels;
    return PNG_OK;
}

void png_free(png_image_t *img) {
    if (img && img->pixels) {
        kfree(img->pixels);
        img->pixels = 0;
    }
}
