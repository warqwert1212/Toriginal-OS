/* imgdec.c */
#include "imgdec.h"
#include "png.h"
#include "heap.h"
#include "string.h"

#define IMG_MAX_OUT_W 1920u
#define IMG_MAX_OUT_H 1200u
#define IMG_MAX_BYTES (24u * 1024u * 1024u)

static const uint8_t g_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

#define F_0_5411961    2217
#define F_M1_847759065 -7568
#define F_0_765366865  3135
#define F_1_175875602  4816
#define F_0_298631336  1223
#define F_2_053119869  8410
#define F_3_072711026  12586
#define F_1_501321110  6149
#define F_M0_899976223 -3686
#define F_M2_562915447 -10498
#define F_M1_961570560 -8035
#define F_M0_390180644 -1598

#define FSH(x) ((x) * 4096)

#define IDCT_1D(s0, s1, s2, s3, s4, s5, s6, s7)                          \
    int t0, t1, t2, t3, p1, p2, p3, p4, p5, x0, x1, x2, x3;              \
    p2 = (s2); p3 = (s6);                                                \
    p1 = (p2 + p3) * F_0_5411961;                                        \
    t2 = p1 + p3 * F_M1_847759065;                                       \
    t3 = p1 + p2 * F_0_765366865;                                        \
    p2 = (s0); p3 = (s4);                                                \
    t0 = FSH(p2 + p3);                                                   \
    t1 = FSH(p2 - p3);                                                   \
    x0 = t0 + t3; x3 = t0 - t3; x1 = t1 + t2; x2 = t1 - t2;              \
    t0 = (s7); t1 = (s5); t2 = (s3); t3 = (s1);                          \
    p3 = t0 + t2; p4 = t1 + t3; p1 = t0 + t3; p2 = t1 + t2;              \
    p5 = (p3 + p4) * F_1_175875602;                                      \
    t0 = t0 * F_0_298631336;                                             \
    t1 = t1 * F_2_053119869;                                             \
    t2 = t2 * F_3_072711026;                                             \
    t3 = t3 * F_1_501321110;                                             \
    p1 = p5 + p1 * F_M0_899976223;                                       \
    p2 = p5 + p2 * F_M2_562915447;                                       \
    p3 = p3 * F_M1_961570560;                                            \
    p4 = p4 * F_M0_390180644;                                            \
    t3 += p1 + p4; t2 += p2 + p3; t1 += p2 + p4; t0 += p1 + p3;

static inline uint8_t clamp8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void idct_block(uint8_t *out, int stride, const int *data) {
    int val[64];
    int *v = val;
    const int *d = data;

    for (int i = 0; i < 8; i++, d++, v++) {
        if (d[8] == 0 && d[16] == 0 && d[24] == 0 && d[32] == 0 &&
            d[40] == 0 && d[48] == 0 && d[56] == 0) {
            int dc = d[0] * 4;
            v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = dc;
        } else {
            IDCT_1D(d[0], d[8], d[16], d[24], d[32], d[40], d[48], d[56])
            x0 += 512; x1 += 512; x2 += 512; x3 += 512;
            v[0]  = (x0 + t3) >> 10;
            v[56] = (x0 - t3) >> 10;
            v[8]  = (x1 + t2) >> 10;
            v[48] = (x1 - t2) >> 10;
            v[16] = (x2 + t1) >> 10;
            v[40] = (x2 - t1) >> 10;
            v[24] = (x3 + t0) >> 10;
            v[32] = (x3 - t0) >> 10;
        }
    }

    v = val;
    uint8_t *o = out;
    for (int i = 0; i < 8; i++, v += 8, o += stride) {
        IDCT_1D(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])
        x0 += 65536 + (128 << 17);
        x1 += 65536 + (128 << 17);
        x2 += 65536 + (128 << 17);
        x3 += 65536 + (128 << 17);
        o[0] = clamp8((x0 + t3) >> 17);
        o[7] = clamp8((x0 - t3) >> 17);
        o[1] = clamp8((x1 + t2) >> 17);
        o[6] = clamp8((x1 - t2) >> 17);
        o[2] = clamp8((x2 + t1) >> 17);
        o[5] = clamp8((x2 - t1) >> 17);
        o[3] = clamp8((x3 + t0) >> 17);
        o[4] = clamp8((x3 - t0) >> 17);
    }
}

typedef struct {
    uint8_t bits[17];
    uint8_t vals[256];
    int     mincode[17];
    int     maxcode[17];
    int     valptr[17];
    int     present;
} huff_t;

