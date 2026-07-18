/* wm.c - Bare-metal window manager.
 *
 * Ported from root/sys/gui/wm/wm.c (see wm.h's header comment for the
 * full porting rationale). Every hit-test/drag/z-order function below
 * is unchanged from the prototype except:
 *   1. malloc/free -> kmalloc/kfree (heap.h) - the kernel's actual
 *      dynamic allocator, same contract (NULL on failure).
 *   2. <string.h>/<stdlib.h> -> the kernel's own string.h.
 *   3. wm_handle_mouse_down() now accepts button==1 (right-click) and
 *      returns the hit window ID WITHOUT starting a drag/resize or
 *      stealing focus from whatever's currently focused - previously
 *      right-clicks were unconditionally ignored ("only handle left
 *      button for now"), which is the reason a right-click context
 *      menu could never work: the WM was silently eating the event
 *      one layer below where desktop.c's menu code could ever see
 *      it. Right-click still needs SOME window-manager-level answer
 *      ("what, if anything, did the user right-click on") even though
 *      it doesn't move/resize/focus anything, because the desktop's
 *      context menu content depends on whether the click landed on a
 *      window (show "Close window") or empty desktop (show "New
 *      terminal", "Refresh").
 */
#include "wm.h"
#include "string.h"
#include "heap.h"

#define WM_MAX_WINDOWS 64

typedef enum {
    DRAG_NONE = 0,
    DRAG_TITLEBAR,
    DRAG_BORDER_LEFT,
    DRAG_BORDER_RIGHT,
    DRAG_BORDER_TOP,
    DRAG_BORDER_BOTTOM,
    DRAG_CORNER_TL,
    DRAG_CORNER_TR,
    DRAG_CORNER_BL,
    DRAG_CORNER_BR,
} drag_type_t;

typedef struct {
    drag_type_t type;
    uint32_t    window_id;
    int32_t     start_x, start_y;
    int32_t     start_wx, start_wy;
    int32_t     start_ww, start_wh;
} drag_state_t;

typedef struct {
    wm_window_t windows[WM_MAX_WINDOWS];
    int         count;                 /* how many windows are allocated */
    uint32_t    next_id;               /* next window ID to assign */

    int32_t     screen_w, screen_h;
    uint32_t    focus_window;          /* currently focused window ID (0 if none) */

    /* Usable area for a maximized window - defaults to the full
     * screen in wm_init(), narrowed by wm_set_maximize_area() once
     * the caller knows about screen furniture (desktop.c's taskbar)
     * that a maximized window shouldn't cover. */
    int32_t     maximize_x, maximize_y, maximize_w, maximize_h;

    drag_state_t drag;                 /* current drag/resize operation */
} wm_state_t;

static wm_state_t g_wm = {0};

/* - Initialization - */

void wm_init(int32_t screen_w, int32_t screen_h) {
    memset(&g_wm, 0, sizeof(g_wm));
    g_wm.screen_w = screen_w;
    g_wm.screen_h = screen_h;
    g_wm.next_id = 1;
    g_wm.drag.type = DRAG_NONE;

    /* Sane default until wm_set_maximize_area() narrows it - a caller
     * that never calls it still gets correct (if taskbar-covering)
     * maximize behavior instead of an uninitialized zero-size area. */
    g_wm.maximize_x = 0;
    g_wm.maximize_y = 0;
    g_wm.maximize_w = screen_w;
    g_wm.maximize_h = screen_h;
}

void wm_set_maximize_area(int32_t x, int32_t y, int32_t w, int32_t h) {
    g_wm.maximize_x = x;
    g_wm.maximize_y = y;
    g_wm.maximize_w = w;
    g_wm.maximize_h = h;
}

void wm_shutdown(void) {
    for (int i = 0; i < g_wm.count; i++) {
        if (g_wm.windows[i].pixels) {
            kfree(g_wm.windows[i].pixels);
        }
    }
    memset(&g_wm, 0, sizeof(g_wm));
}

/* - Window list access - */

void wm_get_window_list(wm_window_t **out_list, int *out_count) {
    *out_list = g_wm.windows;
    *out_count = g_wm.count;
}

/* - Find window by ID - */

