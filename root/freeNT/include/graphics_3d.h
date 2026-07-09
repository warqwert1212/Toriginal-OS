/* graphics_3d.h - Software 3D rasterizer built on graphics_core.
 *
 * Like graphics_2d.c, this never touches g_framebuffer memory
 * directly - triangle fill goes through graphics_core's
 * fill_span/row-ptr primitives. This is a from-scratch fixed-
 * pipeline rasterizer (model -> world -> view -> projection ->
 * screen), no hardware GPU assumed.
 *
 * Scope for this stage: vector/matrix math, a depth buffer, and
 * flat-shaded + depth-tested triangle rasterization. This is enough
 * to render solid 3D scenes (useful groundwork for later WM effects
 * like window-flip animations, or actual 3D content). Texture
 * mapping and per-vertex lighting are natural follow-ons once this
 * core is confirmed working.
 */
#ifndef GRAPHICS_3D_H
#define GRAPHICS_3D_H

#include "graphics_core.h"

/* ── Vector / matrix types ────────────────────────────────────────── */

typedef struct { float x, y, z; }     vec3_t;
typedef struct { float x, y, z, w; }  vec4_t;

/* Column-major 4x4, matching the convention used by the transform
 * helpers below (v' = M * v). Stored as m[col][row] so
 * mat4_identity()/mat4_mul() read naturally against that layout. */
typedef struct { float m[4][4]; } mat4_t;

/* A single vertex as it flows through the pipeline: position, and a
 * flat per-triangle color (no per-vertex color interpolation yet -
 * that's a natural extension once this base rasterizer is
 * confirmed). */
typedef struct {
    vec3_t pos;
} vec3_vertex_t;

typedef struct {
    vec3_vertex_t v0, v1, v2;
    color_t color;
} triangle_t;

/* ── Vector math ──────────────────────────────────────────────────── */
vec3_t vec3_add(vec3_t a, vec3_t b);
vec3_t vec3_sub(vec3_t a, vec3_t b);
vec3_t vec3_scale(vec3_t a, float s);
vec3_t vec3_cross(vec3_t a, vec3_t b);
float  vec3_dot(vec3_t a, vec3_t b);
float  vec3_length(vec3_t a);
vec3_t vec3_normalize(vec3_t a);

/* ── Matrix math ──────────────────────────────────────────────────── */
mat4_t mat4_identity(void);
mat4_t mat4_mul(mat4_t a, mat4_t b);
mat4_t mat4_translate(float x, float y, float z);
mat4_t mat4_scale(float x, float y, float z);
mat4_t mat4_rotate_x(float radians);
mat4_t mat4_rotate_y(float radians);
mat4_t mat4_rotate_z(float radians);
mat4_t mat4_perspective(float fov_y_radians, float aspect, float z_near, float z_far);
mat4_t mat4_look_at(vec3_t eye, vec3_t target, vec3_t up);
vec4_t mat4_mul_vec4(mat4_t m, vec4_t v);

/* ── Depth buffer ─────────────────────────────────────────────────── */
/* One depth buffer sized to the current framebuffer resolution.
 * Allocated lazily on first gfx3d_init() call - if allocation fails,
 * gfx3d_init() returns -1 and the rasterizer refuses to draw (rather
 * than draw without depth testing and silently produce visually
 * wrong output with no indication anything failed). */
int  gfx3d_init(void);
void gfx3d_clear_depth(void);
int  gfx3d_is_ready(void);

/* ── Rasterizer ───────────────────────────────────────────────────── */
/* Renders one triangle already in *screen space* (x,y in pixels, z
 * in normalized device depth [0,1] used only for the depth test) -
 * the caller is responsible for running vertices through
 * model/view/projection matrices and the perspective divide first.
 * This split (transform math here, but rasterization takes
 * pre-transformed input) mirrors how real GPU pipelines separate
 * vertex processing from the rasterizer stage, and keeps this
 * function simple enough to trust. */
void gfx3d_rasterize_triangle_screen(vec3_t s0, vec3_t s1, vec3_t s2, color_t color);

/* Convenience wrapper: takes a triangle_t in model space plus the
 * combined model-view-projection matrix, does the full transform +
 * perspective divide + viewport mapping, then rasterizes. This is
 * the function most callers actually want. */
void gfx3d_draw_triangle(triangle_t tri, mat4_t mvp, uint32_t viewport_w, uint32_t viewport_h);

#endif /* GRAPHICS_3D_H */
