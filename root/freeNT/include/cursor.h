/* cursor.h - Mouse cursor rendering, loaded from real PNG assets on
 * disk (NOT embedded in the kernel binary).
 *
 * Cursor sprites live as actual .png files under /sys/gui/assets/ on
 * the TRPFS-formatted disk, loaded at runtime via fs_open/fs_read and
 * decoded with the from-scratch PNG decoder (png.h) - this is a real
 * OS asset pipeline, not a compiled-in bitmap array. If the asset
 * files aren't present (e.g. no persistent disk installed yet), the
 * cursor system falls back to a minimal procedurally-drawn pointer
 * shape rather than failing to render a cursor at all - see
 * cursor_draw()'s fallback path.
 */
#ifndef CURSOR_H
#define CURSOR_H

#include "graphics_core.h"

typedef enum {
    CURSOR_ARROW = 0,  /* default pointer */
    CURSOR_HAND  = 1,  /* click / link / interactive state */
} cursor_shape_t;

#define CURSOR_ASSET_PATH_ARROW "/sys/gui/assets/cursor_arrow.png"
#define CURSOR_ASSET_PATH_HAND  "/sys/gui/assets/cursor_hand.png"

/* Loads both cursor PNGs from disk and decodes them into ready-to-
 * blit ARGB surfaces. Safe to call even if the assets aren't present
 * or the disk isn't mounted yet - returns 0 on success, -1 if either
 * asset failed to load (cursor_draw() still works in that case via
 * its procedural fallback, so callers aren't required to treat a
 * nonzero return as fatal). Should be called once during boot, after
 * fs_init()/installer_try_automount() have run. */
int cursor_assets_init(void);

/* True if cursor_assets_init() successfully loaded real PNG assets -
 * false means cursor_draw() is using the procedural fallback shape
 * for both cursor states. */
int cursor_assets_loaded(void);

/* Draws the given cursor shape at (x,y), which is the sprite's
 * top-left corner. Uses the loaded PNG asset (full color + alpha
 * blending via graphics_blend_pixel) if cursor_assets_loaded(),
 * otherwise draws a small procedural pointer shape in solid `color`
 * as a fallback so a cursor is always visible even before any disk
 * asset is available. */
/* Returns the pixel dimensions of the currently-active sprite for
 * `shape` - callers that need to know how large an area to erase/
 * redraw around the cursor (see gfx_terminal.c's mouse-cursor erase
 * logic) must query this rather than assume a fixed size, since PNG
 * assets can be any resolution (unlike the old embedded 32x32
 * bitmaps this replaced). Returns a sane small fallback size (16x16)
 * if assets aren't loaded, matching the fallback shape's actual
 * rendered footprint, not the ~32x32 dimensions no longer used. */
void cursor_get_size(cursor_shape_t shape, uint32_t *out_width, uint32_t *out_height);

void cursor_draw(int32_t x, int32_t y, cursor_shape_t shape, color_t color);

/* Releases the loaded cursor surfaces (frees the decoded pixel
 * buffers). Not required at normal kernel runtime (the cursor lives
 * for the whole session), provided for completeness / future
 * asset-reload support. */
void cursor_assets_free(void);

#endif /* CURSOR_H */
