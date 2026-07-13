/* gfx_terminal.c - Graphical framebuffer terminal implementation. */

#include "gfx_terminal.h"
#include "graphics_core.h"
#include "font8x16.h"
#include "cursor.h"
#include "mouse.h"
#include "heap.h"
#include "vga.h"   /* vga_color_t - reusing the same 16-color enum so
                    * gterm_set_color()'s callers (via vga.c's
                    * delegation) don't need any new color mapping. */

/* ── Cell grid state ──────────────────────────────────────────────── */

typedef struct {
    char    ch;
    uint8_t fg;   /* vga_color_t index */
    uint8_t bg;   /* vga_color_t index */
} gterm_cell_t;

static gterm_cell_t *g_grid = 0;
static uint32_t g_cols = 0;
static uint32_t g_rows = 0;
static uint32_t g_cursor_row = 0;
static uint32_t g_cursor_col = 0;
static uint8_t  g_cur_fg = VGA_LIGHT_GREY;
static uint8_t  g_cur_bg = VGA_BLACK;
static int      g_active = 0;

/* ── Mouse cursor state (for erase-old/draw-new redraw) ──────────── */

static int32_t g_last_mouse_x = -1;
static int32_t g_last_mouse_y = -1;
static int     g_have_last_mouse = 0;
static cursor_shape_t g_last_mouse_shape = CURSOR_ARROW;

/* ── Text selection state ────────────────────────────────────────── */

static int      g_selecting = 0;         /* mouse button held, actively dragging */
static uint32_t g_sel_start_row = 0, g_sel_start_col = 0;
static uint32_t g_sel_end_row = 0, g_sel_end_col = 0;
static int      g_has_selection = 0;

/* ── VGA 16-color palette -> 32-bit ARGB ─────────────────────────────
 * Standard VGA text-mode palette values, so colors chosen via the
 * existing vga_color_t enum (which shell.c and callers already use
 * through vga_set_color()) look like the same colors they'd have
 * gotten in real VGA text mode - no surprises from switching backends. */
static const color_t vga_palette[16] = {
    0xFF000000, /* VGA_BLACK        */
    0xFF0000AA, /* VGA_BLUE         */
    0xFF00AA00, /* VGA_GREEN        */
    0xFF00AAAA, /* VGA_CYAN         */
    0xFFAA0000, /* VGA_RED          */
    0xFFAA00AA, /* VGA_MAGENTA      */
    0xFFAA5500, /* VGA_BROWN        */
    0xFFAAAAAA, /* VGA_LIGHT_GREY   */
    0xFF555555, /* VGA_DARK_GREY    */
    0xFF5555FF, /* VGA_LIGHT_BLUE   */
    0xFF55FF55, /* VGA_LIGHT_GREEN  */
    0xFF55FFFF, /* VGA_LIGHT_CYAN   */
    0xFFFF5555, /* VGA_LIGHT_RED    */
    0xFFFF55FF, /* VGA_LIGHT_MAGENTA*/
    0xFFFFFF55, /* VGA_YELLOW       */
    0xFFFFFFFF, /* VGA_WHITE        */
};

static inline color_t palette_color(uint8_t index) {
    if (index > 15) index = 15; /* clamp rather than index OOB into
                                  * a stack array on a bad enum value */
    return vga_palette[index];
}

/* ── Cell <-> pixel geometry ──────────────────────────────────────── */

static inline uint32_t cell_px_x(uint32_t col) { return col * GTERM_CELL_W; }
static inline uint32_t cell_px_y(uint32_t row) { return row * GTERM_CELL_H; }

/* ── Rendering ────────────────────────────────────────────────────── */

/* Redraws exactly one cell - the only place that actually calls
 * font_draw_glyph(), so every code path that changes a cell's
 * contents (putc, clear, selection highlight, cursor-under-erase)
 * goes through the same rendering logic rather than each
 * reimplementing "draw this glyph with these colors". */
