/* graphics.h - Umbrella header for freeNT's graphics stack.
 *
 * Include this if you just want "graphics" and don't care about the
 * layering; it pulls in all three real layers:
 *
 *   graphics_core.h - framebuffer ownership, pixel-level primitives.
 *                     The only layer that touches g_framebuffer memory
 *                     directly.
 *   graphics_2d.h   - surfaces, clip stack, alpha blitting, clip-aware
 *                     shapes. What the terminal/GUI/WM call.
 *   graphics_3d.h   - vector/matrix math, depth buffer, software
 *                     triangle rasterizer.
 *
 * This file intentionally defines NOTHING itself - every type
 * (color_t, framebuffer_t, gfx2d_surface_t, mat4_t, ...) lives in
 * exactly one of the three headers above. Duplicating definitions
 * here (as an earlier version of this file did, before the
 * core/2d/3d split) causes redefinition errors the moment a
 * translation unit includes both this file and one of the specific
 * layer headers directly - which several kernel files now do. If you
 * need a new graphics type or function, add it to whichever layer
 * header actually owns that concept, not here.
 */
#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "graphics_core.h"
#include "graphics_2d.h"
#include "graphics_3d.h"

#endif /* GRAPHICS_H */