typedef struct {
    int      id, h, v, tq, td, ta, pred;
    int      pw, ph;
    uint8_t *plane;
} jcomp_t;

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
    uint32_t       acc;
    int            cnt;
} bitstream_t;

static void bs_fill(bitstream_t *b) {
    while (b->cnt <= 24) {
        uint32_t byte = 0;
        if (b->p < b->end) {
            byte = *b->p;
            if (byte == 0xFF) {
                if (b->p + 1 < b->end && b->p[1] == 0x00) {
                    b->p += 2;
                } else {
                    byte = 0;
                }
            } else {
                b->p++;
            }
        }
        b->acc |= byte << (24 - b->cnt);
        b->cnt += 8;
    }
}

static int bs_getbits(bitstream_t *b, int n) {
    if (n == 0) return 0;
    if (b->cnt < n) bs_fill(b);
    int v = (int)(b->acc >> (32 - n));
    b->acc <<= n;
    b->cnt -= n;
    return v;
}

static int bs_getbit(bitstream_t *b) {
    if (b->cnt < 1) bs_fill(b);
    int v = (int)(b->acc >> 31);
    b->acc <<= 1;
    b->cnt -= 1;
    return v;
}

static int huff_decode(bitstream_t *b, const huff_t *h) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | bs_getbit(b);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) {
            return h->vals[h->valptr[l] + code - h->mincode[l]];
        }
    }
    return -1;
}

static int receive_extend(bitstream_t *b, int n) {
    if (n == 0) return 0;
    int v = bs_getbits(b, n);
    if (v < (1 << (n - 1))) v -= (1 << n) - 1;
    return v;
}

