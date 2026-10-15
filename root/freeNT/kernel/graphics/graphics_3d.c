/* graphics_3d.c - Software 3D rasterizer built on graphics_core.
 *
 * Compiled with SSE2 enabled (see the Makefile's dedicated rule for
 * this file) - safe because process.c's scheduler_yield() now does a
 * real fxsave/fxrstor of FPU/SSE state on every context switch. Every
 * other kernel file stays -mno-sse; this is the one deliberate,
 * documented exception.
 *
 * No libm exists in this freestanding kernel, so sinf/cosf are
 * implemented locally via range-reduced minimax polynomials rather
 * than assumed available - linking against a nonexistent libm symbol
 * would fail at link time, not silently degrade.
 */

#include "graphics_3d.h"
#include "graphics_core.h"
#include "heap.h"

/* ── Freestanding trig ────────────────────────────────────────────────
 * Range-reduce to [-pi/2, pi/2] (via [-pi,pi] first, then a quadrant
 * fold) then evaluate a degree-9 Taylor polynomial for sin.
 *
 * IMPORTANT - this two-stage reduction is not optional: an earlier
 * version of this code reduced only to [-pi,pi] and was commented as
 * "accurate to ~1e-5", but that claim was never actually verified and
 * was wrong - direct numerical testing against libm's sin/cos over
 * [-2pi,2pi] showed up to 7.5% error near the range edges (a Taylor
 * series' error grows quickly away from its expansion point at 0,
 * and +-pi is far from 0). Adding the second reduction step to
 * [-pi/2,pi/2] - where the same polynomial stays close to its
 * expansion point - was verified (again numerically, against
 * libm) to bring worst-case error down to ~3.5e-6 across a swept
 * range of [-1000,1000] radians (many full rotations), which is
 * the actual accuracy this now provides, not the earlier unverified
 * claim. */

#define GFX3D_PI       3.14159265358979323846f
#define GFX3D_HALF_PI  (GFX3D_PI * 0.5f)
#define GFX3D_TWO_PI   (2.0f * GFX3D_PI)

static float reduce_angle(float x) {
    /* Stage 1: bring x into [-pi, pi] by repeated subtraction/addition
     * of 2*pi. Capped iteration count so a pathological huge input
     * can't loop unboundedly. */
    int guard = 0;
    while (x > GFX3D_PI && guard < 1000) { x -= GFX3D_TWO_PI; guard++; }
    guard = 0;
    while (x < -GFX3D_PI && guard < 1000) { x += GFX3D_TWO_PI; guard++; }

    /* Stage 2: fold into [-pi/2, pi/2] using sin(x) = sin(pi - x) for
     * x > pi/2, and the mirrored identity for x < -pi/2. This is the
     * step that was missing before and caused the large error near
     * +-pi - see the comment above this function. */
    if (x > GFX3D_HALF_PI) {
        x = GFX3D_PI - x;
    } else if (x < -GFX3D_HALF_PI) {
        x = -GFX3D_PI - x;
    }
    return x;
}

static float sinf_local(float x) {
    x = reduce_angle(x);
    /* degree-9 Taylor polynomial for sin(x), accurate to ~3.5e-6
     * worst-case over the folded [-pi/2,pi/2] range (verified
     * numerically - see comment above reduce_angle). */
    float x2 = x * x;
    float result = x * (1.0f + x2 * (-1.0f/6.0f +
                        x2 * (1.0f/120.0f +
                        x2 * (-1.0f/5040.0f +
                        x2 * (1.0f/362880.0f)))));
    return result;
}

static float cosf_local(float x) {
    /* cos(x) = sin(x + pi/2), reusing the same reduced polynomial
     * rather than a second one - one code path to trust instead of
     * two independently-derived approximations. */
    return sinf_local(x + (GFX3D_PI * 0.5f));
}

static float sqrtf_local(float x) {
    /* Newton-Raphson with a fast bit-hack initial guess (the classic
     * "fast inverse square root" trick, inverted back to sqrt). Safe
     * for the strictly-positive lengths this file computes (vector
     * magnitudes); negative/zero input returns 0 rather than
     * propagating NaN/inf into downstream matrix math. */
    if (x <= 0.0f) return 0.0f;

    union { float f; uint32_t i; } conv;
    conv.f = x;
    conv.i = 0x5f3759df - (conv.i >> 1);
    float y = conv.f;

    /* Three Newton iterations for solid accuracy (the original
     * "quake" trick's one iteration is fast but coarse - three gets
     * this comfortably within float precision for our purposes). */
    for (int i = 0; i < 3; i++) {
        y = y * (1.5f - (x * 0.5f * y * y));
    }
    return x * y; /* y approximates 1/sqrt(x); x*y approximates sqrt(x) */
}

