#include "image_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t rd_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static uint16_t rd_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int load_jpeg(const uint8_t *buf, size_t len, image_t *out);

static uint32_t argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static int load_png(const uint8_t *buf, size_t len, image_t *out) {
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (len < 8 || memcmp(buf, sig, 8) != 0) return 0;

    size_t pos = 8;
    int32_t width = 0, height = 0;
    int bit_depth = 0, color_type = 0;
    uint8_t *idat = NULL;
    size_t idat_len = 0;

    while (pos + 8 <= len) {
        uint32_t chunk_len = rd_be32(buf + pos);
        const uint8_t *type = buf + pos + 4;
        const uint8_t *data = buf + pos + 8;
        if (pos + 8 + chunk_len + 4 > len) break;

        if (memcmp(type, "IHDR", 4) == 0) {
            width = (int32_t)rd_be32(data);
            height = (int32_t)rd_be32(data + 4);
            bit_depth = data[8];
            color_type = data[9];
        } else if (memcmp(type, "IDAT", 4) == 0) {
            idat = (uint8_t *)realloc(idat, idat_len + chunk_len);
            memcpy(idat + idat_len, data, chunk_len);
            idat_len += chunk_len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }

        pos += 8 + chunk_len + 4;
    }

    if (width <= 0 || height <= 0 || !idat) {
        free(idat);
        return 0;
    }
    if (bit_depth != 8 || (color_type != 2 && color_type != 6)) {
        /* only 8-bit RGB / RGBA supported for now */
        free(idat);
        return 0;
    }

    int channels = (color_type == 6) ? 4 : 3;
    size_t stride = (size_t)width * channels;
    size_t raw_len = (size_t)height * (stride + 1);

    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw) {
        free(idat);
        return 0;
    }

    uLongf dest_len = (uLongf)raw_len;
    int zret = uncompress(raw, &dest_len, idat, (uLong)idat_len);
    free(idat);
    if (zret != Z_OK || dest_len != raw_len) {
        free(raw);
        return 0;
    }

    uint8_t *pixels_bytes = (uint8_t *)malloc(stride * height);
    if (!pixels_bytes) {
        free(raw);
        return 0;
    }

    for (int32_t y = 0; y < height; y++) {
        uint8_t filter = raw[y * (stride + 1)];
        const uint8_t *src = raw + y * (stride + 1) + 1;
        uint8_t *dst = pixels_bytes + (size_t)y * stride;
        const uint8_t *prev = (y > 0) ? pixels_bytes + (size_t)(y - 1) * stride : NULL;

        for (size_t x = 0; x < stride; x++) {
            uint8_t a = (x >= (size_t)channels) ? dst[x - channels] : 0;
            uint8_t b = prev ? prev[x] : 0;
            uint8_t c = (prev && x >= (size_t)channels) ? prev[x - channels] : 0;
            uint8_t raw_val = src[x];

            switch (filter) {
                case 0: dst[x] = raw_val; break;
                case 1: dst[x] = raw_val + a; break;
                case 2: dst[x] = raw_val + b; break;
                case 3: dst[x] = raw_val + (uint8_t)((a + b) / 2); break;
                case 4: dst[x] = raw_val + paeth(a, b, c); break;
                default:
                    free(raw);
                    free(pixels_bytes);
                    return 0;
            }
        }
    }
    free(raw);

    uint32_t *pixels = (uint32_t *)malloc((size_t)width * height * sizeof(uint32_t));
    if (!pixels) {
        free(pixels_bytes);
        return 0;
    }

    for (int32_t y = 0; y < height; y++) {
        for (int32_t x = 0; x < width; x++) {
            const uint8_t *p = pixels_bytes + (size_t)y * stride + (size_t)x * channels;
            uint8_t r = p[0], g = p[1], b = p[2];
            uint8_t a = (channels == 4) ? p[3] : 255;
            pixels[y * width + x] = argb(a, r, g, b);
        }
    }
    free(pixels_bytes);

    out->pixels = pixels;
    out->width = width;
    out->height = height;
    return 1;
}