static int wm_find_window_index(uint32_t window_id) {
    for (int i = 0; i < g_wm.count; i++) {
        if (g_wm.windows[i].id == window_id) {
            return i;
        }
    }
    return -1;
}

wm_window_t *wm_get_window(uint32_t window_id) {
    int idx = wm_find_window_index(window_id);
    return (idx >= 0) ? &g_wm.windows[idx] : NULL;
}

/* FIX: neither the ported prototype's drag-resize path nor a naive
 * maximize implementation ever reallocated win->pixels when
 * client_w/client_h changed - only wm_create_window() allocated it,
 * once, at the window's initial size. Growing a window (by dragging
 * a border, or by maximizing, which is really just "resize to a big
 * new size") would then blit/paint into a buffer smaller than
 * client_w*client_h claims, reading and writing past the allocation.
 * This is the single choke point every size-changing code path below
 * now goes through: it reallocates (kmalloc + kfree the old buffer)
 * whenever the new client area is larger than what's currently
 * allocated, and clears the buffer either way so shrink-then-grow
 * doesn't show stale pixels from an unrelated previous size. Returns
 * 0 and leaves the window at its old size/buffer if the reallocation
 * fails, rather than leaving win->client_w/h claiming a size the
 * buffer doesn't actually have. */
static int wm_window_resize_buffer(wm_window_t *win, int32_t new_client_w, int32_t new_client_h) {
    int32_t new_pitch = new_client_w * 4;
    uint64_t new_size = (uint64_t)new_pitch * (uint64_t)new_client_h;
    uint64_t old_size = (uint64_t)win->pitch * (uint64_t)win->client_h;

    if (new_size > old_size || !win->pixels) {
        uint32_t *new_pixels = (uint32_t *)kmalloc((size_t)new_size);
        if (!new_pixels) return 0; /* allocation failed - caller must not apply the resize */
        if (win->pixels) kfree(win->pixels);
        win->pixels = new_pixels;
    }

    memset(win->pixels, 0, (size_t)new_size);
    win->pitch = new_pitch;
    win->client_w = new_client_w;
    win->client_h = new_client_h;
    return 1;
}

/* - Window creation - */

uint32_t wm_create_window(int32_t x, int32_t y, int32_t w, int32_t h,
                          const char *title) {
    if (g_wm.count >= WM_MAX_WINDOWS) {
        return 0;  /* window limit reached */
    }

    /* Clamp to screen bounds */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > g_wm.screen_w) x = g_wm.screen_w - w;
    if (y + h > g_wm.screen_h) y = g_wm.screen_h - h;
    if (w < 100) w = 100;
    if (h < 100) h = 100;

    wm_window_t *win = &g_wm.windows[g_wm.count];
    win->id = g_wm.next_id++;
    win->flags = WM_WIN_VISIBLE | WM_WIN_DECORATED;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->pixels = NULL;
    win->pitch = 0;
    win->client_w = 0;
    win->client_h = 0;

    /* Compute client area */
    int32_t cx, cy, cw, ch;
    wm_decorate_bounds(x, y, w, h, &cx, &cy, &cw, &ch);
    win->client_x = cx;
    win->client_y = cy;

    if (!wm_window_resize_buffer(win, cw, ch)) {
        return 0;  /* kmalloc failed */
    }

    if (title) {
        strncpy(win->title, title, sizeof(win->title) - 1);
        win->title[sizeof(win->title) - 1] = '\0';
    }

    win->app_private = NULL;

    g_wm.count++;

    /* New window gets focus */
    wm_set_focus(win->id);

    return win->id;
}

/* - Window destruction - */

void wm_destroy_window(uint32_t window_id) {
    int idx = wm_find_window_index(window_id);
    if (idx < 0) return;

    wm_window_t *win = &g_wm.windows[idx];
    if (win->pixels) {
        kfree(win->pixels);
        win->pixels = NULL;
    }

    /* Remove from list by shifting later windows down */
    for (int i = idx; i < g_wm.count - 1; i++) {
        g_wm.windows[i] = g_wm.windows[i + 1];
    }
    g_wm.count--;

    /* If we just destroyed the focused window, move focus to the topmost */
    if (g_wm.focus_window == window_id) {
        g_wm.focus_window = (g_wm.count > 0) ? g_wm.windows[g_wm.count - 1].id : 0;
    }
}

