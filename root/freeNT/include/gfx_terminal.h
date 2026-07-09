/* gfx_terminal.h - Graphical framebuffer terminal.
 *
 * Replaces VGA text-mode output with a framebuffer-backed character
 * grid, rendered via the 8x16 font (font8x16.h) scaled 2x per cell
 * (16x32 px/cell), plus a live mouse cursor (cursor.h) with
 * click-to-move-caret and click-drag text selection.
 *
 * vga_putc()/vga_write()/vga_clear()/vga_set_color()/vga_set_cursor()
 * in vga.c delegate to this module when it's active (gterm_is_active()
 * returns true), so shell.c and every other existing caller of the
 * vga_* API keeps working completely unchanged - this module is an
 * additive backend, not a replacement callers need to know about.
 */
#ifndef GFX_TERMINAL_H
#define GFX_TERMINAL_H

#include "graphics_core.h"

/* Cell size: 8x16 font glyphs at 2x scale = 16x32 px/cell. Locked in
 * after verifying this divides the target 1024x768 framebuffer with
 * zero leftover pixels (64 cols x 24 rows exactly). */
#define GTERM_FONT_SCALE   2
#define GTERM_CELL_W       (8  * GTERM_FONT_SCALE)   /* 16 */
#define GTERM_CELL_H       (16 * GTERM_FONT_SCALE)   /* 32 */

/* Initializes the terminal grid sized to whatever framebuffer
 * resolution graphics_core has (must be called after graphics_init()
 * succeeds). Returns 0 on success, -1 if graphics isn't available or
 * the grid allocation fails - callers should fall back to VGA text
 * mode on failure rather than assume this always succeeds. */
int gterm_init(void);

/* True once gterm_init() has succeeded - vga.c's delegation checks
 * this before routing output here instead of real VGA memory. */
int gterm_is_active(void);

/* ── Text output (mirrors vga.c's API shape on purpose) ─────────────── */
void gterm_putc(char c);
void gterm_write(const char *str);
void gterm_clear(void);
void gterm_set_color(uint8_t fg_index, uint8_t bg_index);
void gterm_set_cursor(uint16_t row, uint16_t col);

/* ── Mouse integration ────────────────────────────────────────────────
 * Called once per timer tick (see interrupts.c's pit_irq_handler) -
 * polls mouse_get_state(), erases the previous cursor position by
 * redrawing the text cells under it, handles click-to-move-caret and
 * click-drag selection, then draws the cursor sprite at the new
 * position. This is the standard "erase old, draw new" technique for
 * a software mouse cursor - there's no hardware cursor overlay plane
 * in this framebuffer mode, so every cursor movement genuinely
 * repaints the affected screen cells. */
void gterm_tick(void);

/* Returns 1 and copies the current selection into *out (must hold at
 * least gterm_selection_length()+1 bytes) if there's an active
 * text selection, 0 otherwise. Used by a future clipboard/paste
 * command - selection itself is tracked internally via mouse drag. */
int gterm_get_selection(char *out, uint32_t out_capacity);
uint32_t gterm_selection_length(void);
void gterm_clear_selection(void);

#endif /* GFX_TERMINAL_H */