static int load_bmp(const uint8_t *buf, size_t len, image_t *out) {
    if (len < 54 || buf[0] != 'B' || buf[1] != 'M') return 0;

    uint32_t data_offset = *(uint32_t *)(buf + 10);
    int32_t width = *(int32_t *)(buf + 18);
    int32_t height_raw = *(int32_t *)(buf + 22);
    uint16_t bpp = *(uint16_t *)(buf + 28);

    if (bpp != 24 && bpp != 32) return 0;

    int flip = height_raw > 0;
    int32_t height = flip ? height_raw : -height_raw;
    int bytes_per_px = bpp / 8;
    size_t row_size = ((size_t)width * bytes_per_px + 3) & ~((size_t)3);

    if (data_offset + row_size * height > len) return 0;

    uint32_t *pixels = (uint32_t *)malloc((size_t)width * height * sizeof(uint32_t));
    if (!pixels) return 0;

    for (int32_t y = 0; y < height; y++) {
        int32_t src_row = flip ? (height - 1 - y) : y;
        const uint8_t *row = buf + data_offset + (size_t)src_row * row_size;
        for (int32_t x = 0; x < width; x++) {
            const uint8_t *p = row + (size_t)x * bytes_per_px;
            uint8_t b = p[0], g = p[1], r = p[2];
            uint8_t a = (bytes_per_px == 4) ? p[3] : 255;
            pixels[y * width + x] = argb(a, r, g, b);
        }
    }

    out->pixels = pixels;
    out->width = width;
    out->height = height;
    return 1;
}

/* ICO: a directory of embedded images (largest one wins here). Each entry
 * is either a full PNG (modern large icons/cursors) or a legacy
 * BITMAPINFOHEADER + XOR color data + AND transparency mask, stacked
 * bottom-up. We only support 24/32bpp uncompressed entries -- same
 * "reject what we don't handle yet" policy as the BMP/PNG paths above. */
static int load_ico(const uint8_t *buf, size_t len, image_t *out) {
    if (len < 6 || buf[0] != 0 || buf[1] != 0) return 0;
    uint16_t type = rd_le16(buf + 2);
    if (type != 1 && type != 2) return 0; /* 1 = icon, 2 = cursor */
    uint16_t count = rd_le16(buf + 4);
    if (count == 0) return 0;

    int best_idx = -1;
    int best_area = -1;
    for (uint16_t i = 0; i < count; i++) {
        size_t entry_off = 6 + (size_t)i * 16;
        if (entry_off + 16 > len) break;
        int w = buf[entry_off]; if (w == 0) w = 256;
        int h = buf[entry_off + 1]; if (h == 0) h = 256;
        int area = w * h;
        if (area > best_area) { best_area = area; best_idx = i; }
    }
    if (best_idx < 0) return 0;

    size_t entry_off = 6 + (size_t)best_idx * 16;
    uint32_t data_size = rd_le32(buf + entry_off + 8);
    uint32_t data_off  = rd_le32(buf + entry_off + 12);
    if ((size_t)data_off + data_size > len || data_size < 8) return 0;

    const uint8_t *img = buf + data_off;

    static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (data_size >= 8 && memcmp(img, png_sig, 8) == 0) {
        return load_png(img, data_size, out);
    }

    if (data_size < 40) return 0;
    uint32_t hdr_size = rd_le32(img);
    if (hdr_size < 40 || data_size < hdr_size) return 0;

    int32_t bmp_w = (int32_t)rd_le32(img + 4);
    int32_t bmp_h_raw = (int32_t)rd_le32(img + 8);
    uint16_t bpp = rd_le16(img + 14);
    uint32_t compression = rd_le32(img + 16);

    if (compression != 0 || (bpp != 24 && bpp != 32)) return 0;

    int32_t height = bmp_h_raw / 2; /* stored height covers XOR + AND stacked */
    int32_t width = bmp_w;
    if (width <= 0 || height <= 0) return 0;

    int bytes_per_px = bpp / 8;
    size_t xor_row = (((size_t)width * bytes_per_px + 3) / 4) * 4;
    size_t xor_size = xor_row * height;
    size_t and_row = (((size_t)width + 31) / 32) * 4;
    size_t and_size = and_row * height;

    if ((size_t)hdr_size + xor_size + and_size > data_size) return 0;

    const uint8_t *xor_data = img + hdr_size;
    const uint8_t *and_data = xor_data + xor_size;

    uint32_t *pixels = (uint32_t *)malloc((size_t)width * height * sizeof(uint32_t));
    if (!pixels) return 0;

    for (int32_t y = 0; y < height; y++) {
        int32_t src_row = height - 1 - y; /* bottom-up like BMP */
        const uint8_t *xrow = xor_data + (size_t)src_row * xor_row;
        const uint8_t *arow = and_data + (size_t)src_row * and_row;
        for (int32_t x = 0; x < width; x++) {
            const uint8_t *p = xrow + (size_t)x * bytes_per_px;
            uint8_t b = p[0], g = p[1], r = p[2];
            uint8_t a;
            if (bytes_per_px == 4) {
                a = p[3];
            } else {
                int bit = arow[x / 8] & (0x80 >> (x % 8));
                a = bit ? 0 : 255; /* AND mask: 1 = transparent pixel */
            }
            pixels[y * width + x] = argb(a, r, g, b);
        }
    }

    out->pixels = pixels;
    out->width = width;
    out->height = height;
    return 1;
}