/* - Z-order - */

void wm_raise_window(uint32_t window_id) {
    int idx = wm_find_window_index(window_id);
    if (idx < 0 || idx == g_wm.count - 1) {
        return;  /* already topmost, or not found */
    }

    /* Move to the end (topmost) */
    wm_window_t temp = g_wm.windows[idx];
    for (int i = idx; i < g_wm.count - 1; i++) {
        g_wm.windows[i] = g_wm.windows[i + 1];
    }
    g_wm.windows[g_wm.count - 1] = temp;
}

/* - Focus - */

void wm_set_focus(uint32_t window_id) {
    if (wm_find_window_index(window_id) < 0 && window_id != 0) {
        return;  /* not found */
    }

    /* Clear old focus flag */
    for (int i = 0; i < g_wm.count; i++) {
        g_wm.windows[i].flags &= ~WM_WIN_FOCUSED;
    }

    /* Set new focus */
    g_wm.focus_window = window_id;
    if (window_id != 0) {
        wm_window_t *win = wm_get_window(window_id);
        if (win) {
            win->flags |= WM_WIN_FOCUSED;
        }
    }
}

uint32_t wm_get_focus(void) {
    return g_wm.focus_window;
}

/* - Minimize / maximize / restore - */

void wm_minimize_window(uint32_t window_id) {
    wm_window_t *win = wm_get_window(window_id);
    if (!win) return;

    win->flags |= WM_WIN_MINIMIZED;

    /* A minimized window can't stay focused - real desktops move
     * focus to whatever's now topmost among the still-visible
     * windows, they don't leave a hidden window "focused" with
     * nothing on screen showing it. */
    if (g_wm.focus_window == window_id) {
        uint32_t new_focus = 0;
        for (int i = g_wm.count - 1; i >= 0; i--) {
            if (!(g_wm.windows[i].flags & WM_WIN_MINIMIZED) &&
                g_wm.windows[i].id != window_id) {
                new_focus = g_wm.windows[i].id;
                break;
            }
        }
        wm_set_focus(new_focus);
    }
}

void wm_maximize_window(uint32_t window_id) {
    wm_window_t *win = wm_get_window(window_id);
    if (!win) return;
    if (win->flags & WM_WIN_MAXIMIZED) return; /* already maximized - no-op, use wm_restore_window() to undo */

    /* Save exact current bounds so restore puts it back precisely,
     * not at some guessed default size. */
    win->restore_x = win->x;
    win->restore_y = win->y;
    win->restore_w = win->w;
    win->restore_h = win->h;

    int32_t new_x = g_wm.maximize_x;
    int32_t new_y = g_wm.maximize_y;
    int32_t new_w = g_wm.maximize_w;
    int32_t new_h = g_wm.maximize_h;

    int32_t cx, cy, cw, ch;
    wm_decorate_bounds(new_x, new_y, new_w, new_h, &cx, &cy, &cw, &ch);

    /* Reallocate BEFORE committing the new outer bounds - if the
     * kmalloc inside wm_window_resize_buffer() fails, the window
     * must stay exactly as it was (old size, old buffer), not end up
     * maximized-looking with an undersized buffer underneath. */
    if (!wm_window_resize_buffer(win, cw, ch)) return;

    win->x = new_x;
    win->y = new_y;
    win->w = new_w;
    win->h = new_h;
    win->client_x = cx;
    win->client_y = cy;

    win->flags |= WM_WIN_MAXIMIZED;
    win->flags &= ~WM_WIN_MINIMIZED; /* maximizing implies un-minimizing */

    wm_raise_window(window_id);
    wm_set_focus(window_id);
}