static void redraw_cell(uint32_t row, uint32_t col) {
    if (row >= g_rows || col >= g_cols) return;

    gterm_cell_t *cell = &g_grid[row * g_cols + col];

    int in_selection = 0;
    if (g_has_selection || g_selecting) {
        /* Normalize selection to (start <= end) in reading order
         * (row-major) so the highlight test below doesn't need to
         * handle "dragged backwards" as a separate case. */
        uint32_t sr = g_sel_start_row, sc = g_sel_start_col;
        uint32_t er = g_sel_end_row,   ec = g_sel_end_col;
        if (sr > er || (sr == er && sc > ec)) {
            uint32_t tr = sr, tc = sc;
            sr = er; sc = ec;
            er = tr; ec = tc;
        }

        if (row > sr && row < er) {
            in_selection = 1;
        } else if (row == sr && row == er) {
            in_selection = (col >= sc && col <= ec);
        } else if (row == sr) {
            in_selection = (col >= sc);
        } else if (row == er) {
            in_selection = (col <= ec);
        }
    }

    color_t fg = palette_color(cell->fg);
    color_t bg = palette_color(cell->bg);

    if (in_selection) {
        /* Swap fg/bg for the selection highlight - standard terminal
         * selection convention, and reusing the existing palette
         * means no new color concept is needed just for this. */
        color_t tmp = fg;
        fg = bg;
        bg = tmp;
    }

    uint32_t px = cell_px_x(col);
    uint32_t py = cell_px_y(row);

    /* Fill the full cell background first (font_draw_glyph with
     * bg_transparent=0 would also paint the background, but doing it
     * as one fill_rect covering the whole cell first, then the glyph
     * with transparent background, keeps inter-glyph background
     * consistent even for the parts of the cell the 8x16 grid doesn't
     * directly cover after 2x scaling - there is no such gap at
     * exactly 2x, but this ordering is also what makes it trivial to
     * later support things like a half-height cursor bar without
     * fighting glyph-vs-background draw order). */
    graphics_fill_rect(px, py, GTERM_CELL_W, GTERM_CELL_H, bg);
    font_draw_glyph(px, py, cell->ch ? cell->ch : ' ', fg, bg, 1, GTERM_FONT_SCALE);
}

static void redraw_all(void) {
    for (uint32_t r = 0; r < g_rows; r++) {
        for (uint32_t c = 0; c < g_cols; c++) {
            redraw_cell(r, c);
        }
    }
}

/* ── Scrolling ────────────────────────────────────────────────────── */

static void scroll_up_one_line(void) {
    /* Move every row up by one, blank the last row. This is a plain
     * memmove-style shift of the cell array - the actual pixel
     * repaint happens via redraw_all() after, since partial-row
     * framebuffer scrolling (blitting pixel rows) would need
     * graphics_core support this stage doesn't have yet (no
     * "shift screen region" primitive) and full-grid redraw at
     * 64x24 cells is cheap enough not to need it. */
    for (uint32_t r = 1; r < g_rows; r++) {
        for (uint32_t c = 0; c < g_cols; c++) {
            g_grid[(r - 1) * g_cols + c] = g_grid[r * g_cols + c];
        }
    }
    for (uint32_t c = 0; c < g_cols; c++) {
        gterm_cell_t *cell = &g_grid[(g_rows - 1) * g_cols + c];
        cell->ch = ' ';
        cell->fg = g_cur_fg;
        cell->bg = g_cur_bg;
    }
    redraw_all();
}

/* ── Public API ───────────────────────────────────────────────────── */

int gterm_init(void) {
    if (!graphics_is_available()) return -1;

    uint32_t cols = g_framebuffer.width  / GTERM_CELL_W;
    uint32_t rows = g_framebuffer.height / GTERM_CELL_H;

    if (cols == 0 || rows == 0) return -1;

    uint64_t cell_count = (uint64_t)cols * rows;
    uint64_t bytes = cell_count * sizeof(gterm_cell_t);

    /* Same defensive ceiling as the other kmalloc call sites in the
     * graphics stack (gfx2d_surface_create, gfx3d_init) - reject
     * rather than risk a wrapped size_t multiplication. At any
     * realistic framebuffer resolution this grid is a few KB, so
     * this ceiling is generous, not tight. */
    if (bytes > 0x1000000ULL) return -1;

    void *mem = kmalloc((size_t)bytes);
    if (!mem) return -1;

    g_grid = (gterm_cell_t *)mem;
    g_cols = cols;
    g_rows = rows;
    g_cursor_row = 0;
    g_cursor_col = 0;
    g_cur_fg = VGA_LIGHT_GREY;
    g_cur_bg = VGA_BLACK;

    for (uint64_t i = 0; i < cell_count; i++) {
        g_grid[i].ch = ' ';
        g_grid[i].fg = g_cur_fg;
        g_grid[i].bg = g_cur_bg;
    }

    mouse_set_bounds((int32_t)g_framebuffer.width, (int32_t)g_framebuffer.height);

    g_active = 1;
    redraw_all();
    return 0;
}

int gterm_is_active(void) {
    return g_active;
}

void gterm_set_color(uint8_t fg_index, uint8_t bg_index) {
    g_cur_fg = fg_index;
    g_cur_bg = bg_index;
}

