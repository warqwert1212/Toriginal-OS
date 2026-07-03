#ifndef _COMPOSITOR_H
#define _COMPOSITOR_H

#include <stdint.h>

/* ==============================================================================
 * COMPOSITOR.H — Compositing Engine
 *
 * Reads the WM's window list and renders everything to a framebuffer:
 *   1. Wallpaper background
 *   2. Windows in z-order
 *   3. Title bars and window borders with Aero glass effect (blur + tint)
 *
 * The Aero glass effect:
 *   - Gaussian blur of the region behind the window frame (titlebar + borders)
 *   - Tint overlay (semi-transparent white or blue)
 *   - Bright 1px highlight along the top/left edges
 *   - Creates that translucent, glassy look from Windows Vista/7
 *
 * The compositor outputs to the platform framebuffer and handles damage
 * tracking to avoid redrawing unchanged regions.
 * ============================================================================== */

/* Initialize compositor. Call once at startup. */
void comp_init(int32_t screen_w, int32_t screen_h);

/* Shutdown compositor. */
void comp_shutdown(void);

/* Set the wallpaper bitmap (ARGB8888, must not be NULL).
 * Compositor makes a copy; caller retains ownership of the input buffer. */
void comp_set_wallpaper(uint32_t *argb_pixels, int32_t w, int32_t h);

/* Main composite pass: renders WM window list to framebuffer.
 * Called once per frame. */
void comp_composite(void);

/* Get the current framebuffer for presentation.
 * Returns pointer to ARGB8888 pixels, width, height, and pitch (bytes per row).
 * DO NOT MODIFY; valid only until next comp_composite(). */
void comp_get_framebuffer(uint32_t **out_pixels, int32_t *out_w,
                         int32_t *out_h, int32_t *out_pitch);

#endif /* _COMPOSITOR_H */
