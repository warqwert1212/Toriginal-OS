#ifndef _WM_H
#define _WM_H

#include <stdint.h>



/* ─ Window flags ────────────────────────────────────────────────────────── */
#define WM_WIN_VISIBLE    0x01
#define WM_WIN_FOCUSED    0x02
#define WM_WIN_DECORATED  0x04  /* has title bar and borders */

/* ─ Window struct ──────────────────────────────────────────────────────── */

typedef struct {
    uint32_t  id;               /* unique window ID (never 0) */
    uint32_t  flags;            /* WM_WIN_* */
    
    /* Position and size in screen space */
    int32_t   x, y;             /* top-left corner of the decorated window */
    int32_t   w, h;             /* width/height including decorations */
    
    /* Client area (title bar + borders subtracted) */
    int32_t   client_x, client_y;
    int32_t   client_w, client_h;
    
    /* Window content: 32-bit ARGB pixels, only the client area */
    uint32_t *pixels;           /* allocated by WM on create */
    int32_t   pitch;            /* bytes per row (usually client_w * 4) */
    
    char      title[64];
    
    /* Opaque app data (WM doesn't touch this) */
    void     *app_private;
} wm_window_t;

/* ─ Public interface ───────────────────────────────────────────────────── */

/* Call once at startup. Sets screen dimensions so WM can clamp windows. */
void wm_init(int32_t screen_w, int32_t screen_h);

/* Shutdown — frees all windows. */
void wm_shutdown(void);

/* Create a new window. Returns window ID (> 0) on success, 0 on failure. */
uint32_t wm_create_window(int32_t x, int32_t y, int32_t w, int32_t h,
                          const char *title);

/* Destroy a window by ID. */
void wm_destroy_window(uint32_t window_id);

/* Get window by ID. Returns NULL if not found. */
wm_window_t *wm_get_window(uint32_t window_id);

/* Get the window list (back-to-front order for compositor).
 * Returns pointer to internal array and count. DO NOT MODIFY. */
void wm_get_window_list(wm_window_t **out_list, int *out_count);

/* Raise a window to the top (make it topmost). */
void wm_raise_window(uint32_t window_id);

/* Set focus to a window. Only one window is focused at a time. */
void wm_set_focus(uint32_t window_id);

/* Get the currently focused window ID (0 if none). */
uint32_t wm_get_focus(void);

/* ─ Input handling ────────────────────────────────────────────────────── */

/* Mouse move/click. WM performs hit testing, focus changes, drag/resize.
 * Returns the window ID that "handled" the event (usually topmost under cursor),
 * or 0 if no window was hit. */
uint32_t wm_handle_mouse_move(int32_t x, int32_t y);
uint32_t wm_handle_mouse_down(int32_t x, int32_t y, int button);
uint32_t wm_handle_mouse_up(int32_t x, int32_t y, int button);

/* Keyboard event. Dispatched to focused window. */
void wm_handle_key(int keycode, int is_press);

/* ─ Utility: compute client area from decorated window bounds ─────────── */

/* The WM reserves N pixels for decorations (title bar height, border widths).
 * Decorator needs to know this too (see compositor/). */
#define WM_BORDER_WIDTH   2
#define WM_TITLEBAR_HEIGHT 20

static inline void wm_decorate_bounds(int32_t x, int32_t y, int32_t w, int32_t h,
                                      int32_t *out_client_x, int32_t *out_client_y,
                                      int32_t *out_client_w, int32_t *out_client_h) {
    *out_client_x = x + WM_BORDER_WIDTH;
    *out_client_y = y + WM_TITLEBAR_HEIGHT;
    *out_client_w = w - 2 * WM_BORDER_WIDTH;
    *out_client_h = h - WM_TITLEBAR_HEIGHT - WM_BORDER_WIDTH;
    
    /* Clamp to non-negative */
    if (*out_client_w < 1) *out_client_w = 1;
    if (*out_client_h < 1) *out_client_h = 1;
}

#endif /* _WM_H */