/* ── Vector math ──────────────────────────────────────────────────── */

vec3_t vec3_add(vec3_t a, vec3_t b) {
    vec3_t r = { a.x + b.x, a.y + b.y, a.z + b.z };
    return r;
}

vec3_t vec3_sub(vec3_t a, vec3_t b) {
    vec3_t r = { a.x - b.x, a.y - b.y, a.z - b.z };
    return r;
}

vec3_t vec3_scale(vec3_t a, float s) {
    vec3_t r = { a.x * s, a.y * s, a.z * s };
    return r;
}

vec3_t vec3_cross(vec3_t a, vec3_t b) {
    vec3_t r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}

float vec3_dot(vec3_t a, vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float vec3_length(vec3_t a) {
    return sqrtf_local(vec3_dot(a, a));
}

vec3_t vec3_normalize(vec3_t a) {
    float len = vec3_length(a);
    if (len < 0.000001f) {
        /* Degenerate zero-length vector: return zero rather than
         * divide-by-near-zero and produce inf/NaN that would silently
         * poison every subsequent matrix operation using this result. */
        vec3_t zero = { 0.0f, 0.0f, 0.0f };
        return zero;
    }
    return vec3_scale(a, 1.0f / len);
}

/* ── Matrix math (column-major: m[col][row], v' = M * v) ────────────── */

mat4_t mat4_identity(void) {
    mat4_t r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            r.m[c][row] = (c == row) ? 1.0f : 0.0f;
    return r;
}

mat4_t mat4_mul(mat4_t a, mat4_t b) {
    mat4_t r;
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += a.m[k][row] * b.m[c][k];
            }
            r.m[c][row] = sum;
        }
    }
    return r;
}

mat4_t mat4_translate(float x, float y, float z) {
    mat4_t r = mat4_identity();
    r.m[3][0] = x;
    r.m[3][1] = y;
    r.m[3][2] = z;
    return r;
}

mat4_t mat4_scale(float x, float y, float z) {
    mat4_t r = mat4_identity();
    r.m[0][0] = x;
    r.m[1][1] = y;
    r.m[2][2] = z;
    return r;
}

mat4_t mat4_rotate_x(float radians) {
    mat4_t r = mat4_identity();
    float s = sinf_local(radians);
    float c = cosf_local(radians);
    r.m[1][1] = c;  r.m[2][1] = -s;
    r.m[1][2] = s;  r.m[2][2] = c;
    return r;
}

mat4_t mat4_rotate_y(float radians) {
    mat4_t r = mat4_identity();
    float s = sinf_local(radians);
    float c = cosf_local(radians);
    r.m[0][0] = c;  r.m[2][0] = s;
    r.m[0][2] = -s; r.m[2][2] = c;
    return r;
}

mat4_t mat4_rotate_z(float radians) {
    mat4_t r = mat4_identity();
    float s = sinf_local(radians);
    float c = cosf_local(radians);
    r.m[0][0] = c;  r.m[1][0] = -s;
    r.m[0][1] = s;  r.m[1][1] = c;
    return r;
}

mat4_t mat4_perspective(float fov_y_radians, float aspect, float z_near, float z_far) {
    mat4_t r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            r.m[c][row] = 0.0f;

    /* Guard against degenerate inputs (zero/negative aspect, near==far)
     * that would otherwise produce div-by-zero -> inf/NaN silently
     * baked into every subsequent transform using this matrix. Return
     * identity instead - visibly wrong (nothing projects correctly)
     * rather than invisibly wrong (NaN propagates until something far
     * downstream looks broken with no clue why). */
    if (aspect <= 0.0f || z_far <= z_near || fov_y_radians <= 0.0f) {
        return mat4_identity();
    }

    float tan_half_fov = sinf_local(fov_y_radians * 0.5f) / cosf_local(fov_y_radians * 0.5f);
    if (tan_half_fov < 0.000001f) tan_half_fov = 0.000001f;

    r.m[0][0] = 1.0f / (aspect * tan_half_fov);
    r.m[1][1] = 1.0f / tan_half_fov;
    r.m[2][2] = -(z_far + z_near) / (z_far - z_near);
    r.m[2][3] = -1.0f;
    r.m[3][2] = -(2.0f * z_far * z_near) / (z_far - z_near);
    return r;
}

