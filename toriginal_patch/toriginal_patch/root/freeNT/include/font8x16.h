/* font8x16.h - 8x16 bitmap font, ASCII 32-126.
 *
 * See font8x16.c for how this data was generated and verified (every
 * glyph rendered as ASCII art and visually checked before conversion
 * to byte values - this is not transcribed from an external font
 * file, so gaps outside 32-126 are legitimately blank rather than
 * missing data from some larger source).
 */
#ifndef FONT8X16_H
#define FONT8X16_H

#include "graphics_core.h"

#define FONT8X16_WIDTH        8
#define FONT8X16_HEIGHT       16
#define FONT8X16_NUM_GLYPHS   256
#define FONT8X16_FIRST_PRINT  32
#define FONT8X16_LAST_PRINT   126

/* font8x16_data[c][row] - one byte per scanline, MSB = leftmost
 * pixel. Codepoints outside [32,126] are all-zero (blank cell). */
extern const uint8_t font8x16_data[FONT8X16_NUM_GLYPHS][FONT8X16_HEIGHT];

/* Draws glyph `c` at framebuffer pixel (x,y) (top-left of the cell),
 * scaled by `scale` in both dimensions (e.g. scale=2 draws a 16x32
 * cell from the 8x16 source data - see gfx_terminal.c's FONT_SCALE).
 * fg is used for set bits, bg for unset bits; if bg_transparent is
 * nonzero, unset bits are skipped entirely (useful for overlaying
 * text on top of existing content) rather than drawn as bg. Goes
 * through graphics_core's graphics_draw_pixel(), so it's bounds-safe
 * and clips at the framebuffer edge automatically. */
void font_draw_glyph(uint32_t x, uint32_t y, char c, color_t fg, color_t bg,
                     int bg_transparent, uint32_t scale);

#endif /* FONT8X16_H */