void gterm_set_cursor(uint16_t row, uint16_t col) {
    if (row < g_rows) g_cursor_row = row;
    if (col < g_cols) g_cursor_col = col;
}

void gterm_clear(void) {
    if (!g_active) return;
    for (uint32_t r = 0; r < g_rows; r++) {
        for (uint32_t c = 0; c < g_cols; c++) {
            gterm_cell_t *cell = &g_grid[r * g_cols + c];
            cell->ch = ' ';
            cell->fg = g_cur_fg;
            cell->bg = g_cur_bg;
        }
    }
    g_cursor_row = 0;
    g_cursor_col = 0;
    redraw_all();
}

void gterm_putc(char c) {
    if (!g_active) return;

    switch (c) {
    case '\n':
        g_cursor_col = 0;
        g_cursor_row++;
        break;

    case '\r':
        g_cursor_col = 0;
        break;

    case '\b':
        if (g_cursor_col > 0) {
            g_cursor_col--;
        } else if (g_cursor_row > 0) {
            g_cursor_row--;
            g_cursor_col = g_cols - 1;
        } else {
            break; /* nothing to backspace over at (0,0) */
        }
        {
            gterm_cell_t *cell = &g_grid[g_cursor_row * g_cols + g_cursor_col];
            cell->ch = ' ';
            cell->fg = g_cur_fg;
            cell->bg = g_cur_bg;
            redraw_cell(g_cursor_row, g_cursor_col);
        }
        break;

    default: {
        gterm_cell_t *cell = &g_grid[g_cursor_row * g_cols + g_cursor_col];
        cell->ch = c;
        cell->fg = g_cur_fg;
        cell->bg = g_cur_bg;
        redraw_cell(g_cursor_row, g_cursor_col);

        g_cursor_col++;
        if (g_cursor_col >= g_cols) {
            g_cursor_col = 0;
            g_cursor_row++;
        }
        break;
    }
    }

    if (g_cursor_row >= g_rows) {
        scroll_up_one_line();
        g_cursor_row = g_rows - 1;
    }
}

void gterm_write(const char *str) {
    if (!str) return;
    while (*str) gterm_putc(*str++);
}

/* ── Mouse cursor + selection ─────────────────────────────────────── */

/* Converts a screen pixel coordinate to the cell it falls within,
 * clamped to the valid grid range - a click slightly past the last
 * row/col (e.g. in leftover sub-cell pixels, though at the locked
 * 1024x768/16x32 geometry there are none) still resolves to the
 * nearest real cell rather than being silently ignored. */
static void pixel_to_cell(int32_t px, int32_t py, uint32_t *out_row, uint32_t *out_col) {
    if (px < 0) px = 0;
    if (py < 0) py = 0;

    uint32_t col = (uint32_t)px / GTERM_CELL_W;
    uint32_t row = (uint32_t)py / GTERM_CELL_H;

    if (col >= g_cols) col = g_cols - 1;
    if (row >= g_rows) row = g_rows - 1;

    *out_row = row;
    *out_col = col;
}

/* Set by gterm_request_tick() (interrupt-safe), consumed by
 * gterm_poll_tick() (normal context). volatile because it's written
 * from the PIT ISR and read from ordinary kernel code. */
static volatile int g_tick_pending = 0;

void gterm_request_tick(void) {
    g_tick_pending = 1;
}

int gterm_poll_tick(void) {
    if (!g_tick_pending) return 0;
    g_tick_pending = 0;
    gterm_tick();
    return 1;
}