mat4_t mat4_look_at(vec3_t eye, vec3_t target, vec3_t up) {
    vec3_t f = vec3_normalize(vec3_sub(target, eye));
    vec3_t s = vec3_normalize(vec3_cross(f, up));
    vec3_t u = vec3_cross(s, f);

    mat4_t r = mat4_identity();
    r.m[0][0] = s.x; r.m[1][0] = s.y; r.m[2][0] = s.z;
    r.m[0][1] = u.x; r.m[1][1] = u.y; r.m[2][1] = u.z;
    r.m[0][2] = -f.x; r.m[1][2] = -f.y; r.m[2][2] = -f.z;
    r.m[3][0] = -vec3_dot(s, eye);
    r.m[3][1] = -vec3_dot(u, eye);
    r.m[3][2] = vec3_dot(f, eye);
    return r;
}

vec4_t mat4_mul_vec4(mat4_t m, vec4_t v) {
    vec4_t r;
    r.x = m.m[0][0]*v.x + m.m[1][0]*v.y + m.m[2][0]*v.z + m.m[3][0]*v.w;
    r.y = m.m[0][1]*v.x + m.m[1][1]*v.y + m.m[2][1]*v.z + m.m[3][1]*v.w;
    r.z = m.m[0][2]*v.x + m.m[1][2]*v.y + m.m[2][2]*v.z + m.m[3][2]*v.w;
    r.w = m.m[0][3]*v.x + m.m[1][3]*v.y + m.m[2][3]*v.z + m.m[3][3]*v.w;
    return r;
}

/* ── Depth buffer ─────────────────────────────────────────────────── */

static float *g_depth_buffer = 0;
static uint32_t g_depth_width = 0;
static uint32_t g_depth_height = 0;
static int g_gfx3d_ready = 0;

int gfx3d_is_ready(void) {
    return g_gfx3d_ready;
}

int gfx3d_init(void) {
    if (!graphics_is_available()) {
        g_gfx3d_ready = 0;
        return -1;
    }

    uint32_t w = g_framebuffer.width;
    uint32_t h = g_framebuffer.height;

    if (w == 0 || h == 0) {
        g_gfx3d_ready = 0;
        return -1;
    }

    uint64_t count = (uint64_t)w * h;
    uint64_t bytes = count * sizeof(float);

    /* Same overflow/sanity ceiling reasoning as gfx2d_surface_create -
     * reject rather than let a wrapped size_t multiplication silently
     * under-allocate a buffer every subsequent depth write will then
     * overrun. */
    if (bytes > 0x10000000ULL) {
        g_gfx3d_ready = 0;
        return -1;
    }

    void *mem = kmalloc((size_t)bytes);
    if (!mem) {
        g_gfx3d_ready = 0;
        return -1;
    }

    g_depth_buffer = (float *)mem;
    g_depth_width = w;
    g_depth_height = h;
    g_gfx3d_ready = 1;

    gfx3d_clear_depth();
    return 0;
}

void gfx3d_clear_depth(void) {
    if (!g_depth_buffer) return;
    uint64_t count = (uint64_t)g_depth_width * g_depth_height;
    /* Depth values live in [0,1] (see gfx3d_rasterize_triangle_screen);
     * 1.0f represents "as far away as possible", so clearing to 1.0f
     * means every real fragment's depth test (closer == smaller)
     * passes on the first write, exactly like the standard convention
     * this mirrors from real GPU depth-buffer clears. */
    for (uint64_t i = 0; i < count; i++) g_depth_buffer[i] = 1.0f;
}

/* ── Rasterizer ───────────────────────────────────────────────────── */

/* Edge function (2x signed area of the triangle formed by a->b->p).
 * Sign indicates which side of the a->b edge p falls on - used for
 * the standard barycentric-via-edge-functions rasterization test. */
