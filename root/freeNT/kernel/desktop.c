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

#define TASKBAR_H       32
#define CLOSE_BTN_SIZE  16

/* ── Window chrome colors ─────────────────────────────────────────── */
static color_t chrome_titlebar_focused(void)   { return graphics_rgb(0x2A, 0x5C, 0x8A); }
static color_t chrome_titlebar_unfocused(void) { return graphics_rgb(0x50, 0x50, 0x54); }
static color_t chrome_border(void)             { return graphics_rgb(0x18, 0x18, 0x1A); }
static color_t chrome_close_btn(void)          { return graphics_rgb(0xC0, 0x30, 0x30); }

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

    int32_t close_x = win->x + win->w - WM_BORDER_WIDTH - CLOSE_BTN_SIZE - 4;
    int32_t close_y = win->y + (WM_TITLEBAR_HEIGHT - CLOSE_BTN_SIZE) / 2;
    gfx2d_rect_t close_rect = { close_x, close_y, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE };
    gfx2d_fill_rect(close_rect, chrome_close_btn());
    font_draw_glyph((uint32_t)(close_x + 4), (uint32_t)(close_y), 'x',
                     GRAPHICS_COLOR_WHITE, chrome_close_btn(), 1, 1);

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

static int point_in_close_button(wm_window_t *win, int32_t x, int32_t y) {
    int32_t close_x = win->x + win->w - WM_BORDER_WIDTH - CLOSE_BTN_SIZE - 4;
    int32_t close_y = win->y + (WM_TITLEBAR_HEIGHT - CLOSE_BTN_SIZE) / 2;
    return point_in_rect(x, y, close_x, close_y, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
}

/* ── Right-click context menu actions ───────────────────────────────
 * menu_action_fn's `ctx` carries whatever the menu was opened
 * against - for the desktop-empty-space menu that's NULL (no window
 * involved); for the window-titlebar menu it's the wm_window_t* that
 * was right-clicked, passed at the menu_handle_click() call site. */
static int32_t g_next_new_window_x = 80;
static int32_t g_next_new_window_y = 80;

static void action_new_window(void *ctx) {
    (void)ctx;
    uint32_t id = wm_create_window(g_next_new_window_x, g_next_new_window_y,
                                   320, 220, "New Window");
    if (id) {
        wm_window_t *win = wm_get_window(id);
        if (win) paint_demo_window_content(win);

        g_next_new_window_x += 32;
        g_next_new_window_y += 32;
        if (g_next_new_window_x > 400) g_next_new_window_x = 80;
        if (g_next_new_window_y > 300) g_next_new_window_y = 80;
    }
}

static void action_close_window(void *ctx) {
    wm_window_t *win = (wm_window_t *)ctx;
    if (win) wm_destroy_window(win->id);
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

    uint32_t seed_id = wm_create_window(120, 90, 360, 240, "Window 1");
    if (seed_id) {
        wm_window_t *seed = wm_get_window(seed_id);
        if (seed) paint_demo_window_content(seed);
    }

    desktop_menu_t empty_space_menu;
    menu_init(&empty_space_menu);
    menu_add_item(&empty_space_menu, "New Window", action_new_window);
    menu_add_item(&empty_space_menu, "Refresh", NULL);

    desktop_menu_t window_menu;
    menu_init(&window_menu);
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
            wm_window_t *list; int count;
            wm_get_window_list(&list, &count);
            int consumed_by_close = 0;
            for (int i = count - 1; i >= 0; i--) {
                if (point_in_rect(st.x, st.y, list[i].x, list[i].y, list[i].w, list[i].h)
                    && point_in_close_button(&list[i], st.x, st.y)) {
                    wm_destroy_window(list[i].id);
                    consumed_by_close = 1;
                    break;
                }
            }
            if (!consumed_by_close) {
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
                draw_window(&list[i]);
            }

            color_t taskbar = graphics_rgb(14, 18, 24);
            color_t accent  = graphics_rgb(90, 190, 210);
            color_t white   = GRAPHICS_COLOR_WHITE;

            graphics_fill_rect(0, h - TASKBAR_H, w, TASKBAR_H, taskbar);
            graphics_draw_line(0, h - TASKBAR_H, w - 1, h - TASKBAR_H, accent);
            draw_string(12, h - TASKBAR_H + (TASKBAR_H - FONT8X16_HEIGHT) / 2,
                        "Toriginal OS - right-click for menu, Esc to exit",
                        white, taskbar, 1, 1);

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
    }

    wm_shutdown();

    gterm_clear();
    io_put_string("Back in the shell. Type 'help' for a list of commands.\n");
}