void gterm_tick(void) {
    if (!g_active) return;

    mouse_state_t st;
    mouse_get_state(&st);

    /* Erase the cursor sprite from its previous position by
     * repainting the text cells it overlapped - cheaper than a full-
     * grid redraw every tick, and correct because redraw_cell()
     * always repaints a cell's true contents regardless of what the
     * cursor previously drew over it. Uses cursor_get_size() rather
     * than a fixed constant because PNG-loaded cursor assets can have
     * different real dimensions per shape (and even differ between
     * boots depending on what's on disk) - there is no longer a
     * single fixed cursor size to assume. */
    if (g_have_last_mouse) {
        uint32_t last_w, last_h;
        cursor_get_size(g_last_mouse_shape, &last_w, &last_h);

        uint32_t r0, c0, r1, c1;
        pixel_to_cell(g_last_mouse_x, g_last_mouse_y, &r0, &c0);
        pixel_to_cell(g_last_mouse_x + (int32_t)last_w - 1,
                      g_last_mouse_y + (int32_t)last_h - 1, &r1, &c1);
        for (uint32_t r = r0; r <= r1 && r < g_rows; r++) {
            for (uint32_t c = c0; c <= c1 && c < g_cols; c++) {
                redraw_cell(r, c);
            }
        }
    }

    uint32_t cell_row, cell_col;
    pixel_to_cell(st.x, st.y, &cell_row, &cell_col);

    if (st.left_button) {
        if (!g_selecting) {
            /* Button just went down (or we missed the transition and
             * are catching up) - start a fresh selection anchored
             * here rather than assuming an in-progress one, so a
             * stray leftover g_sel_start_* from a previous drag never
             * bleeds into a new click. */
            g_selecting = 1;
            g_sel_start_row = cell_row;
            g_sel_start_col = cell_col;
            g_has_selection = 0;
        }
        g_sel_end_row = cell_row;
        g_sel_end_col = cell_col;

        /* Click-to-move-caret: even mid-selection, keep the text
         * cursor following the drag head - this matches how every
         * mainstream terminal/text editor treats "click and drag":
         * the caret tracks the drag point, selection is the span
         * from anchor to caret. */
        gterm_set_cursor((uint16_t)cell_row, (uint16_t)cell_col);

        /* Redraw affected region so the selection highlight updates
         * live during the drag, not just once at release. Redrawing
         * the whole grid every tick during a drag is more pixel work
         * than strictly necessary, but at 64x24 cells and 100 ticks/
         * sec this is comfortably cheap, and it guarantees the
         * highlight is never stale/wrong-shaped mid-drag, which a
         * more surgical "only touched cells" diff would risk getting
         * subtly wrong on first implementation. */
        redraw_all();
    } else if (g_selecting) {
        /* Button released: finalize the selection if it actually
         * spans more than a single cell (a plain click with no drag
         * shouldn't leave a phantom one-cell "selection" behind). */
        g_selecting = 0;
        if (g_sel_start_row != g_sel_end_row || g_sel_start_col != g_sel_end_col) {
            g_has_selection = 1;
        } else {
            g_has_selection = 0;
            gterm_set_cursor((uint16_t)cell_row, (uint16_t)cell_col);
        }
        redraw_all();
    }

    color_t cursor_color = GRAPHICS_COLOR_WHITE;
    cursor_shape_t shape = st.left_button ? CURSOR_HAND : CURSOR_ARROW;
    cursor_draw(st.x, st.y, shape, cursor_color);

    g_last_mouse_x = st.x;
    g_last_mouse_y = st.y;
    g_last_mouse_shape = shape;
    g_have_last_mouse = 1;
}

void gterm_clear_selection(void) {
    if (g_has_selection) {
        g_has_selection = 0;
        redraw_all();
    }
}

uint32_t gterm_selection_length(void) {
    if (!g_has_selection) return 0;

    uint32_t sr = g_sel_start_row, sc = g_sel_start_col;
    uint32_t er = g_sel_end_row,   ec = g_sel_end_col;
    if (sr > er || (sr == er && sc > ec)) {
        uint32_t tr = sr, tc = sc;
        sr = er; sc = ec;
        er = tr; ec = tc;
    }

    uint32_t len = 0;
    for (uint32_t r = sr; r <= er; r++) {
        uint32_t start_c = (r == sr) ? sc : 0;
        uint32_t end_c   = (r == er) ? ec : g_cols - 1;
        len += (end_c - start_c + 1);
        if (r != er) len += 1; /* newline between selected rows */
    }
    return len;
}

int gterm_get_selection(char *out, uint32_t out_capacity) {
    if (!g_has_selection || !out || out_capacity == 0) return 0;

    uint32_t sr = g_sel_start_row, sc = g_sel_start_col;
    uint32_t er = g_sel_end_row,   ec = g_sel_end_col;
    if (sr > er || (sr == er && sc > ec)) {
        uint32_t tr = sr, tc = sc;
        sr = er; sc = ec;
        er = tr; ec = tc;
    }

    uint32_t written = 0;
    for (uint32_t r = sr; r <= er && written < out_capacity - 1; r++) {
        uint32_t start_c = (r == sr) ? sc : 0;
        uint32_t end_c   = (r == er) ? ec : g_cols - 1;
        for (uint32_t c = start_c; c <= end_c && written < out_capacity - 1; c++) {
            out[written++] = g_grid[r * g_cols + c].ch;
        }
        if (r != er && written < out_capacity - 1) {
            out[written++] = '\n';
        }
    }
    out[written] = '\0';
    return 1;
}