/* ============================================================================
 * JPEG -- baseline (sequential DCT) decoder only. Progressive (SOF2) is
 * rejected outright; same "narrow it to what's actually needed, reject
 * the rest cleanly" policy as everywhere else in this file. Supports
 * grayscale and YCbCr, any H/V subsampling (4:4:4, 4:2:2, 4:2:0), and
 * restart markers (DRI/RSTn), since real-world JPEGs commonly use both.
 * ============================================================================ */

#include <math.h>

static const int JPEG_ZIGZAG[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static uint16_t rd_be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

typedef struct {
    uint8_t bits[17];
    uint8_t huffval[256];
    int nvals;
    int mincode[17];
    int maxcode[17];
    int valptr[17];
} huff_table_t;

static void huff_build(huff_table_t *t) {
    int huffsize[257], huffcode[257];
    int k = 0;
    for (int l = 1; l <= 16; l++) {
        for (int i = 0; i < t->bits[l]; i++) huffsize[k++] = l;
    }
    huffsize[k] = 0;

    int code = 0, si = (k > 0) ? huffsize[0] : 0;
    k = 0;
    while (huffsize[k]) {
        while (huffsize[k] == si) { huffcode[k] = code; code++; k++; }
        code <<= 1;
        si++;
    }

    for (int l = 0; l <= 16; l++) { t->mincode[l] = 0; t->maxcode[l] = -1; t->valptr[l] = 0; }
    k = 0;
    for (int l = 1; l <= 16; l++) {
        if (t->bits[l] == 0) continue;
        t->valptr[l] = k;
        t->mincode[l] = huffcode[k];
        k += t->bits[l];
        t->maxcode[l] = huffcode[k - 1];
    }
}

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    uint32_t bitbuf;
    int bitcnt;
    int eof;
} jbits_t;

static int jbits_get(jbits_t *br) {
    if (br->bitcnt == 0) {
        if (br->pos >= br->len) { br->eof = 1; return 0; }
        uint8_t b = br->data[br->pos];
        if (b == 0xFF) {
            if (br->pos + 1 < br->len && br->data[br->pos + 1] == 0x00) {
                br->pos += 2;
            } else {
                br->eof = 1; /* real marker (RST/EOI/...) -- stop here */
                return 0;
            }
        } else {
            br->pos += 1;
        }
        br->bitbuf = b;
        br->bitcnt = 8;
    }
    br->bitcnt--;
    return (br->bitbuf >> br->bitcnt) & 1;
}

static void jbits_restart(jbits_t *br) {
    br->bitcnt = 0;
    br->eof = 0;
    if (br->pos + 1 < br->len && br->data[br->pos] == 0xFF &&
        br->data[br->pos + 1] >= 0xD0 && br->data[br->pos + 1] <= 0xD7) {
        br->pos += 2;
    }
}

static int huff_decode(jbits_t *br, const huff_table_t *t) {
    int code = jbits_get(br);
    int len = 1;
    while (len <= 16) {
        if (t->maxcode[len] != -1 && code <= t->maxcode[len]) break;
        code = (code << 1) | jbits_get(br);
        len++;
    }
    if (len > 16) return -1;
    int idx = t->valptr[len] + (code - t->mincode[len]);
    if (idx < 0 || idx >= t->nvals) return -1;
    return t->huffval[idx];
}

