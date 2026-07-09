/* deflate.c - RFC 1951 DEFLATE decompressor implementation. */

#include "deflate.h"

/* ── Bit reader ───────────────────────────────────────────────────────
 * DEFLATE packs bits LSB-first within each byte (RFC 1951 section
 * 3.1.1: "packing the bits starting with the least-significant bit
 * of the byte"). This reader pulls bits in that order and supports
 * reading up to 16 bits at a time (the longest DEFLATE code length,
 * plus small fixed-width fields, never exceeds this). */
typedef struct {
    const uint8_t *data;
    size_t len;
    size_t byte_pos;
    uint32_t bit_buf;
    int bit_count;
} bitreader_t;

static void br_init(bitreader_t *br, const uint8_t *data, size_t len) {
    br->data = data;
    br->len = len;
    br->byte_pos = 0;
    br->bit_buf = 0;
    br->bit_count = 0;
}

/* Returns -1 if the stream runs out of input mid-read (truncated
 * data) - every caller checks for this rather than silently treating
 * exhausted input as zero bits, which would let a truncated file
 * "successfully" decode into wrong/incomplete output. */
static int br_need_bits(bitreader_t *br, int n) {
    while (br->bit_count < n) {
        if (br->byte_pos >= br->len) return -1;
        br->bit_buf |= ((uint32_t)br->data[br->byte_pos]) << br->bit_count;
        br->byte_pos++;
        br->bit_count += 8;
    }
    return 0;
}

static int br_get_bits(bitreader_t *br, int n, uint32_t *out) {
    if (n == 0) { *out = 0; return 0; }
    if (br_need_bits(br, n) < 0) return -1;
    *out = br->bit_buf & ((1u << n) - 1u);
    br->bit_buf >>= n;
    br->bit_count -= n;
    return 0;
}

/* Discards any partial bits in the current byte, advancing to the
 * next byte boundary - required before a stored (uncompressed)
 * block, which is byte-aligned per RFC 1951 section 3.2.4. */
static void br_align_to_byte(bitreader_t *br) {
    br->bit_buf = 0;
    br->bit_count = 0;
}

/* ── Canonical Huffman decoding ───────────────────────────────────────
 * DEFLATE Huffman codes are "canonical": fully determined by the code
 * LENGTH of each symbol (RFC 1951 section 3.2.2). This table stores,
 * per code length, the first code value and the range of symbol
 * indices - a standard, well-tested approach to canonical Huffman
 * decoding (build counts per length, derive first-code-per-length,
 * then decode bit-by-bit checking against those ranges). */
#define MAX_HUFFMAN_CODE_LEN 15
#define MAX_HUFFMAN_SYMBOLS  288  /* enough for the literal/length alphabet */

typedef struct {
    uint16_t counts[MAX_HUFFMAN_CODE_LEN + 1]; /* counts[len] = how many symbols have this code length */
    uint16_t symbols[MAX_HUFFMAN_SYMBOLS];     /* symbols in canonical order */
    int num_symbols;
} huffman_table_t;

/* Builds a canonical Huffman table from an array of code lengths (one
 * per symbol, 0 = symbol unused). This is the standard construction
 * algorithm from RFC 1951 section 3.2.2, and separately, the widely-
 * used "puff.c"-style table-based decode approach (counts + sorted
 * symbol list) rather than building an explicit tree - simpler to
 * verify correct and to bounds-check. */
static void huffman_build(huffman_table_t *table, const uint8_t *lengths, int num_lengths) {
    int i;
    for (i = 0; i <= MAX_HUFFMAN_CODE_LEN; i++) table->counts[i] = 0;
    for (i = 0; i < num_lengths; i++) table->counts[lengths[i]]++;
    table->counts[0] = 0; /* unused symbols don't participate */

    /* offsets[len] = index into table->symbols where symbols of this
     * length start, computed as a running sum of counts. */
    uint16_t offsets[MAX_HUFFMAN_CODE_LEN + 2];
    offsets[1] = 0;
    for (i = 1; i <= MAX_HUFFMAN_CODE_LEN; i++) {
        offsets[i + 1] = (uint16_t)(offsets[i] + table->counts[i]);
    }

    for (i = 0; i < num_lengths; i++) {
        if (lengths[i] != 0) {
            table->symbols[offsets[lengths[i]]] = (uint16_t)i;
            offsets[lengths[i]]++;
        }
    }
    table->num_symbols = num_lengths;
}

