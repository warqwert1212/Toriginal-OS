/* wm.h - Bare-metal window manager: window create/destroy/z-order/
 * focus/drag/resize, plus hit-testing for mouse input.
 *
 * Ported from root/sys/gui/wm/wm.c (the SDL-hosted desktop prototype)
 * with zero logic changes beyond swapping malloc/free for kmalloc/
 * kfree (see wm.c) - the prototype's WM was already written against
 * plain <string.h>/<stdlib.h> with no SDL calls anywhere in it, so
 * every hit-test/drag/z-order rule here is byte-for-byte the same
 * behavior that was already designed and tested in the prototype.
 *
 * A window's `pixels` buffer is a plain heap-allocated ARGB8888
 * array - the compositor (compositor.c, new for this kernel; the
 * SDL prototype's compositor.c is a separate, not-ported, Aero-glass-
 * blur implementation this kernel doesn't have yet) wraps it in a
 * gfx2d_surface_t view and blits it to the real screen framebuffer
 * via graphics_2d.h, so wm.c itself never needs to know graphics_core
 * or graphics_2d exist at all - same separation of concerns the
 * prototype had between wm.c and compositor.c, just with a different,
 * simpler compositor on this side. */
#ifndef _WM_H
#define _WM_H

#include <stdint.h>

/* - Window flags - */
#define WM_WIN_VISIBLE    0x01
#define WM_WIN_FOCUSED    0x02
#define WM_WIN_DECORATED  0x04  /* has title bar and borders */
#define WM_WIN_MINIMIZED  0x08  /* hidden from compositing, still exists/tracked */
#define WM_WIN_MAXIMIZED  0x10  /* filling the maximize area wm_init() was told about */

/* - Window struct - */

typedef struct {
    uint32_t  id;               /* unique window ID (never 0) */
    uint32_t  flags;            /* WM_WIN_* */

    /* Position and size in screen space */
    int32_t   x, y;             /* top-left corner of the decorated window */
    int32_t   w, h;             /* width/height including decorations */

    /* Saved bounds from before WM_WIN_MAXIMIZED was set, so a second
     * click on the maximize button restores exactly where/what size
     * the window was - not just "unmaximized to some default". Only
     * meaningful while WM_WIN_MAXIMIZED is set. */
    int32_t   restore_x, restore_y, restore_w, restore_h;

    /* Client area (title bar + borders subtracted) */
    int32_t   client_x, client_y;
    int32_t   client_w, client_h;

    /* Window content: 32-bit ARGB pixels, only the client area */
    uint32_t *pixels;           /* allocated by WM on create */
    int32_t   pitch;            /* bytes per row (usually client_w * 4) */

    char      title[64];

    /* Opaque app data (WM doesn't touch this) - reserved for a future
     * process<->window binding (which process owns this window, so
     * input can be routed and the window destroyed on process exit) -
     * not implemented yet (desktop.c's windows today are all demo
     * content painted directly by the desktop loop, not owned by a
     * separate process - see desktop.c's paint_demo_window_content()
     * comment), kept as a field now so adding that binding later
     * doesn't need an ABI change to wm_window_t. */
    void     *app_private;
} wm_window_t;

/* - Public interface - */

/* Call once at startup. Sets screen dimensions so WM can clamp windows. */
void wm_init(int32_t screen_w, int32_t screen_h);

/* Shutdown - frees all windows. */
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

/* - Minimize / maximize / restore -
 *
 * Real state transitions, not just chrome decoration: a minimized
 * window is skipped by the compositor (see desktop.c's draw loop,
 * which checks WM_WIN_MINIMIZED before calling draw_window()) but
 * stays in the window list and keeps its pixel buffer - restoring it
 * doesn't recreate anything. A maximized window has its pre-maximize
 * bounds saved in restore_x/y/w/h so wm_restore_window() puts it back
 * exactly where it was, not at some default size. */
void wm_set_maximize_area(int32_t x, int32_t y, int32_t w, int32_t h);
void wm_minimize_window(uint32_t window_id);
void wm_maximize_window(uint32_t window_id);
void wm_restore_window(uint32_t window_id);  /* un-minimizes OR un-maximizes, whichever is set */

/* - Input handling - */

/* Mouse move/click. WM performs hit testing, focus changes, drag/resize.
 * Returns the window ID that "handled" the event (usually topmost under cursor),
 * or 0 if no window was hit.
 *
 * button: 0 = left, 1 = right. wm_handle_mouse_down() with button==1
 * does NOT drag/resize/raise (right-click is a context-menu gesture,
 * not a window-manipulation gesture on any desktop this was modeled
 * after) - it still returns the hit window ID so the caller (see
 * wm_app_binding.c's right-click handling) knows what was
 * right-clicked, but leaves drag state untouched. This is new versus
 * the prototype, which only ever checked `button != 0` and silently
 * ignored right-clicks entirely - see the comment on
 * wm_handle_mouse_down() in wm.c for why. desktop.c is the current
 * caller that uses the returned window ID to decide which context
 * menu to show. */
uint32_t wm_handle_mouse_move(int32_t x, int32_t y);
uint32_t wm_handle_mouse_down(int32_t x, int32_t y, int button);
uint32_t wm_handle_mouse_up(int32_t x, int32_t y, int button);

/* Keyboard event. Dispatched to focused window. */
void wm_handle_key(int keycode, int is_press);

/* - Utility: compute client area from decorated window bounds - */

/* The WM reserves N pixels for decorations (title bar height, border widths).
 * Decorator needs to know this too (see compositor.c). */
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