static int jpeg_receive_extend(jbits_t *br, int s) {
    if (s == 0) return 0;
    int v = 0;
    for (int i = 0; i < s; i++) v = (v << 1) | jbits_get(br);
    if (v < (1 << (s - 1))) v = v - (1 << s) + 1;
    return v;
}

static void idct_8x8(const int16_t block[64], uint8_t out[64]) {
    static float cos_table[8][8];
    static int init = 0;
    if (!init) {
        for (int x = 0; x < 8; x++)
            for (int u = 0; u < 8; u++)
                cos_table[x][u] = cosf((float)((2 * x + 1) * u) * 3.14159265f / 16.0f);
        init = 1;
    }
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            float sum = 0.0f;
            for (int v = 0; v < 8; v++) {
                float cv = (v == 0) ? 0.70710678f : 1.0f;
                for (int u = 0; u < 8; u++) {
                    float cu = (u == 0) ? 0.70710678f : 1.0f;
                    sum += cu * cv * (float)block[v * 8 + u] * cos_table[x][u] * cos_table[y][v];
                }
            }
            sum *= 0.25f;
            int val = (int)(sum + 128.5f);
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            out[y * 8 + x] = (uint8_t)val;
        }
    }
}

typedef struct {
    int id, h, v, tq, td, ta;
    int dc_pred;
    uint8_t *plane;
    int plane_w, plane_h;
} jpeg_comp_t;