void wm_restore_window(uint32_t window_id) {
    wm_window_t *win = wm_get_window(window_id);
    if (!win) return;

    if (win->flags & WM_WIN_MAXIMIZED) {
        int32_t new_x = win->restore_x;
        int32_t new_y = win->restore_y;
        int32_t new_w = win->restore_w;
        int32_t new_h = win->restore_h;

        int32_t cx, cy, cw, ch;
        wm_decorate_bounds(new_x, new_y, new_w, new_h, &cx, &cy, &cw, &ch);

        /* Restoring only ever shrinks back to the pre-maximize size,
         * so wm_window_resize_buffer() here never actually needs to
         * grow the allocation (it'll just reuse the existing, larger
         * buffer) - called anyway for the same "size and buffer
         * change together, atomically" guarantee every other path
         * gets, rather than special-casing "shrinking never fails". */
        if (!wm_window_resize_buffer(win, cw, ch)) return;

        win->x = new_x;
        win->y = new_y;
        win->w = new_w;
        win->h = new_h;
        win->client_x = cx;
        win->client_y = cy;

        win->flags &= ~WM_WIN_MAXIMIZED;
    }

    win->flags &= ~WM_WIN_MINIMIZED;

    wm_raise_window(window_id);
    wm_set_focus(window_id);
}

/* - Hit testing - */

/* Check if point (x, y) is inside the window's decorated frame. */
static int wm_point_in_window(wm_window_t *win, int32_t x, int32_t y) {
    return (x >= win->x && x < win->x + win->w &&
            y >= win->y && y < win->y + win->h);
}

/* Determine which part of the window border the point is in. */
static drag_type_t wm_classify_point(wm_window_t *win, int32_t x, int32_t y) {
    if (!wm_point_in_window(win, x, y)) {
        return DRAG_NONE;
    }

    int32_t x_rel = x - win->x;
    int32_t y_rel = y - win->y;
    int32_t edge = 5;  /* pixels from edge counts as "on edge" */

    int on_left = (x_rel < edge);
    int on_right = (x_rel >= win->w - edge);
    int on_top = (y_rel < edge);
    int on_bottom = (y_rel >= win->h - edge);
    int on_titlebar = (y_rel < WM_TITLEBAR_HEIGHT);

    /* Corners first */
    if (on_top && on_left) return DRAG_CORNER_TL;
    if (on_top && on_right) return DRAG_CORNER_TR;
    if (on_bottom && on_left) return DRAG_CORNER_BL;
    if (on_bottom && on_right) return DRAG_CORNER_BR;

    /* Edges */
    if (on_left) return DRAG_BORDER_LEFT;
    if (on_right) return DRAG_BORDER_RIGHT;
    if (on_top) return DRAG_BORDER_TOP;
    if (on_bottom) return DRAG_BORDER_BOTTOM;

    /* Title bar (and rest of client area if double-click implemented later) */
    if (on_titlebar) return DRAG_TITLEBAR;

    return DRAG_NONE;
}

/* Find topmost window under the point (search from top down). */
static wm_window_t *wm_window_at_point(int32_t x, int32_t y) {
    for (int i = g_wm.count - 1; i >= 0; i--) {
        if (wm_point_in_window(&g_wm.windows[i], x, y)) {
            return &g_wm.windows[i];
        }
    }
    return NULL;
}

/* - Input handling - */

