/* png.h - PNG image decoder, built on deflate.h.
 *
 * Scope (per project decision): 8-bit-per-channel RGB and RGBA PNGs
 * (color types 2 and 6, bit depth 8) - covers real-world cursor/icon
 * assets. Palette (type 3), grayscale (types 0/4), 16-bit depth, and
 * interlaced (Adam7) images are detected and explicitly rejected
 * rather than silently mis-decoded - see png_result_t.
 */
#ifndef PNG_H
#define PNG_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    PNG_OK = 0,
    PNG_ERR_BAD_SIGNATURE,     /* not a PNG file at all */
    PNG_ERR_BAD_CHUNK,         /* malformed chunk structure or CRC mismatch */
    PNG_ERR_UNSUPPORTED_COLOR, /* color type/bit depth outside the 8-bit RGB/RGBA scope */
    PNG_ERR_UNSUPPORTED_INTERLACE, /* Adam7 interlacing - not supported */
    PNG_ERR_DECOMPRESS_FAILED, /* the DEFLATE/zlib layer rejected the IDAT stream */
    PNG_ERR_BAD_FILTER,        /* scanline filter byte outside 0-4 */
    PNG_ERR_OUT_OF_MEMORY,     /* kmalloc failed for pixel buffer or scratch space */
    PNG_ERR_TRUNCATED,         /* file ended before all expected chunks/data arrived */
} png_result_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t  channels;   /* 3 = RGB, 4 = RGBA */
    /* pixels: width*height*channels bytes, row-major, top-to-bottom,
     * each pixel's channels in R,G,B[,A] order - i.e. already fully
     * unfiltered, de-interlaced (moot, since interlaced input is
     * rejected), and ready to hand to something like
     * gfx2d_surface_t without further processing. Allocated via
     * kmalloc() by png_decode() - caller must png_free() it. */
    uint8_t *pixels;
} png_image_t;

/* Decodes a complete in-memory PNG file (e.g. read in full via
 * fs_read() before calling this - this decoder does not stream from
 * a file descriptor itself). On PNG_OK, `out` is fully populated and
 * out->pixels must later be released with png_free(). On any error,
 * out->pixels is guaranteed NULL (nothing for the caller to
 * accidentally free or dereference). */
png_result_t png_decode(const uint8_t *data, size_t len, png_image_t *out);

void png_free(png_image_t *img);

#endif /* PNG_H */
