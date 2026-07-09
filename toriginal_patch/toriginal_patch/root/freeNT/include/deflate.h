/* deflate.h - RFC 1951 DEFLATE decompressor.
 *
 * A from-scratch DEFLATE decoder: Huffman decoding (both fixed and
 * dynamic trees per RFC 1951 sections 3.2.5/3.2.6), the LZ77
 * length/distance back-reference window, and all three DEFLATE block
 * types (stored/fixed-Huffman/dynamic-Huffman).
 *
 * This is generic - not PNG-specific - because DEFLATE is used by
 * more than PNG (zlib streams, gzip, zip) and keeping the
 * decompressor itself free of PNG concerns means png.c only has to
 * handle the zlib wrapper (2-byte header + Adler-32) and PNG's own
 * filter reconstruction on top of this.
 */
#ifndef DEFLATE_H
#define DEFLATE_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    DEFLATE_OK = 0,
    DEFLATE_ERR_TRUNCATED,      /* ran out of input before a block finished */
    DEFLATE_ERR_BAD_BLOCK_TYPE, /* block type bits were the reserved value 3 */
    DEFLATE_ERR_BAD_HUFFMAN,    /* malformed Huffman code table */
    DEFLATE_ERR_BAD_LENGTH,     /* stored-block LEN/~LEN mismatch */
    DEFLATE_ERR_BAD_DISTANCE,   /* back-reference distance > bytes produced so far */
    DEFLATE_ERR_OUTPUT_FULL,    /* output buffer too small for the decompressed data */
} deflate_result_t;

/* Decompresses a raw DEFLATE stream (NOT zlib-wrapped - no 2-byte
 * header, no Adler-32 trailer; see zlib_inflate() below for that).
 * `out` must be pre-allocated by the caller to at least the
 * decompressed size - this decoder does not allocate or grow the
 * output buffer itself, since the kernel context calling this may
 * want tight control over where that memory comes from. On success,
 * *out_len is set to the actual number of bytes written. */
deflate_result_t deflate_decompress(const uint8_t *in, size_t in_len,
                                    uint8_t *out, size_t out_capacity,
                                    size_t *out_len);

/* Decompresses a zlib-wrapped DEFLATE stream (RFC 1950): validates
 * the 2-byte zlib header, runs deflate_decompress() on the payload,
 * and verifies the trailing Adler-32 checksum against the actual
 * decompressed output. Returns DEFLATE_OK only if decompression
 * succeeded AND the checksum matches - a corrupted PNG whose DEFLATE
 * stream happens to decode "successfully" but doesn't match its own
 * checksum is still reported as an error, not silently accepted. */
deflate_result_t zlib_inflate(const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t out_capacity,
                              size_t *out_len);

#endif /* DEFLATE_H */
