/* desktop.c - Real windowed desktop environment.
 *
 * This REPLACES the previous desktop_run() (a dead-end icon screen
 * where both icons just quit back to the shell - see git history /
 * the .bak snapshots for that version). This version is backed by a
 * genuine window manager (wm.h - ported from the SDL-hosted
 * prototype at root/sys/gui/wm/, see wm.h's header comment) and a
 * right-click context menu (desktop_menu.h - ported from
 * root/sys/gui/desktop_env/desktop_menu.c), wired together with a
 * simple compositor that lives right here in this file rather than
 * as a separate compositor.c: window chrome drawing (titlebar,
 * borders, close button) plus the actual per-frame blit of each
 * window's client-area pixels onto the real screen framebuffer via
 * graphics_2d.h.
 *
 * Deliberately NOT ported: the SDL prototype's compositor.c Aero-
 * glass blur/tint effect. That's a real, separate rendering feature
 * (Gaussian blur of the framebuffer region behind a window frame)
 * that doesn't exist anywhere in this bare-metal kernel today and
 * would be its own substantial piece of work - flat-colored chrome
 * here is the honest "simple, but it works" version, not a
 * placeholder secretly standing in for glass that was actually
 * built.
 *
 * Runs its own event loop exactly like the previous desktop_run()
 * did: polls keyboard + PS/2 mouse directly, throttled to ~30fps via
 * pit_get_milliseconds(), restores the text terminal on exit.
 */
#include "desktop.h"
#include "graphics_core.h"
#include "graphics_2d.h"
#include "font8x16.h"
#include "cursor.h"
#include "mouse.h"
#include "keyboard.h"
#include "pit.h"
#include "rtc.h"
#include "gfx_terminal.h"
#include "io.h"
#include "string.h"
#include "wm.h"
#include "desktop_menu.h"
#include "process.h"   /* process_create/process_start - launching terminal.trp/settings.trp for real */
#include "loader.h"     /* loader_load_executable */
#include "fs.h"         /* fs_stat - check the file actually exists before trying to run it */

#define TASKBAR_H       32
#define CHROME_BTN_SIZE 16
#define CHROME_BTN_GAP  4

/* On-screen feedback for menu actions that can fail (launching a
 * .trp that isn't on disk) - there's no visible console while the
 * desktop owns the whole screen, so silently failing would look
 * exactly like the button doing nothing at all. Shown in the
 * taskbar for a couple seconds, then cleared automatically. */
static char     g_status_msg[128] = {0};
static uint64_t g_status_msg_until_ms = 0;

static void set_status_message(const char *msg, uint64_t now_ms) {
    strncpy(g_status_msg, msg, sizeof(g_status_msg) - 1);
    g_status_msg[sizeof(g_status_msg) - 1] = '\0';
    g_status_msg_until_ms = now_ms + 3000;
}

/* ── Window chrome colors ─────────────────────────────────────────── */
static color_t chrome_titlebar_focused(void)   { return graphics_rgb(0x2A, 0x5C, 0x8A); }
static color_t chrome_titlebar_unfocused(void) { return graphics_rgb(0x50, 0x50, 0x54); }
static color_t chrome_border(void)             { return graphics_rgb(0x18, 0x18, 0x1A); }
static color_t chrome_close_btn(void)          { return graphics_rgb(0xC0, 0x30, 0x30); }
static color_t chrome_minmax_btn(void)         { return graphics_rgb(0x48, 0x48, 0x4C); }

/* ── Small text helper (mirrors the previous desktop.c's draw_string,
 * kept for the taskbar clock / hints - window chrome text below uses
 * font_draw_glyph directly since it needs per-glyph positioning for
 * the titlebar's fixed layout). ────────────────────────────────────*/
static void draw_string(uint32_t x, uint32_t y, const char *s, color_t fg,
                        color_t bg, int transparent, uint32_t scale) {
    uint32_t cx = x;
    for (const char *p = s; *p; p++) {
        font_draw_glyph(cx, y, *p, fg, bg, transparent, scale);
        cx += FONT8X16_WIDTH * scale;
    }
}

static void draw_two_digit(uint32_t x, uint32_t y, uint8_t v, color_t fg, color_t bg) {
    char buf[3];
    buf[0] = (char)('0' + (v / 10) % 10);
    buf[1] = (char)('0' + v % 10);
    buf[2] = '\0';
    draw_string(x, y, buf, fg, bg, 0, 1);
}

