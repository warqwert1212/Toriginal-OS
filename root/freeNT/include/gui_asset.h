/* gui_asset.h - Generic "load a PNG off TRPFS and scale it" helper.
 *
 * wallpaper.c already has this exact sequence (fs_open -> fs_read ->
 * png_decode -> nearest-neighbor scale) written once for the desktop
 * background. startmenu.c needs the same sequence five more times
 * (orb, orb hover, taskbar strip, panel, programs list, power
 * button), so it's pulled out here instead of copy-pasted. wallpaper.c
 * itself is left as-is - it already works and is tested on real
 * hardware/QEMU, and refactoring already-verified code onto this
 * wasn't worth the regression risk for this change.
 */
#ifndef GUI_ASSET_H
#define GUI_ASSET_H

#include "png.h"
#include "graphics_core.h"

/* Reads and decodes the PNG at `path`. Returns 1 on success - `out`
 * is filled in and out->pixels is heap-allocated (caller must
 * png_free(out) when done). Returns 0 on any failure (file missing,
 * not a PNG this decoder handles, out of memory) and leaves `out`
 * zeroed - callers treat that as "asset not installed yet", the same
 * honest fallback wallpaper.c uses, not an error to crash on. */
int gui_asset_load_png(const char *path, png_image_t *out);

/* Nearest-neighbor scales `src` to dst_w x dst_h. Preserves the
 * source alpha channel if it has one (RGBA -> straight-through);
 * treats a 3-channel (RGB, no alpha) source as fully opaque. Returns
 * a heap-allocated color_t[dst_w*dst_h] buffer the caller owns
 * (kfree() when done), or NULL on OOM/invalid input. */
color_t *gui_asset_scale_argb(const png_image_t *src, uint32_t dst_w, uint32_t dst_h);

/* Alpha-blits an already-scaled ARGB buffer at (dst_x, dst_y) via
 * graphics_blend_pixel - respects real per-pixel alpha, unlike
 * gfx2d_blit's opaque copy, so soft/rounded-edge UI art (the orb's
 * glow, the panel's rounded corners) composites correctly over
 * whatever's already on screen instead of carrying a black or
 * garbage-colored box around it. */
void gui_asset_draw_argb(const color_t *buf, uint32_t w, uint32_t h,
                          int32_t dst_x, int32_t dst_y);

#endif /* GUI_ASSET_H */
