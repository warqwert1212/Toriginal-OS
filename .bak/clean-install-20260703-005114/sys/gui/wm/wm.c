#include "wm.h"
#include <string.h>
#include <stdlib.h>

/* ==============================================================================
 * WM.C — Window Manager Implementation
 *
 * Z-order: windows[0..count-1] stored back-to-front. Hit testing searches
 * from the end (topmost) downward. Raising a window moves it to the end.
 *
 * Drag/resize: track which part of the window border the user clicked, then
 * respond to mouse move events by either moving the window or resizing it.
 * ============================================================================== */

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
    int32_t     start_x, start_y;     /* mouse position at start of drag */
    int32_t     start_wx, start_wy;   /* window position at start */
    int32_t     start_ww, start_wh;   /* window size at start */
} drag_state_t;

typedef struct {
    wm_window_t windows[WM_MAX_WINDOWS];
    int         count;                 /* how many windows are allocated */
    uint32_t    next_id;               /* next window ID to assign */
    
    int32_t     screen_w, screen_h;
    uint32_t    focus_window;          /* currently focused window ID (0 if none) */
    
    drag_state_t drag;                 /* current drag/resize operation */
} wm_state_t;

static wm_state_t g_wm = {0};

/* ─ Initialization ──────────────────────────────────────────────────────── */

void wm_init(int32_t screen_w, int32_t screen_h) {
    memset(&g_wm, 0, sizeof(g_wm));
    g_wm.screen_w = screen_w;
    g_wm.screen_h = screen_h;
    g_wm.next_id = 1;
    g_wm.drag.type = DRAG_NONE;
}

void wm_shutdown(void) {
    for (int i = 0; i < g_wm.count; i++) {
        if (g_wm.windows[i].pixels) {
            free(g_wm.windows[i].pixels);
        }
    }
    memset(&g_wm, 0, sizeof(g_wm));
}

/* ─ Window list access ──────────────────────────────────────────────────── */

void wm_get_window_list(wm_window_t **out_list, int *out_count) {
    *out_list = g_wm.windows;
    *out_count = g_wm.count;
}

/* ─ Find window by ID ──────────────────────────────────────────────────── */

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

/* ─ Window creation ────────────────────────────────────────────────────── */

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
    
    /* Compute client area */
    wm_decorate_bounds(x, y, w, h, &win->client_x, &win->client_y,
                       &win->client_w, &win->client_h);
    
    /* Allocate pixel buffer for client area */
    win->pitch = win->client_w * 4;  /* ARGB8888 = 4 bytes per pixel */
    win->pixels = (uint32_t *)malloc(win->pitch * win->client_h);
    if (!win->pixels) {
        return 0;  /* malloc failed */
    }
    
    /* Clear to transparent black */
    memset(win->pixels, 0, win->pitch * win->client_h);
    
    if (title) {
        strncpy(win->title, title, sizeof(win->title) - 1);
        win->title[sizeof(win->title) - 1] = '\0';
    }
    
    g_wm.count++;
    
    /* New window gets focus */
    wm_set_focus(win->id);
    
    return win->id;
}

/* ─ Window destruction ─────────────────────────────────────────────────── */

void wm_destroy_window(uint32_t window_id) {
    int idx = wm_find_window_index(window_id);
    if (idx < 0) return;
    
    wm_window_t *win = &g_wm.windows[idx];
    if (win->pixels) {
        free(win->pixels);
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

/* ─ Z-order ────────────────────────────────────────────────────────────── */

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

/* ─ Focus ──────────────────────────────────────────────────────────────── */

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

/* ─ Hit testing ────────────────────────────────────────────────────────── */

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

/* ─ Input handling ─────────────────────────────────────────────────────── */

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
            
            win->x = new_x;
            win->y = new_y;
            win->w = new_w;
            win->h = new_h;
            
            /* Update client area */
            wm_decorate_bounds(win->x, win->y, win->w, win->h,
                             &win->client_x, &win->client_y,
                             &win->client_w, &win->client_h);
        }
        
        return g_wm.drag.window_id;
    }
    
    /* Not dragging: just find what's under the cursor (for cursor change, etc.) */
    wm_window_t *win = wm_window_at_point(x, y);
    return win ? win->id : 0;
}

uint32_t wm_handle_mouse_down(int32_t x, int32_t y, int button) {
    if (button != 0) {
        return 0;  /* only handle left button for now */
    }
    
    /* Find the window under the cursor */
    wm_window_t *win = wm_window_at_point(x, y);
    if (!win) {
        return 0;
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
    /* Dispatch to focused window — would pass to an event queue later */
}