static int point_in_rect(int32_t px, int32_t py, int32_t x, int32_t y,
                         int32_t w, int32_t h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

/* ── Window content demo ─────────────────────────────────────────────
 * wm_create_window() gives every new window a blank (transparent
 * black) client-area buffer - something has to actually paint into
 * it or every window looks like a hole in the desktop. This kernel
 * doesn't yet have a real windowed app running inside one of these
 * (see term.c's header comment: the real terminal app runs full-
 * screen today, not inside a WM window, since gterm is a single
 * global grid, not a multi-instance one) - so each demo window gets
 * a simple, distinct solid-color fill plus its window ID as text,
 * which is enough to prove multiple independently-movable, resizable,
 * closable windows genuinely work, without pretending a real app is
 * running inside them yet. */
static void paint_demo_window_content(wm_window_t *win) {
    color_t fill = graphics_rgb(
        (uint8_t)(40 + (win->id * 47) % 160),
        (uint8_t)(40 + (win->id * 91) % 160),
        (uint8_t)(40 + (win->id * 137) % 160));

    for (int32_t y = 0; y < win->client_h; y++) {
        for (int32_t x = 0; x < win->client_w; x++) {
            win->pixels[y * win->client_w + x] = fill;
        }
    }
}

/* Titlebar button layout: close is rightmost, then maximize, then
 * minimize, matching the common left-to-right reading order of
 * "least destructive to most destructive" being closest to the
 * pointer's natural rest position - this is a convention choice, not
 * a technical requirement, but it's the one most desktops settled on
 * and there's no reason to invent a different one here. */
static void chrome_button_rects(wm_window_t *win, gfx2d_rect_t *out_close,
                                gfx2d_rect_t *out_maximize, gfx2d_rect_t *out_minimize) {
    int32_t y = win->y + (WM_TITLEBAR_HEIGHT - CHROME_BTN_SIZE) / 2;
    int32_t right_edge = win->x + win->w - WM_BORDER_WIDTH - CHROME_BTN_GAP;

    int32_t close_x = right_edge - CHROME_BTN_SIZE;
    int32_t max_x    = close_x - CHROME_BTN_GAP - CHROME_BTN_SIZE;
    int32_t min_x    = max_x - CHROME_BTN_GAP - CHROME_BTN_SIZE;

    out_close->x = close_x;    out_close->y = y;
    out_close->w = CHROME_BTN_SIZE; out_close->h = CHROME_BTN_SIZE;

    out_maximize->x = max_x;   out_maximize->y = y;
    out_maximize->w = CHROME_BTN_SIZE; out_maximize->h = CHROME_BTN_SIZE;

    out_minimize->x = min_x;   out_minimize->y = y;
    out_minimize->w = CHROME_BTN_SIZE; out_minimize->h = CHROME_BTN_SIZE;
}

/* ── Chrome + blit: composites one window onto the screen ──────────── */
static void draw_window(wm_window_t *win) {
    int focused = (win->flags & WM_WIN_FOCUSED) != 0;
    color_t titlebar = focused ? chrome_titlebar_focused() : chrome_titlebar_unfocused();
    color_t border   = chrome_border();

    gfx2d_rect_t frame = { win->x, win->y, (uint32_t)win->w, (uint32_t)win->h };
    gfx2d_fill_rect(frame, border);

    gfx2d_rect_t bar = { win->x + WM_BORDER_WIDTH, win->y,
                          (uint32_t)(win->w - 2 * WM_BORDER_WIDTH),
                          (uint32_t)WM_TITLEBAR_HEIGHT };
    gfx2d_fill_rect(bar, titlebar);

    uint32_t text_y = (uint32_t)(win->y + (WM_TITLEBAR_HEIGHT - FONT8X16_HEIGHT) / 2);
    uint32_t text_x = (uint32_t)(win->x + WM_BORDER_WIDTH + 6);
    for (const char *p = win->title; *p; p++) {
        font_draw_glyph(text_x, text_y, *p, GRAPHICS_COLOR_WHITE, titlebar, 1, 1);
        text_x += FONT8X16_WIDTH;
    }

    gfx2d_rect_t close_rect, max_rect, min_rect;
    chrome_button_rects(win, &close_rect, &max_rect, &min_rect);

    gfx2d_fill_rect(close_rect, chrome_close_btn());
    font_draw_glyph((uint32_t)close_rect.x + 4, (uint32_t)close_rect.y, 'x',
                     GRAPHICS_COLOR_WHITE, chrome_close_btn(), 1, 1);

    gfx2d_fill_rect(max_rect, chrome_minmax_btn());
    /* '+' when not maximized (click to maximize), a small box glyph
     * substitute ('=') when already maximized (click to restore) -
     * real desktops draw two overlapping squares for "restore"; a
     * plain glyph is the honest simple version rather than hand-
     * drawing a second icon shape pixel-by-pixel for one button. */
    font_draw_glyph((uint32_t)max_rect.x + 4, (uint32_t)max_rect.y,
                     (win->flags & WM_WIN_MAXIMIZED) ? '=' : '+',
                     GRAPHICS_COLOR_WHITE, chrome_minmax_btn(), 1, 1);

    gfx2d_fill_rect(min_rect, chrome_minmax_btn());
    font_draw_glyph((uint32_t)min_rect.x + 4, (uint32_t)min_rect.y, '_',
                     GRAPHICS_COLOR_WHITE, chrome_minmax_btn(), 1, 1);

    /* Client area: blit the window's own pixel buffer. wm_window_t's
     * pixels/client_w/client_h are exactly the shape gfx2d_surface_t
     * expects (see wm.h's header comment), so this is a zero-copy
     * view, not a duplicate buffer. */
    gfx2d_surface_t view;
    view.width  = (uint32_t)win->client_w;
    view.height = (uint32_t)win->client_h;
    view.pixels = (color_t *)win->pixels;
    gfx2d_blit(&view, win->client_x, win->client_y);
}

/* Which button (if any) is at screen point (x,y). Returns 'c' (close),
 * 'M' (maximize), 'm' (minimize), or 0 (none) - checked in that order
 * since the buttons don't overlap, order doesn't affect correctness,
 * just avoids three separate point_in_rect() call sites at every use. */
static char chrome_button_at(wm_window_t *win, int32_t x, int32_t y) {
    gfx2d_rect_t close_rect, max_rect, min_rect;
    chrome_button_rects(win, &close_rect, &max_rect, &min_rect);

    if (point_in_rect(x, y, close_rect.x, close_rect.y, (int32_t)close_rect.w, (int32_t)close_rect.h)) return 'c';
    if (point_in_rect(x, y, max_rect.x, max_rect.y, (int32_t)max_rect.w, (int32_t)max_rect.h)) return 'M';
    if (point_in_rect(x, y, min_rect.x, min_rect.y, (int32_t)min_rect.w, (int32_t)min_rect.h)) return 'm';
    return 0;
}

/* ── Right-click context menu actions ───────────────────────────────
 * menu_action_fn's `ctx` carries whatever the menu was opened
 * against - for the desktop-empty-space menu that's NULL (no window
 * involved); for the window-titlebar menu it's the wm_window_t* that
 * was right-clicked, passed at the menu_handle_click() call site.
 *
 * "New Terminal" and "Settings" do NOT spawn a fake built-in window -
 * they attempt to actually load and run terminal.trp / settings.trp
 * from disk, the exact same way the shell's own `run <path>` command
 * does (see root/sys/shell/shell.c's cmd_run(): process_create() then
 * loader_load_executable() then process_start()). Neither .trp
 * exists on disk yet (root/apps/term/term.c is real source but isn't
 * built into a .trp and installed onto the filesystem image as part
 * of this change - that's a packaging/build step, not a kernel-side
 * one), so today both buttons will genuinely report "not found" via
 * set_status_message() rather than silently doing nothing or opening
 * a placeholder window pretending to be a real app. The moment a real
 * terminal.trp/settings.trp is placed on disk, these start working
 * with zero changes needed here. */
static uint64_t g_now_ms_for_menu_actions = 0; /* set once per frame before dispatching a menu click */

static void launch_trp(const char *path) {
    inode_t st;
    if (fs_stat(path, &st) != 0) {
        char msg[128];
        strncpy(msg, path, sizeof(msg) - 1);
        msg[sizeof(msg) - 1] = '\0';
        /* Keep it short and honest: which file, and that it's
         * missing - not a stack trace, not an errno, just the fact
         * a real user clicking this button needs. */
        size_t len = strlen(msg);
        if (len + 11 < sizeof(msg)) {
            strncpy(msg + len, ": not found", sizeof(msg) - len - 1);
        }
        set_status_message(msg, g_now_ms_for_menu_actions);
        return;
    }

    process_t *proc = process_create(path, 1);
    if (!proc) {
        set_status_message("failed to create process", g_now_ms_for_menu_actions);
        return;
    }
    if (loader_load_executable(path, proc->pid) == 0) {
        process_start(proc->pid);
        char msg[128];
        strncpy(msg, "launched ", sizeof(msg) - 1);
        size_t len = strlen(msg);
        strncpy(msg + len, path, sizeof(msg) - len - 1);
        msg[sizeof(msg) - 1] = '\0';
        set_status_message(msg, g_now_ms_for_menu_actions);
    } else {
        char msg[128];
        strncpy(msg, path, sizeof(msg) - 1);
        size_t len = strlen(msg);
        if (len + 18 < sizeof(msg)) {
            strncpy(msg + len, ": loader rejected it", sizeof(msg) - len - 1);
        }
        msg[sizeof(msg) - 1] = '\0';
        set_status_message(msg, g_now_ms_for_menu_actions);
    }
}

static void action_new_terminal(void *ctx) {
    (void)ctx;
    launch_trp("/terminal.trp");
}

static void action_settings(void *ctx) {
    (void)ctx;
    launch_trp("/settings.trp");
}

static void action_close_window(void *ctx) {
    wm_window_t *win = (wm_window_t *)ctx;
    if (win) wm_destroy_window(win->id);
}

static void action_minimize_window(void *ctx) {
    wm_window_t *win = (wm_window_t *)ctx;
    if (win) wm_minimize_window(win->id);
}

static void action_maximize_window(void *ctx) {
    wm_window_t *win = (wm_window_t *)ctx;
    if (!win) return;
    if (win->flags & WM_WIN_MAXIMIZED) wm_restore_window(win->id);
    else wm_maximize_window(win->id);
}

/* ── Main loop ────────────────────────────────────────────────────── */

void desktop_run(void) {
    if (!graphics_is_available()) {
        io_put_string("desktop: no graphics framebuffer available; use the shell instead.\n");
        return;
    }

    uint32_t w = g_framebuffer.width;
    uint32_t h = g_framebuffer.height;

    wm_init((int32_t)w, (int32_t)h);
    /* Maximized windows fill the screen minus the taskbar - not the
     * whole screen, or "maximize" would draw a window on top of (and
     * make unreachable) the taskbar's clock and the only way back to
     * a minimized window. */
    wm_set_maximize_area(0, 0, (int32_t)w, (int32_t)(h - TASKBAR_H));

    /* One demo window so the WM has something to demonstrate drag/
     * resize/minimize/maximize/close on - see paint_demo_window_content()'s
     * comment: nothing real runs inside a WM window yet (term.c runs
     * full-screen, not windowed), so this is honestly labeled as a
     * demo rather than pretending to be a real app. There is
     * deliberately no "New Window" menu item spawning more of these -
     * the right-click menu only offers things that attempt to launch
     * a real .trp (New Terminal, Settings). */
    uint32_t seed_id = wm_create_window(120, 90, 360, 240, "Demo Window");
    if (seed_id) {
        wm_window_t *seed = wm_get_window(seed_id);
        if (seed) paint_demo_window_content(seed);
    }

    desktop_menu_t empty_space_menu;
    menu_init(&empty_space_menu);
    menu_add_item(&empty_space_menu, "New Terminal", action_new_terminal);
    menu_add_item(&empty_space_menu, "Settings", action_settings);

    desktop_menu_t window_menu;
    menu_init(&window_menu);
    menu_add_item(&window_menu, "Minimize", action_minimize_window);
    menu_add_item(&window_menu, "Maximize", action_maximize_window);
    menu_add_item(&window_menu, "Close Window", action_close_window);

    /* Which menu is open, and (for window_menu specifically) which
     * window it targets - menu_handle_click()'s ctx argument is
     * supplied by the CALLER at click time, not stored on the menu
     * itself, so the target window has to be tracked here. */
    desktop_menu_t *active_menu = NULL;
    wm_window_t *active_menu_target = NULL;

    mouse_set_bounds((int32_t)w, (int32_t)h);

    uint64_t last_frame = 0;
    int running = 1;
    int last_left = 0;
    int last_right = 0;

    while (running) {
        uint64_t now = pit_get_milliseconds();
        g_now_ms_for_menu_actions = now;

        while (keyboard_has_input()) {
            char c = keyboard_getc_nb();
            if (c == 27) { running = 0; }
        }

        mouse_state_t st;
        mouse_get_state(&st);

        int left_pressed  = st.left_button  && !last_left;
        int right_pressed = st.right_button && !last_right;

        if (active_menu && active_menu->open && left_pressed) {
            menu_handle_click(active_menu, st.x, st.y, active_menu_target);
            menu_close(active_menu);
            active_menu = NULL;
            active_menu_target = NULL;
        } else if (right_pressed) {
            if (active_menu && active_menu->open) {
                menu_close(active_menu);
                active_menu = NULL;
                active_menu_target = NULL;
            }

            /* wm_handle_mouse_down(button=1) deliberately doesn't
             * raise/focus/drag (see wm.c's comment) - it purely
             * reports what was right-clicked, which is exactly what's
             * needed to decide which menu to show. */
            uint32_t hit_id = wm_handle_mouse_down(st.x, st.y, 1);
            if (hit_id != 0) {
                wm_window_t *hit = wm_get_window(hit_id);
                int32_t rel_y = st.y - (hit ? hit->y : 0);
                if (hit && rel_y < WM_TITLEBAR_HEIGHT) {
                    menu_open_at(&window_menu, st.x, st.y);
                    active_menu = &window_menu;
                    active_menu_target = hit;
                }
                /* Right-clicking a window's client content (rather
                 * than its titlebar) has no menu today - that would
                 * be an app-defined context menu, which needs an app
                 * running inside the window to define it (see
                 * paint_demo_window_content()'s comment: nothing
                 * real runs inside these windows yet). */
            } else {
                menu_open_at(&empty_space_menu, st.x, st.y);
                active_menu = &empty_space_menu;
                active_menu_target = NULL;
            }
        } else if (left_pressed) {
            /* Chrome button hit-test happens BEFORE handing off to
             * wm_handle_mouse_down(), since clicking close/minimize/
             * maximize shouldn't also raise/focus/start-dragging the
             * window underneath the button. Only visible (non-
             * minimized) windows can have their buttons clicked, for
             * the obvious reason that a minimized window's titlebar
             * isn't on screen to click. */
            wm_window_t *list; int count;
            wm_get_window_list(&list, &count);
            int consumed = 0;
            for (int i = count - 1; i >= 0; i--) {
                if (list[i].flags & WM_WIN_MINIMIZED) continue;
                if (!point_in_rect(st.x, st.y, list[i].x, list[i].y, list[i].w, list[i].h)) continue;

                char btn = chrome_button_at(&list[i], st.x, st.y);
                if (btn == 'c') {
                    wm_destroy_window(list[i].id);
                    consumed = 1;
                } else if (btn == 'M') {
                    if (list[i].flags & WM_WIN_MAXIMIZED) wm_restore_window(list[i].id);
                    else wm_maximize_window(list[i].id);
                    consumed = 1;
                } else if (btn == 'm') {
                    wm_minimize_window(list[i].id);
                    consumed = 1;
                }
                if (consumed) break;
            }
            if (!consumed) {
                wm_handle_mouse_down(st.x, st.y, 0);
            }
        } else if (st.left_button) {
            wm_handle_mouse_move(st.x, st.y);
        }

        if (!st.left_button && last_left) {
            wm_handle_mouse_up(st.x, st.y, 0);
        }

        last_left = st.left_button;
        last_right = st.right_button;

        if (running && (now - last_frame >= 33 || last_frame == 0)) {
            graphics_clear_screen(graphics_rgb(24, 58, 82));

            wm_window_t *list; int count;
            wm_get_window_list(&list, &count);
            for (int i = 0; i < count; i++) {
                /* Minimized windows are skipped entirely - not drawn,
                 * not composited, matching WM_WIN_MINIMIZED's meaning
                 * (see wm.h's comment: "hidden from compositing, still
                 * exists/tracked"). They're still in the list, so
                 * clicking their taskbar entry below can restore them. */
                if (list[i].flags & WM_WIN_MINIMIZED) continue;
                draw_window(&list[i]);
            }

            color_t taskbar = graphics_rgb(14, 18, 24);
            color_t accent  = graphics_rgb(90, 190, 210);
            color_t white   = GRAPHICS_COLOR_WHITE;

            graphics_fill_rect(0, h - TASKBAR_H, w, TASKBAR_H, taskbar);
            graphics_draw_line(0, h - TASKBAR_H, w - 1, h - TASKBAR_H, accent);

            /* Taskbar left side: either the status message from a
             * recent menu action (launched/not-found/etc.), or the
             * default hint text - never both, so a real result isn't
             * immediately buried under static help text. */
            const char *left_text = "Toriginal OS - right-click for menu, Esc to exit";
            if (now < g_status_msg_until_ms) {
                left_text = g_status_msg;
            }
            draw_string(12, h - TASKBAR_H + (TASKBAR_H - FONT8X16_HEIGHT) / 2,
                        left_text, white, taskbar, 1, 1);

            /* Minimized windows get a small clickable label in the
             * taskbar - without this, minimizing a window would be a
             * one-way trip (see wm_restore_window()'s existence: the
             * WM-level API to bring one back exists, but needs SOME
             * on-screen affordance to trigger it, since the window
             * itself isn't visible to click on anymore). */
            uint32_t taskbar_item_x = 420;
            for (int i = 0; i < count; i++) {
                if (!(list[i].flags & WM_WIN_MINIMIZED)) continue;
                uint32_t label_w = (uint32_t)(strlen(list[i].title) + 2) * FONT8X16_WIDTH;
                gfx2d_rect_t item = { (int32_t)taskbar_item_x, (int32_t)(h - TASKBAR_H + 4),
                                       label_w, TASKBAR_H - 8 };
                gfx2d_fill_rect(item, graphics_rgb(40, 46, 54));
                draw_string(taskbar_item_x + FONT8X16_WIDTH, h - TASKBAR_H + (TASKBAR_H - FONT8X16_HEIGHT) / 2,
                            list[i].title, white, graphics_rgb(40, 46, 54), 0, 1);
                taskbar_item_x += label_w + 8;
            }

            rtc_time_t t;
            rtc_read(&t);
            uint32_t clock_y = h - TASKBAR_H + (TASKBAR_H - FONT8X16_HEIGHT) / 2;
            uint32_t clock_x = w - 12 - (8 * FONT8X16_WIDTH);
            draw_two_digit(clock_x, clock_y, t.hour, white, taskbar);
            draw_string(clock_x + 2 * FONT8X16_WIDTH, clock_y, ":", white, taskbar, 1, 1);
            draw_two_digit(clock_x + 3 * FONT8X16_WIDTH, clock_y, t.minute, white, taskbar);
            draw_string(clock_x + 5 * FONT8X16_WIDTH, clock_y, ":", white, taskbar, 1, 1);
            draw_two_digit(clock_x + 6 * FONT8X16_WIDTH, clock_y, t.second, white, taskbar);

            if (active_menu) menu_draw(active_menu);

            cursor_draw(st.x, st.y, st.left_button ? CURSOR_HAND : CURSOR_ARROW,
                        GRAPHICS_COLOR_WHITE);
            last_frame = now;
        }

        /* Clicking a minimized window's taskbar label restores it -
         * checked after the frame's draw pass computed taskbar_item_x
         * positions would be ideal, but those are frame-local; instead
         * this re-derives the same layout on click, which is cheap
         * (at most WM_MAX_WINDOWS iterations) and keeps the hit-test
         * independent of exactly when in the loop a click lands
         * relative to the throttled ~30fps draw. */
        if (left_pressed && st.y >= (int32_t)(h - TASKBAR_H) && st.x >= 420) {
            wm_window_t *list; int count;
            wm_get_window_list(&list, &count);
            uint32_t item_x = 420;
            for (int i = 0; i < count; i++) {
                if (!(list[i].flags & WM_WIN_MINIMIZED)) continue;
                uint32_t label_w = (uint32_t)(strlen(list[i].title) + 2) * FONT8X16_WIDTH;
                if ((uint32_t)st.x >= item_x && (uint32_t)st.x < item_x + label_w) {
                    wm_restore_window(list[i].id);
                    break;
                }
                item_x += label_w + 8;
            }
        }
    }

    wm_shutdown();

    gterm_clear();
    io_put_string("Back in the shell. Type 'help' for a list of commands.\n");
}