static int load_jpeg(const uint8_t *buf, size_t len, image_t *out) {
    if (len < 4 || buf[0] != 0xFF || buf[1] != 0xD8) return 0;
    size_t pos = 2;

    uint16_t qtab[4][64];
    memset(qtab, 0, sizeof(qtab));
    huff_table_t dc_tabs[4], ac_tabs[4];
    memset(dc_tabs, 0, sizeof(dc_tabs));
    memset(ac_tabs, 0, sizeof(ac_tabs));

    int width = 0, height = 0, ncomp = 0;
    jpeg_comp_t comps[4];
    memset(comps, 0, sizeof(comps));
    int restart_interval = 0;
    int have_sof = 0;

    while (pos + 2 <= len) {
        if (buf[pos] != 0xFF) { pos++; continue; }
        uint8_t marker = buf[pos + 1];
        pos += 2;

        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (marker == 0xD9) break; /* EOI */
        if (pos + 2 > len) break;

        uint16_t seg_len = rd_be16(buf + pos);
        if (seg_len < 2 || pos + seg_len > len) break;
        const uint8_t *seg = buf + pos + 2;
        size_t seg_data_len = seg_len - 2;

        if (marker == 0xDB) { /* DQT */
            size_t p = 0;
            while (p < seg_data_len) {
                uint8_t pq_tq = seg[p++];
                int pq = pq_tq >> 4, tq = pq_tq & 0xF;
                if (tq > 3) return 0;
                if (pq == 0) {
                    if (p + 64 > seg_data_len) return 0;
                    for (int i = 0; i < 64; i++) qtab[tq][i] = seg[p + i];
                    p += 64;
                } else {
                    if (p + 128 > seg_data_len) return 0;
                    for (int i = 0; i < 64; i++) qtab[tq][i] = rd_be16(seg + p + i * 2);
                    p += 128;
                }
            }
        } else if (marker == 0xC0) { /* SOF0: baseline only */
            if (seg_data_len < 6 || seg[0] != 8) return 0;
            height = rd_be16(seg + 1);
            width = rd_be16(seg + 3);
            ncomp = seg[5];
            if (ncomp != 1 && ncomp != 3) return 0;
            if (6 + (size_t)ncomp * 3 > seg_data_len) return 0;
            for (int i = 0; i < ncomp; i++) {
                const uint8_t *cd = seg + 6 + i * 3;
                comps[i].id = cd[0];
                comps[i].h = cd[1] >> 4;
                comps[i].v = cd[1] & 0xF;
                comps[i].tq = cd[2];
            }
            have_sof = 1;
        } else if (marker == 0xC2) {
            return 0; /* progressive JPEG not supported */
        } else if (marker == 0xC4) { /* DHT */
            size_t p = 0;
            while (p < seg_data_len) {
                uint8_t tc_th = seg[p++];
                int tc = tc_th >> 4, th = tc_th & 0xF;
                if (th > 3 || p + 16 > seg_data_len) return 0;
                huff_table_t *t = (tc == 0) ? &dc_tabs[th] : &ac_tabs[th];
                int total = 0;
                for (int l = 1; l <= 16; l++) { t->bits[l] = seg[p + l - 1]; total += t->bits[l]; }
                p += 16;
                if (p + (size_t)total > seg_data_len) return 0;
                for (int i = 0; i < total; i++) t->huffval[i] = seg[p + i];
                t->nvals = total;
                p += total;
                huff_build(t);
            }
        } else if (marker == 0xDD) { /* DRI */
            if (seg_data_len < 2) return 0;
            restart_interval = rd_be16(seg);
        } else if (marker == 0xDA) { /* SOS -- decode scan right here */
            if (!have_sof || seg_data_len < 1) return 0;
            int ns = seg[0];
            if (ns != ncomp || 1 + (size_t)ns * 2 > seg_data_len) return 0;
            for (int i = 0; i < ns; i++) {
                int cs = seg[1 + i * 2], tdta = seg[2 + i * 2];
                for (int c = 0; c < ncomp; c++) {
                    if (comps[c].id == cs) { comps[c].td = tdta >> 4; comps[c].ta = tdta & 0xF; }
                }
            }

            int hmax = 0, vmax = 0;
            for (int c = 0; c < ncomp; c++) {
                if (comps[c].h > hmax) hmax = comps[c].h;
                if (comps[c].v > vmax) vmax = comps[c].v;
            }
            if (hmax == 0 || vmax == 0) return 0;

            int mcu_w = 8 * hmax, mcu_h = 8 * vmax;
            int mcus_x = (width + mcu_w - 1) / mcu_w;
            int mcus_y = (height + mcu_h - 1) / mcu_h;

            int alloc_failed = 0;
            for (int c = 0; c < ncomp; c++) {
                comps[c].plane_w = mcus_x * comps[c].h * 8;
                comps[c].plane_h = mcus_y * comps[c].v * 8;
                comps[c].plane = (uint8_t *)malloc((size_t)comps[c].plane_w * comps[c].plane_h);
                if (!comps[c].plane) alloc_failed = 1;
            }
            if (alloc_failed) {
                for (int c = 0; c < ncomp; c++) free(comps[c].plane);
                return 0;
            }

            jbits_t br = { buf, len, pos + seg_len, 0, 0, 0 };
            int mcus_since_restart = 0;
            int total_mcus = mcus_x * mcus_y;

            for (int mcu_i = 0; mcu_i < total_mcus; mcu_i++) {
                int mcu_col = mcu_i % mcus_x;
                int mcu_row = mcu_i / mcus_x;

                for (int c = 0; c < ncomp; c++) {
                    for (int by = 0; by < comps[c].v; by++) {
                        for (int bx = 0; bx < comps[c].h; bx++) {
                            int16_t zz[64];
                            memset(zz, 0, sizeof(zz));

                            int s = huff_decode(&br, &dc_tabs[comps[c].td]);
                            if (s < 0) s = 0;
                            int diff = jpeg_receive_extend(&br, s);
                            comps[c].dc_pred += diff;
                            zz[0] = (int16_t)comps[c].dc_pred;

                            int k = 1;
                            while (k < 64) {
                                int rs = huff_decode(&br, &ac_tabs[comps[c].ta]);
                                if (rs < 0) break;
                                int r = rs >> 4, sz = rs & 0xF;
                                if (sz == 0) {
                                    if (r == 15) { k += 16; continue; }
                                    break; /* EOB */
                                }
                                k += r;
                                if (k >= 64) break;
                                zz[k] = (int16_t)jpeg_receive_extend(&br, sz);
                                k++;
                            }

                            int16_t coeff[64];
                            memset(coeff, 0, sizeof(coeff));
                            const uint16_t *q = qtab[comps[c].tq];
                            for (int i = 0; i < 64; i++) {
                                coeff[JPEG_ZIGZAG[i]] = (int16_t)(zz[i] * q[i]);
                            }

                            uint8_t samples[64];
                            idct_8x8(coeff, samples);

                            int px0 = (mcu_col * comps[c].h + bx) * 8;
                            int py0 = (mcu_row * comps[c].v + by) * 8;
                            for (int yy = 0; yy < 8; yy++) {
                                memcpy(comps[c].plane + (size_t)(py0 + yy) * comps[c].plane_w + px0,
                                       samples + yy * 8, 8);
                            }
                        }
                    }
                }

                mcus_since_restart++;
                if (restart_interval > 0 && mcus_since_restart == restart_interval &&
                    mcu_i != total_mcus - 1) {
                    jbits_restart(&br);
                    for (int c = 0; c < ncomp; c++) comps[c].dc_pred = 0;
                    mcus_since_restart = 0;
                }
            }

            uint32_t *pixels = (uint32_t *)malloc((size_t)width * height * sizeof(uint32_t));
            if (!pixels) {
                for (int c = 0; c < ncomp; c++) free(comps[c].plane);
                return 0;
            }

            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    if (ncomp == 1) {
                        int sx = x * comps[0].h / hmax, sy = y * comps[0].v / vmax;
                        uint8_t yv = comps[0].plane[sy * comps[0].plane_w + sx];
                        pixels[y * width + x] = argb(255, yv, yv, yv);
                    } else {
                        int sx0 = x * comps[0].h / hmax, sy0 = y * comps[0].v / vmax;
                        int sx1 = x * comps[1].h / hmax, sy1 = y * comps[1].v / vmax;
                        int sx2 = x * comps[2].h / hmax, sy2 = y * comps[2].v / vmax;
                        int Yv = comps[0].plane[sy0 * comps[0].plane_w + sx0];
                        int Cb = comps[1].plane[sy1 * comps[1].plane_w + sx1] - 128;
                        int Cr = comps[2].plane[sy2 * comps[2].plane_w + sx2] - 128;
                        int r = Yv + (int)(1.402f * (float)Cr);
                        int g = Yv - (int)(0.344136f * (float)Cb) - (int)(0.714136f * (float)Cr);
                        int b = Yv + (int)(1.772f * (float)Cb);
                        if (r < 0) r = 0;
                        if (r > 255) r = 255;
                        if (g < 0) g = 0;
                        if (g > 255) g = 255;
                        if (b < 0) b = 0;
                        if (b > 255) b = 255;
                        pixels[y * width + x] = argb(255, (uint8_t)r, (uint8_t)g, (uint8_t)b);
                    }
                }
            }

            for (int c = 0; c < ncomp; c++) free(comps[c].plane);

            out->pixels = pixels;
            out->width = width;
            out->height = height;
            return 1;
        }

        pos += seg_len;
    }

    return 0;
}