static void huff_build(huff_t *h) {
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        code += h->bits[l];
        k += h->bits[l];
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    h->present = 1;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

typedef struct {
    uint16_t qt[4][64];
    int      qt_present[4];
    huff_t   dc[4];
    huff_t   ac[4];
    jcomp_t  comp[3];
    int      ncomp;
    int      width, height;
    int      hmax, vmax;
    int      ri;
    int      adobe_transform;
    int      have_sof;
} jpeg_t;

static int jpeg_decode(const uint8_t *data, size_t len, png_image_t *out) {
    jpeg_t *j = (jpeg_t *)kmalloc(sizeof(jpeg_t));
    if (!j) return -1;
    memset(j, 0, sizeof(*j));
    j->adobe_transform = -1;

    int rc = -1;
    uint32_t *acc = 0;
    uint8_t *pixels = 0;
    uint8_t *prev_planes[3] = {0, 0, 0};
    (void)prev_planes;

    const uint8_t *p = data;
    const uint8_t *end = data + len;
    if (len < 4 || p[0] != 0xFF || p[1] != 0xD8) goto done;
    p += 2;

    const uint8_t *scan = 0;
    int scan_td[3] = {0, 0, 0}, scan_ta[3] = {0, 0, 0};

    while (p + 4 <= end) {
        if (p[0] != 0xFF) { p++; continue; }
        uint8_t m = p[1];
        if (m == 0xFF) { p++; continue; }
        if (m == 0x00 || m == 0x01 || (m >= 0xD0 && m <= 0xD8)) { p += 2; continue; }
        if (m == 0xD9) break;
        uint16_t seglen = rd16(p + 2);
        if (seglen < 2 || p + 2 + seglen > end) goto done;
        const uint8_t *s = p + 4;
        const uint8_t *se = p + 2 + seglen;

        if (m == 0xDB) {
            while (s < se) {
                int pq = s[0] >> 4, tq = s[0] & 15; s++;
                if (tq > 3) goto done;
                for (int i = 0; i < 64; i++) {
                    if (pq) { if (s + 2 > se) goto done; j->qt[tq][i] = rd16(s); s += 2; }
                    else    { if (s + 1 > se) goto done; j->qt[tq][i] = *s++; }
                }
                j->qt_present[tq] = 1;
            }
        } else if (m == 0xC0 || m == 0xC1) {
            if (se - s < 6) goto done;
            if (s[0] != 8) goto done;
            j->height = rd16(s + 1);
            j->width  = rd16(s + 3);
            j->ncomp  = s[5];
            if (j->ncomp != 1 && j->ncomp != 3) goto done;
            if (se - s < 6 + 3 * j->ncomp) goto done;
            for (int i = 0; i < j->ncomp; i++) {
                j->comp[i].id = s[6 + i * 3];
                j->comp[i].h  = s[7 + i * 3] >> 4;
                j->comp[i].v  = s[7 + i * 3] & 15;
                j->comp[i].tq = s[8 + i * 3];
                if (j->comp[i].h < 1 || j->comp[i].h > 4 || j->comp[i].v < 1 || j->comp[i].v > 4) goto done;
                if (j->comp[i].tq > 3) goto done;
            }
            j->have_sof = 1;
        } else if (m == 0xC2 || (m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)) {
            goto done;
        } else if (m == 0xC4) {
            while (s + 17 <= se) {
                int tc = s[0] >> 4, th = s[0] & 15;
                if (th > 3 || tc > 1) goto done;
                huff_t *h = tc ? &j->ac[th] : &j->dc[th];
                memset(h, 0, sizeof(*h));
                int total = 0;
                for (int i = 1; i <= 16; i++) { h->bits[i] = s[i]; total += s[i]; }
                if (total > 256 || s + 17 + total > se) goto done;
                memcpy(h->vals, s + 17, (size_t)total);
                huff_build(h);
                s += 17 + total;
            }
        } else if (m == 0xDD) {
            if (se - s < 2) goto done;
            j->ri = rd16(s);
        } else if (m == 0xEE) {
            if (se - s >= 12 && memcmp(s, "Adobe", 5) == 0) j->adobe_transform = s[11];
        } else if (m == 0xDA) {
            if (!j->have_sof) goto done;
            int ns = s[0];
            if (ns != j->ncomp || se - s < 1 + 2 * ns + 3) goto done;
            for (int i = 0; i < ns; i++) {
                int cid = s[1 + i * 2];
                int tdta = s[2 + i * 2];
                int idx = -1;
                for (int k = 0; k < j->ncomp; k++) if (j->comp[k].id == cid) idx = k;
                if (idx < 0) goto done;
                scan_td[idx] = tdta >> 4;
                scan_ta[idx] = tdta & 15;
            }
            scan = se;
            break;
        }
        p += 2 + seglen;
    }

    if (!scan || j->width <= 0 || j->height <= 0) goto done;
    for (int i = 0; i < j->ncomp; i++) {
        j->comp[i].td = scan_td[i] & 3;
        j->comp[i].ta = scan_ta[i] & 3;
        if (!j->qt_present[j->comp[i].tq]) goto done;
        if (!j->dc[j->comp[i].td].present || !j->ac[j->comp[i].ta].present) goto done;
    }

    int W = j->width, H = j->height;
    j->hmax = 1; j->vmax = 1;
    for (int i = 0; i < j->ncomp; i++) {
        if (j->comp[i].h > j->hmax) j->hmax = j->comp[i].h;
        if (j->comp[i].v > j->vmax) j->vmax = j->comp[i].v;
    }

    int mcux = (W + 8 * j->hmax - 1) / (8 * j->hmax);
    int mcuy = (H + 8 * j->vmax - 1) / (8 * j->vmax);

    int s_down = 1;
    while ((W + s_down - 1) / s_down > (int)IMG_MAX_OUT_W || (H + s_down - 1) / s_down > (int)IMG_MAX_OUT_H) s_down++;
    int ow = (W + s_down - 1) / s_down;
    int oh = (H + s_down - 1) / s_down;
    if ((uint64_t)ow * oh * 3 > IMG_MAX_BYTES) goto done;

    for (int i = 0; i < j->ncomp; i++) {
        j->comp[i].pw = mcux * 8 * j->comp[i].h;
        j->comp[i].ph = 8 * j->comp[i].v;
        j->comp[i].plane = (uint8_t *)kmalloc((size_t)j->comp[i].pw * j->comp[i].ph);
        if (!j->comp[i].plane) goto cleanup;
        j->comp[i].pred = 0;
    }

    pixels = (uint8_t *)kmalloc((size_t)ow * oh * 3);
    acc = (uint32_t *)kmalloc((size_t)ow * 3 * sizeof(uint32_t));
    if (!pixels || !acc) goto cleanup;
    memset(acc, 0, (size_t)ow * 3 * sizeof(uint32_t));

    bitstream_t bs;
    bs.p = scan; bs.end = end; bs.acc = 0; bs.cnt = 0;

    int coef[64];
    int acc_rows = 0;
    int out_y = 0;
    int mcu_count = 0;
    int total_mcus = mcux * mcuy;
    int ok = 1;

    for (int my = 0; my < mcuy && ok; my++) {
        for (int mx = 0; mx < mcux && ok; mx++) {
            for (int ci = 0; ci < j->ncomp && ok; ci++) {
                jcomp_t *c = &j->comp[ci];
                const huff_t *hd = &j->dc[c->td];
                const huff_t *ha = &j->ac[c->ta];
                const uint16_t *q = j->qt[c->tq];
                for (int by = 0; by < c->v; by++) {
                    for (int bx = 0; bx < c->h; bx++) {
                        memset(coef, 0, sizeof(coef));
                        int t = huff_decode(&bs, hd);
                        if (t < 0 || t > 15) { ok = 0; break; }
                        int diff = t ? receive_extend(&bs, t) : 0;
                        c->pred += diff;
                        coef[0] = c->pred * (int)q[0];
                        int k = 1;
                        while (k < 64) {
                            int rs = huff_decode(&bs, ha);
                            if (rs < 0) { ok = 0; break; }
                            int r = rs >> 4, sbits = rs & 15;
                            if (sbits == 0) {
                                if (r == 15) { k += 16; continue; }
                                break;
                            }
                            k += r;
                            if (k > 63) break;
                            coef[g_zigzag[k]] = receive_extend(&bs, sbits) * (int)q[k];
                            k++;
                        }
                        if (!ok) break;
                        idct_block(c->plane + (size_t)(by * 8) * c->pw + (size_t)(mx * c->h + bx) * 8, c->pw, coef);
                    }
                    if (!ok) break;
                }
            }
            mcu_count++;
            if (ok && j->ri && (mcu_count % j->ri) == 0 && mcu_count < total_mcus) {
                bs.acc = 0; bs.cnt = 0;
                while (bs.p + 1 < bs.end && !(bs.p[0] == 0xFF && bs.p[1] >= 0xD0 && bs.p[1] <= 0xD7)) bs.p++;
                if (bs.p + 1 < bs.end) bs.p += 2;
                for (int ci = 0; ci < j->ncomp; ci++) j->comp[ci].pred = 0;
            }
        }

        int strip_h = 8 * j->vmax;
        for (int ry = 0; ry < strip_h; ry++) {
            int y = my * strip_h + ry;
            if (y >= H) break;
            for (int x = 0; x < W; x++) {
                int r, g, b;
                if (j->ncomp == 1) {
                    int Y = j->comp[0].plane[(size_t)(ry * j->comp[0].v / j->vmax) * j->comp[0].pw + (x * j->comp[0].h / j->hmax)];
                    r = g = b = Y;
                } else {
                    jcomp_t *c0 = &j->comp[0], *c1 = &j->comp[1], *c2 = &j->comp[2];
                    int Y  = c0->plane[(size_t)(ry * c0->v / j->vmax) * c0->pw + (x * c0->h / j->hmax)];
                    int Cb = c1->plane[(size_t)(ry * c1->v / j->vmax) * c1->pw + (x * c1->h / j->hmax)];
                    int Cr = c2->plane[(size_t)(ry * c2->v / j->vmax) * c2->pw + (x * c2->h / j->hmax)];
                    if (j->adobe_transform == 0) {
                        r = Y; g = Cb; b = Cr;
                    } else {
                        Cb -= 128; Cr -= 128;
                        r = Y + ((91881 * Cr) >> 16);
                        g = Y - ((22554 * Cb + 46802 * Cr) >> 16);
                        b = Y + ((116130 * Cb) >> 16);
                    }
                }
                uint32_t *a = &acc[(size_t)(x / s_down) * 3];
                a[0] += (uint32_t)clamp8(r);
                a[1] += (uint32_t)clamp8(g);
                a[2] += (uint32_t)clamp8(b);
            }
            acc_rows++;
            if (acc_rows == s_down || y == H - 1) {
                for (int ox = 0; ox < ow; ox++) {
                    int cols = W - ox * s_down;
                    if (cols > s_down) cols = s_down;
                    uint32_t cnt = (uint32_t)(cols * acc_rows);
                    uint8_t *o = pixels + ((size_t)out_y * ow + ox) * 3;
                    o[0] = (uint8_t)(acc[ox * 3 + 0] / cnt);
                    o[1] = (uint8_t)(acc[ox * 3 + 1] / cnt);
                    o[2] = (uint8_t)(acc[ox * 3 + 2] / cnt);
                }
                memset(acc, 0, (size_t)ow * 3 * sizeof(uint32_t));
                acc_rows = 0;
                out_y++;
            }
        }
    }

    if (!ok && out_y < oh / 2) goto cleanup;
    while (out_y < oh) {
        memset(pixels + (size_t)out_y * ow * 3, 0, (size_t)ow * 3);
        out_y++;
    }

    out->width = (uint32_t)ow;
    out->height = (uint32_t)oh;
    out->channels = 3;
    out->pixels = pixels;
    pixels = 0;
    rc = 0;

cleanup:
    for (int i = 0; i < 3; i++) if (j->comp[i].plane) kfree(j->comp[i].plane);
done:
    if (acc) kfree(acc);
    if (pixels) kfree(pixels);
    kfree(j);
    return rc;
}

static uint32_t rd32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int bmp_decode(const uint8_t *d, size_t len, png_image_t *out) {
    if (len < 26 || d[0] != 'B' || d[1] != 'M') return -1;
    uint32_t data_off = rd32le(d + 10);
    uint32_t hdr = rd32le(d + 14);
    if (hdr < 40 || 14 + hdr > len) return -1;

    int32_t w = (int32_t)rd32le(d + 18);
    int32_t h = (int32_t)rd32le(d + 22);
    uint16_t bpp = (uint16_t)(d[28] | (d[29] << 8));
    uint32_t comp = rd32le(d + 30);
    uint32_t ncolors = rd32le(d + 46);

    int top_down = 0;
    if (h < 0) { top_down = 1; h = -h; }
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return -1;
    if (comp != 0 && comp != 3) return -1;
    if (bpp != 8 && bpp != 24 && bpp != 32) return -1;
    if ((uint64_t)w * h * 3 > IMG_MAX_BYTES) return -1;

    uint32_t pal_off = 14 + hdr;
    uint32_t pal_n = 0;
    if (bpp == 8) {
        pal_n = ncolors ? ncolors : 256;
        if (pal_n > 256 || pal_off + pal_n * 4 > len) return -1;
    }

    uint64_t stride = (((uint64_t)w * bpp + 31) / 32) * 4;
    if ((uint64_t)data_off + stride * (uint64_t)h > len) return -1;

    uint8_t *px = (uint8_t *)kmalloc((size_t)w * h * 3);
    if (!px) return -1;

    for (int y = 0; y < h; y++) {
        int sy = top_down ? y : (h - 1 - y);
        const uint8_t *row = d + data_off + (size_t)stride * sy;
        uint8_t *o = px + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            if (bpp == 24) {
                o[x * 3 + 0] = row[x * 3 + 2];
                o[x * 3 + 1] = row[x * 3 + 1];
                o[x * 3 + 2] = row[x * 3 + 0];
            } else if (bpp == 32) {
                o[x * 3 + 0] = row[x * 4 + 2];
                o[x * 3 + 1] = row[x * 4 + 1];
                o[x * 3 + 2] = row[x * 4 + 0];
            } else {
                uint32_t idx = row[x];
                if (idx >= pal_n) idx = 0;
                const uint8_t *pe = d + pal_off + idx * 4;
                o[x * 3 + 0] = pe[2];
                o[x * 3 + 1] = pe[1];
                o[x * 3 + 2] = pe[0];
            }
        }
    }

    out->width = (uint32_t)w;
    out->height = (uint32_t)h;
    out->channels = 3;
    out->pixels = px;
    return 0;
}

int image_decode(const uint8_t *data, size_t len, png_image_t *out) {
    if (!data || !out) return -1;
    out->pixels = 0;
    out->width = 0;
    out->height = 0;
    out->channels = 0;
    if (len >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        return png_decode(data, len, out) == PNG_OK && out->pixels ? 0 : -1;
    }
    if (len >= 2 && data[0] == 'B' && data[1] == 'M') return bmp_decode(data, len, out);
    if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8) return jpeg_decode(data, len, out);
    return -1;
}

static int ci_eq(char a, char b) {
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
    return a == b;
}

static int suffix_is(const char *name, uint8_t n, const char *ext) {
    size_t el = strlen(ext);
    if (n <= el + 1) return 0;
    if (name[n - el - 1] != '.') return 0;
    for (size_t i = 0; i < el; i++) if (!ci_eq(name[n - el + i], ext[i])) return 0;
    return 1;
}

int image_has_supported_suffix(const char *name, uint8_t name_len) {
    return suffix_is(name, name_len, "png") || suffix_is(name, name_len, "bmp") ||
           suffix_is(name, name_len, "jpg") || suffix_is(name, name_len, "jpeg");
}