uint32_t wm_handle_mouse_move(int32_t x, int32_t y) {
    /* If we're in a drag, update the window */
    if (g_wm.drag.type != DRAG_NONE) {
        wm_window_t *win = wm_get_window(g_wm.drag.window_id);
        if (!win) {
            g_wm.drag.type = DRAG_NONE;
            return 0;
        }

        int32_t dx = x - g_wm.drag.start_x;
        int32_t dy = y - g_wm.drag.start_y;

        if (g_wm.drag.type == DRAG_TITLEBAR) {
            /* Move the window */
            win->x = g_wm.drag.start_wx + dx;
            win->y = g_wm.drag.start_wy + dy;

            /* Clamp to screen */
            if (win->x < 0) win->x = 0;
            if (win->y < 0) win->y = 0;
            if (win->x + win->w > g_wm.screen_w) {
                win->x = g_wm.screen_w - win->w;
            }
            if (win->y + win->h > g_wm.screen_h) {
                win->y = g_wm.screen_h - win->h;
            }
        } else {
            /* Resize the window (resize logic for 8 drag types) */
            int32_t new_w = g_wm.drag.start_ww;
            int32_t new_h = g_wm.drag.start_wh;
            int32_t new_x = g_wm.drag.start_wx;
            int32_t new_y = g_wm.drag.start_wy;

            if (g_wm.drag.type == DRAG_BORDER_LEFT ||
                g_wm.drag.type == DRAG_CORNER_TL ||
                g_wm.drag.type == DRAG_CORNER_BL) {
                new_x += dx;
                new_w -= dx;
            }
            if (g_wm.drag.type == DRAG_BORDER_RIGHT ||
                g_wm.drag.type == DRAG_CORNER_TR ||
                g_wm.drag.type == DRAG_CORNER_BR) {
                new_w += dx;
            }
            if (g_wm.drag.type == DRAG_BORDER_TOP ||
                g_wm.drag.type == DRAG_CORNER_TL ||
                g_wm.drag.type == DRAG_CORNER_TR) {
                new_y += dy;
                new_h -= dy;
            }
            if (g_wm.drag.type == DRAG_BORDER_BOTTOM ||
                g_wm.drag.type == DRAG_CORNER_BL ||
                g_wm.drag.type == DRAG_CORNER_BR) {
                new_h += dy;
            }

            /* Enforce minimum size */
            if (new_w < 100) new_w = 100;
            if (new_h < 100) new_h = 100;

            int32_t cx, cy, cw, ch;
            wm_decorate_bounds(new_x, new_y, new_w, new_h, &cx, &cy, &cw, &ch);

            /* FIX: previously this only updated client_w/client_h -
             * win->pixels stayed allocated at whatever size the
             * window was FIRST created at. Dragging a border to make
             * a window bigger than its initial size meant every
             * later paint/blit read and wrote past the real
             * allocation (see wm_window_resize_buffer()'s comment).
             * Reallocate-then-commit, same as maximize/restore: if
             * the reallocation fails, the whole drag step is
             * abandoned (window stays at its last good size) rather
             * than applying new bounds the buffer doesn't support. */
            if (wm_window_resize_buffer(win, cw, ch)) {
                win->x = new_x;
                win->y = new_y;
                win->w = new_w;
                win->h = new_h;
                win->client_x = cx;
                win->client_y = cy;
            }
        }

        return g_wm.drag.window_id;
    }

    /* Not dragging: just find what's under the cursor (for cursor change, etc.) */
    wm_window_t *win = wm_window_at_point(x, y);
    return win ? win->id : 0;
}

uint32_t wm_handle_mouse_down(int32_t x, int32_t y, int button) {
    if (button != 0 && button != 1) {
        return 0;  /* only left (0) and right (1) are meaningful today */
    }

    /* Find the window under the cursor */
    wm_window_t *win = wm_window_at_point(x, y);
    if (!win) {
        return 0;
    }

    if (button == 1) {
        /* FIX: right-click used to be silently dropped here (the
         * whole function returned 0 for any button != 0), which is
         * why a right-click context menu could never be built on top
         * of this WM - the click never made it past this check. A
         * right-click still deliberately does NOT raise the window,
         * change focus, or start a drag/resize - real desktop
         * environments (Windows, GNOME, macOS) all leave window
         * stacking/focus alone on a bare right-click and let the
         * context menu itself be the only visible effect, since
         * right-click is explicitly a "ask a question about this
         * thing" gesture, not a "manipulate this thing" gesture.
         * Callers (desktop.c) use the returned window ID purely
         * to decide which context menu to show. */
        return win->id;
    }

    /* Raise it and give it focus */
    wm_raise_window(win->id);
    wm_set_focus(win->id);

    /* Classify what part of the window was clicked */
    drag_type_t click_type = wm_classify_point(win, x, y);
    if (click_type != DRAG_NONE) {
        g_wm.drag.type = click_type;
        g_wm.drag.window_id = win->id;
        g_wm.drag.start_x = x;
        g_wm.drag.start_y = y;
        g_wm.drag.start_wx = win->x;
        g_wm.drag.start_wy = win->y;
        g_wm.drag.start_ww = win->w;
        g_wm.drag.start_wh = win->h;
    }

    return win->id;
}

uint32_t wm_handle_mouse_up(int32_t x, int32_t y, int button) {
    (void)x; (void)y;  /* unused */

    if (button != 0) {
        return 0;
    }

    uint32_t was_dragging = g_wm.drag.window_id;
    g_wm.drag.type = DRAG_NONE;
    g_wm.drag.window_id = 0;

    return was_dragging;
}

void wm_handle_key(int keycode, int is_press) {
    (void)keycode; (void)is_press;
    /* Dispatch to focused window - would pass to an event queue later */
}