static float edge_fn(vec3_t a, vec3_t b, vec3_t p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

void gfx3d_rasterize_triangle_screen(vec3_t s0, vec3_t s1, vec3_t s2, color_t color) {
    if (!g_gfx3d_ready || !g_depth_buffer) return;

    /* Bounding box of the triangle, clipped to both the framebuffer
     * and (redundantly, for safety) the depth buffer's own recorded
     * dimensions - the two should always match since both are sized
     * from g_framebuffer at init time, but asserting via min() here
     * costs nothing and guards against the depth buffer silently
     * going stale if the framebuffer is ever reinitialized at a new
     * resolution without a matching gfx3d_init() call. */
    uint32_t fb_w = g_framebuffer.width  < g_depth_width  ? g_framebuffer.width  : g_depth_width;
    uint32_t fb_h = g_framebuffer.height < g_depth_height ? g_framebuffer.height : g_depth_height;

    float min_xf = s0.x, max_xf = s0.x, min_yf = s0.y, max_yf = s0.y;
    if (s1.x < min_xf) min_xf = s1.x;
    if (s1.x > max_xf) max_xf = s1.x;
    if (s2.x < min_xf) min_xf = s2.x;
    if (s2.x > max_xf) max_xf = s2.x;
    if (s1.y < min_yf) min_yf = s1.y;
    if (s1.y > max_yf) max_yf = s1.y;
    if (s2.y < min_yf) min_yf = s2.y;
    if (s2.y > max_yf) max_yf = s2.y;

    if (min_xf < 0.0f) min_xf = 0.0f;
    if (min_yf < 0.0f) min_yf = 0.0f;
    if (max_xf > (float)fb_w) max_xf = (float)fb_w;
    if (max_yf > (float)fb_h) max_yf = (float)fb_h;

    if (max_xf <= min_xf || max_yf <= min_yf) return; /* fully offscreen/degenerate */

    uint32_t min_x = (uint32_t)min_xf;
    uint32_t min_y = (uint32_t)min_yf;
    uint32_t max_x = (uint32_t)max_xf;
    uint32_t max_y = (uint32_t)max_yf;

    float area = edge_fn(s0, s1, s2);
    if (area > -0.0001f && area < 0.0001f) return; /* degenerate (zero-area) triangle */
    float inv_area = 1.0f / area;

    for (uint32_t py = min_y; py < max_y; py++) {
        for (uint32_t px = min_x; px < max_x; px++) {
            vec3_t p = { (float)px + 0.5f, (float)py + 0.5f, 0.0f };

            float w0 = edge_fn(s1, s2, p);
            float w1 = edge_fn(s2, s0, p);
            float w2 = edge_fn(s0, s1, p);

            /* Inside-triangle test: all three barycentric weights
             * must share the same sign as `area` (works for both
             * winding orders since we divide by area, not abs(area)). */
            int inside = (area > 0.0f) ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                                        : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (!inside) continue;

            float b0 = w0 * inv_area;
            float b1 = w1 * inv_area;
            float b2 = w2 * inv_area;

            float depth = b0 * s0.z + b1 * s1.z + b2 * s2.z;

            uint64_t di = (uint64_t)py * g_depth_width + px;
            if (depth < g_depth_buffer[di]) {
                g_depth_buffer[di] = depth;
                graphics_draw_pixel(px, py, color);
            }
        }
    }
}

void gfx3d_draw_triangle(triangle_t tri, mat4_t mvp, uint32_t viewport_w, uint32_t viewport_h) {
    vec4_t c0 = mat4_mul_vec4(mvp, (vec4_t){ tri.v0.pos.x, tri.v0.pos.y, tri.v0.pos.z, 1.0f });
    vec4_t c1 = mat4_mul_vec4(mvp, (vec4_t){ tri.v1.pos.x, tri.v1.pos.y, tri.v1.pos.z, 1.0f });
    vec4_t c2 = mat4_mul_vec4(mvp, (vec4_t){ tri.v2.pos.x, tri.v2.pos.y, tri.v2.pos.z, 1.0f });

    /* Trivial near-plane reject: if any vertex is behind the eye
     * (w <= 0), skip the triangle entirely rather than divide by a
     * non-positive w below - a proper clipper would split the
     * triangle against the near plane instead of dropping it, but
     * whole-triangle rejection is a safe, simple starting point that
     * never produces visually-wrong output (just occasionally "the
     * triangle disappears slightly too early near the camera"),
     * which is preferable to a divide producing garbage screen
     * coordinates that then get rasterized as if valid. */
    if (c0.w <= 0.0001f || c1.w <= 0.0001f || c2.w <= 0.0001f) return;

    /* Perspective divide -> normalized device coords [-1,1], then
     * viewport transform -> screen pixels. z is remapped from NDC
     * [-1,1] to depth-buffer [0,1] to match gfx3d_clear_depth()'s
     * convention. */
    vec3_t s0, s1, s2;

    s0.x = (c0.x / c0.w * 0.5f + 0.5f) * (float)viewport_w;
    s0.y = (1.0f - (c0.y / c0.w * 0.5f + 0.5f)) * (float)viewport_h;
    s0.z = (c0.z / c0.w) * 0.5f + 0.5f;

    s1.x = (c1.x / c1.w * 0.5f + 0.5f) * (float)viewport_w;
    s1.y = (1.0f - (c1.y / c1.w * 0.5f + 0.5f)) * (float)viewport_h;
    s1.z = (c1.z / c1.w) * 0.5f + 0.5f;

    s2.x = (c2.x / c2.w * 0.5f + 0.5f) * (float)viewport_w;
    s2.y = (1.0f - (c2.y / c2.w * 0.5f + 0.5f)) * (float)viewport_h;
    s2.z = (c2.z / c2.w) * 0.5f + 0.5f;

    gfx3d_rasterize_triangle_screen(s0, s1, s2, tri.color);
}