int image_load(const char *path, image_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return 0;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return 0;
    }
    size_t read = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (read != (size_t)sz) {
        free(buf);
        return 0;
    }

    int ok = 0;
    if (sz >= 8 && buf[0] == 137 && buf[1] == 'P') {
        ok = load_png(buf, (size_t)sz, out);
    } else if (sz >= 2 && buf[0] == 'B' && buf[1] == 'M') {
        ok = load_bmp(buf, (size_t)sz, out);
    } else if (sz >= 6 && buf[0] == 0 && buf[1] == 0 && (buf[2] == 1 || buf[2] == 2) && buf[3] == 0) {
        ok = load_ico(buf, (size_t)sz, out);
    } else if (sz >= 2 && buf[0] == 0xFF && buf[1] == 0xD8) {
        ok = load_jpeg(buf, (size_t)sz, out);
    }

    free(buf);
    return ok;
}

void image_free(image_t *img) {
    if (img && img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
}

int image_downscale(const image_t *src, int32_t target_w, int32_t target_h, image_t *out) {
    if (!src || !src->pixels || target_w <= 0 || target_h <= 0) return 0;

    uint32_t *pixels = (uint32_t *)malloc((size_t)target_w * target_h * sizeof(uint32_t));
    if (!pixels) return 0;

    for (int32_t y = 0; y < target_h; y++) {
        int32_t sy = (y * src->height) / target_h;
        for (int32_t x = 0; x < target_w; x++) {
            int32_t sx = (x * src->width) / target_w;
            pixels[y * target_w + x] = src->pixels[sy * src->width + sx];
        }
    }

    out->pixels = pixels;
    out->width = target_w;
    out->height = target_h;
    return 1;
}