/* Decodes one symbol from the bit stream using `table`. Returns the
 * symbol value, or -1 on truncated input / an invalid code (a bit
 * pattern that doesn't correspond to any valid canonical code at any
 * length up to MAX_HUFFMAN_CODE_LEN - this can only happen on
 * corrupted input, and is reported as an error rather than silently
 * returning symbol 0 or similar). */
static int huffman_decode(bitreader_t *br, const huffman_table_t *table) {
    int code = 0;
    int first = 0;
    int index = 0;

    for (int len = 1; len <= MAX_HUFFMAN_CODE_LEN; len++) {
        uint32_t bit;
        if (br_get_bits(br, 1, &bit) < 0) return -1;
        code |= (int)bit;

        int count = table->counts[len];
        if (code - first < count) {
            return table->symbols[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1; /* no valid code matched within the max length - corrupt input */
}

/* ── Fixed Huffman tables (RFC 1951 section 3.2.6) ───────────────────
 * Used for DEFLATE block type 1 (fixed Huffman codes). These lengths
 * are literally specified in the RFC, not computed - transcribed
 * directly from the spec's explicit ranges:
 *   0-143:   8 bits
 *   144-255: 9 bits
 *   256-279: 7 bits
 *   280-287: 8 bits
 * and for distances, all 30 codes are 5 bits. */
static huffman_table_t g_fixed_lit_table;
static huffman_table_t g_fixed_dist_table;
static int g_fixed_tables_built = 0;

static void build_fixed_tables(void) {
    if (g_fixed_tables_built) return;

    uint8_t lit_lengths[288];
    int i;
    for (i = 0;   i <= 143; i++) lit_lengths[i] = 8;
    for (i = 144; i <= 255; i++) lit_lengths[i] = 9;
    for (i = 256; i <= 279; i++) lit_lengths[i] = 7;
    for (i = 280; i <= 287; i++) lit_lengths[i] = 8;
    huffman_build(&g_fixed_lit_table, lit_lengths, 288);

    uint8_t dist_lengths[30];
    for (i = 0; i < 30; i++) dist_lengths[i] = 5;
    huffman_build(&g_fixed_dist_table, dist_lengths, 30);

    g_fixed_tables_built = 1;
}

/* ── Length/distance extra-bit tables (RFC 1951 section 3.2.5) ──────
 * Transcribed directly from the RFC's explicit tables - these are
 * fixed data, not derived, so "correct" here means "matches the
 * spec's literal table", which is what's written below. */
static const uint16_t length_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t length_extra_bits[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
    3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,
    8193,12289,16385,24577
};
static const uint8_t dist_extra_bits[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* Code-length alphabet order for dynamic Huffman tables (RFC 1951
 * section 3.2.7) - the code-length codes themselves are transmitted
 * in this specific, spec-mandated order, not sequential 0..18. */
static const uint8_t code_length_order[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

/* ── Dynamic Huffman table parsing (RFC 1951 section 3.2.7) ─────────
 * Reads HLIT/HDIST/HCLEN counts, then the code-length code-lengths
 * (used to decode the actual literal/length and distance code
 * lengths, which may themselves be run-length encoded via codes
 * 16/17/18). */
static int read_dynamic_tables(bitreader_t *br, huffman_table_t *lit_table,
                               huffman_table_t *dist_table) {
    uint32_t hlit, hdist, hclen;
    if (br_get_bits(br, 5, &hlit) < 0) return -1;
    if (br_get_bits(br, 5, &hdist) < 0) return -1;
    if (br_get_bits(br, 4, &hclen) < 0) return -1;

    int num_lit_codes = (int)hlit + 257;
    int num_dist_codes = (int)hdist + 1;
    int num_clen_codes = (int)hclen + 4;

    uint8_t clen_lengths[19] = {0};
    for (int i = 0; i < num_clen_codes; i++) {
        uint32_t bits;
        if (br_get_bits(br, 3, &bits) < 0) return -1;
        clen_lengths[code_length_order[i]] = (uint8_t)bits;
    }

    huffman_table_t clen_table;
    huffman_build(&clen_table, clen_lengths, 19);

    /* Decode the actual literal/length + distance code lengths, which
     * are concatenated into one logical sequence and can reference
     * "repeat previous" (16), "repeat zero x3-10" (17), or "repeat
     * zero x11-138" (18) - per RFC 1951 section 3.2.7. */
    uint8_t all_lengths[288 + 32] = {0};
    int total_codes = num_lit_codes + num_dist_codes;
    int i = 0;

    /* Defensive bound: num_lit_codes maxes at 257+31=288, num_dist_codes
     * maxes at 1+31=32, so total_codes maxes at 320 - matches the
     * all_lengths[288+32] array exactly with no slack, so this check
     * is the real guard against a malicious/corrupt HLIT+HDIST
     * combination overrunning the array, not a redundant formality. */
    if (total_codes > 288 + 32) return -1;

    while (i < total_codes) {
        int sym = huffman_decode(br, &clen_table);
        if (sym < 0) return -1;

        if (sym < 16) {
            all_lengths[i++] = (uint8_t)sym;
        } else if (sym == 16) {
            if (i == 0) return -1; /* "repeat previous" with no previous - corrupt */
            uint32_t repeat;
            if (br_get_bits(br, 2, &repeat) < 0) return -1;
            repeat += 3;
            uint8_t prev = all_lengths[i - 1];
            while (repeat-- > 0 && i < total_codes) all_lengths[i++] = prev;
        } else if (sym == 17) {
            uint32_t repeat;
            if (br_get_bits(br, 3, &repeat) < 0) return -1;
            repeat += 3;
            while (repeat-- > 0 && i < total_codes) all_lengths[i++] = 0;
        } else { /* sym == 18 */
            uint32_t repeat;
            if (br_get_bits(br, 7, &repeat) < 0) return -1;
            repeat += 11;
            while (repeat-- > 0 && i < total_codes) all_lengths[i++] = 0;
        }
    }

    huffman_build(lit_table, all_lengths, num_lit_codes);
    huffman_build(dist_table, all_lengths + num_lit_codes, num_dist_codes);
    return 0;
}

/* ── Main DEFLATE decompression loop ─────────────────────────────── */

deflate_result_t deflate_decompress(const uint8_t *in, size_t in_len,
                                    uint8_t *out, size_t out_capacity,
                                    size_t *out_len) {
    bitreader_t br;
    br_init(&br, in, in_len);

    size_t out_pos = 0;
    int final_block = 0;

    while (!final_block) {
        uint32_t bfinal, btype;
        if (br_get_bits(&br, 1, &bfinal) < 0) return DEFLATE_ERR_TRUNCATED;
        if (br_get_bits(&br, 2, &btype) < 0) return DEFLATE_ERR_TRUNCATED;
        final_block = (int)bfinal;

        if (btype == 0) {
            /* Stored (uncompressed) block - byte-aligned LEN/NLEN
             * header followed by LEN raw bytes (RFC 1951 3.2.4). */
            br_align_to_byte(&br);
            if (br.byte_pos + 4 > br.len) return DEFLATE_ERR_TRUNCATED;

            uint16_t len  = (uint16_t)(br.data[br.byte_pos] | (br.data[br.byte_pos+1] << 8));
            uint16_t nlen = (uint16_t)(br.data[br.byte_pos+2] | (br.data[br.byte_pos+3] << 8));
            br.byte_pos += 4;

            if ((uint16_t)(~len & 0xFFFFu) != nlen) return DEFLATE_ERR_BAD_LENGTH;
            if (br.byte_pos + len > br.len) return DEFLATE_ERR_TRUNCATED;
            if (out_pos + len > out_capacity) return DEFLATE_ERR_OUTPUT_FULL;

            for (uint16_t k = 0; k < len; k++) {
                out[out_pos++] = br.data[br.byte_pos++];
            }
            continue;
        }

        if (btype == 3) return DEFLATE_ERR_BAD_BLOCK_TYPE; /* reserved, always invalid */

        huffman_table_t lit_table, dist_table;
        if (btype == 1) {
            build_fixed_tables();
            lit_table = g_fixed_lit_table;
            dist_table = g_fixed_dist_table;
        } else { /* btype == 2: dynamic Huffman */
            if (read_dynamic_tables(&br, &lit_table, &dist_table) < 0) {
                return DEFLATE_ERR_BAD_HUFFMAN;
            }
        }

        for (;;) {
            int sym = huffman_decode(&br, &lit_table);
            if (sym < 0) return DEFLATE_ERR_BAD_HUFFMAN;

            if (sym < 256) {
                /* literal byte */
                if (out_pos >= out_capacity) return DEFLATE_ERR_OUTPUT_FULL;
                out[out_pos++] = (uint8_t)sym;
            } else if (sym == 256) {
                break; /* end-of-block */
            } else {
                /* length/distance back-reference */
                int len_idx = sym - 257;
                if (len_idx >= 29) return DEFLATE_ERR_BAD_HUFFMAN; /* symbols 286/287 are invalid per spec */

                uint32_t extra;
                if (br_get_bits(&br, length_extra_bits[len_idx], &extra) < 0) {
                    return DEFLATE_ERR_TRUNCATED;
                }
                uint32_t match_len = length_base[len_idx] + extra;

                int dist_sym = huffman_decode(&br, &dist_table);
                if (dist_sym < 0 || dist_sym >= 30) return DEFLATE_ERR_BAD_HUFFMAN;

                if (br_get_bits(&br, dist_extra_bits[dist_sym], &extra) < 0) {
                    return DEFLATE_ERR_TRUNCATED;
                }
                uint32_t distance = dist_base[dist_sym] + extra;

                if (distance > out_pos) return DEFLATE_ERR_BAD_DISTANCE;
                if (out_pos + match_len > out_capacity) return DEFLATE_ERR_OUTPUT_FULL;

                /* Byte-by-byte copy (not memcpy) is required here,
                 * not a style choice: DEFLATE back-references can
                 * have distance < length (e.g. a run of one repeated
                 * byte encoded as distance=1, length=200), meaning
                 * the source and destination ranges legitimately
                 * overlap and later bytes must see earlier bytes
                 * *within this same copy* already written. memcpy's
                 * overlap behavior is undefined for exactly this
                 * case; a forward byte-by-byte loop is the correct,
                 * well-defined way to implement this. */
                size_t src = out_pos - distance;
                for (uint32_t k = 0; k < match_len; k++) {
                    out[out_pos + k] = out[src + k];
                }
                out_pos += match_len;
            }
        }
    }

    *out_len = out_pos;
    return DEFLATE_OK;
}

/* ── zlib wrapper (RFC 1950) ──────────────────────────────────────── */

static uint32_t adler32(const uint8_t *data, size_t len) {
    /* RFC 1950 section 8/9: Adler-32 checksum. Modulo base 65521 (the
     * largest prime smaller than 2^16), computed incrementally rather
     * than via a single pass with a huge accumulator, matching the
     * spec's reference algorithm. */
    uint32_t a = 1, b = 0;
    const uint32_t MOD_ADLER = 65521u;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % MOD_ADLER;
        b = (b + a) % MOD_ADLER;
    }
    return (b << 16) | a;
}

deflate_result_t zlib_inflate(const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t out_capacity,
                              size_t *out_len) {
    /* zlib header is 2 bytes: CMF (compression method/flags) and FLG
     * (flags, including a check bits field such that (CMF*256+FLG)
     * is a multiple of 31 - RFC 1950 section 2.2). PNG's IDAT stream
     * is always zlib-wrapped DEFLATE with no preset dictionary, so
     * FDICT (bit 5 of FLG) should be 0 - a set FDICT bit is rejected
     * since this decoder has no mechanism to supply a preset
     * dictionary. */
    if (in_len < 6) return DEFLATE_ERR_TRUNCATED; /* 2-byte header + >=4-byte Adler32 trailer minimum */

    uint8_t cmf = in[0];
    uint8_t flg = in[1];

    if ((cmf & 0x0F) != 8) return DEFLATE_ERR_BAD_BLOCK_TYPE; /* CM must be 8 (DEFLATE) per spec */
    if (((uint32_t)cmf * 256 + flg) % 31 != 0) return DEFLATE_ERR_BAD_BLOCK_TYPE; /* header checksum */
    if (flg & 0x20) return DEFLATE_ERR_BAD_BLOCK_TYPE; /* FDICT set - unsupported, no preset dict mechanism */

    const uint8_t *deflate_data = in + 2;
    size_t deflate_len = in_len - 2 - 4; /* strip 2-byte header and trailing 4-byte Adler32 */

    deflate_result_t r = deflate_decompress(deflate_data, deflate_len, out, out_capacity, out_len);
    if (r != DEFLATE_OK) return r;

    uint32_t expected_adler =
        ((uint32_t)in[in_len - 4] << 24) |
        ((uint32_t)in[in_len - 3] << 16) |
        ((uint32_t)in[in_len - 2] << 8)  |
        ((uint32_t)in[in_len - 1]);

    uint32_t actual_adler = adler32(out, *out_len);
    if (actual_adler != expected_adler) return DEFLATE_ERR_BAD_LENGTH; /* checksum mismatch = corrupted data */

    return DEFLATE_OK;
}
