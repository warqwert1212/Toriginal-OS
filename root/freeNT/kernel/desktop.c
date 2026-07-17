/* desktop.c - Minimal real graphical desktop environment.
 *
 * Draws directly onto the framebuffer (graphics_core.h), independent
 * of the gterm text-cell grid. Runs its own event loop: polls the
 * keyboard and PS/2 mouse itself rather than going through the
 * shell's line-based input, throttled to ~30 frames/sec using
 * pit_get_milliseconds() so it doesn't peg the CPU harder than a
 * bare-metal kernel with no other work needs to.
 */
#include "desktop.h"
#include "graphics_core.h"
#include "font8x16.h"
#include "cursor.h"
#include "mouse.h"
#include "keyboard.h"
#include "pit.h"
#include "rtc.h"
#include "gfx_terminal.h"
#include "io.h"
#include "string.h"

#define TASKBAR_H   40
#define ICON_W      96
#define ICON_H      72
#define ICON_GAP    24
#define ICON_TOP    60

typedef struct {
    uint32_t x, y, w, h;
    const char *label;
} desktop_icon_t;

static void draw_string(uint32_t x, uint32_t y, const char *s, color_t fg,
                        color_t bg, int transparent, uint32_t scale) {
    uint32_t cx = x;
    for (const char *p = s; *p; p++) {
        font_draw_glyph(cx, y, *p, fg, bg, transparent, scale);
        cx += FONT8X16_WIDTH * scale;
    }
}

static void draw_two_digit(uint32_t x, uint32_t y, uint8_t v, color_t fg,
                           color_t bg) {
    char buf[3];
    buf[0] = (char)('0' + (v / 10) % 10);
    buf[1] = (char)('0' + v % 10);
    buf[2] = '\0';
    draw_string(x, y, buf, fg, bg, 0, 1);
}

static int point_in_rect(int32_t px, int32_t py, uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h) {
    return px >= (int32_t)x && px < (int32_t)(x + w) &&
           py >= (int32_t)y && py < (int32_t)(y + h);
}

static void draw_icon(const desktop_icon_t *ic, color_t body, color_t border,
                      color_t text) {
    graphics_fill_rect(ic->x, ic->y, ic->w, ic->h, body);
    graphics_draw_rect(ic->x, ic->y, ic->w, ic->h, border);
    /* Center the label under the icon glyph area, single line, 6px
     * glyphs assumed to fit within ICON_W at scale 1. */
    uint32_t text_w = (uint32_t)strlen(ic->label) * FONT8X16_WIDTH;
    uint32_t tx = ic->x + (ic->w > text_w ? (ic->w - text_w) / 2 : 0);
    draw_string(tx, ic->y + ic->h - FONT8X16_HEIGHT - 6, ic->label, text,
                body, 1, 1);
}

static void draw_frame(uint32_t w, uint32_t h, const desktop_icon_t *icons,
                       int n_icons, int highlighted) {
    color_t wallpaper = graphics_rgb(24, 58, 82);
    color_t taskbar    = graphics_rgb(14, 18, 24);
    color_t accent     = graphics_rgb(90, 190, 210);
    color_t icon_body  = graphics_rgb(36, 90, 120);
    color_t icon_hi    = graphics_rgb(52, 130, 160);
    color_t white      = GRAPHICS_COLOR_WHITE;

    graphics_clear_screen(wallpaper);

    /* Desktop icons */
    for (int i = 0; i < n_icons; i++) {
        draw_icon(&icons[i], (i == highlighted) ? icon_hi : icon_body,
                  accent, white);
    }

    /* Taskbar */
    graphics_fill_rect(0, h - TASKBAR_H, w, TASKBAR_H, taskbar);
    graphics_draw_line(0, h - TASKBAR_H, w - 1, h - TASKBAR_H, accent);
    draw_string(12, h - TASKBAR_H + (TASKBAR_H - FONT8X16_HEIGHT) / 2,
                "Toriginal OS", white, taskbar, 1, 1);

    /* Clock, right-aligned */
    rtc_time_t t;
    rtc_read(&t);
    uint32_t clock_y = h - TASKBAR_H + (TASKBAR_H - FONT8X16_HEIGHT) / 2;
    uint32_t clock_x = w - 12 - (8 * FONT8X16_WIDTH);
    draw_two_digit(clock_x, clock_y, t.hour, white, taskbar);
    draw_string(clock_x + 2 * FONT8X16_WIDTH, clock_y, ":", white, taskbar, 1, 1);
    draw_two_digit(clock_x + 3 * FONT8X16_WIDTH, clock_y, t.minute, white, taskbar);
    draw_string(clock_x + 5 * FONT8X16_WIDTH, clock_y, ":", white, taskbar, 1, 1);
    draw_two_digit(clock_x + 6 * FONT8X16_WIDTH, clock_y, t.second, white, taskbar);

    draw_string(12, 16, "Esc or 'Exit to shell' to leave the desktop",
                graphics_rgb(210, 230, 235), wallpaper, 1, 1);
}

void desktop_run(void) {
    if (!graphics_is_available()) {
        io_put_string("desktop: no graphics framebuffer available; use the shell instead.\n");
        return;
    }

    uint32_t w = g_framebuffer.width;
    uint32_t h = g_framebuffer.height;

    desktop_icon_t icons[2];
    icons[0].x = ICON_GAP;
    icons[0].y = ICON_TOP;
    icons[0].w = ICON_W;
    icons[0].h = ICON_H;
    icons[0].label = "Terminal";

    icons[1].x = ICON_GAP;
    icons[1].y = ICON_TOP + ICON_H + ICON_GAP;
    icons[1].w = ICON_W;
    icons[1].h = ICON_H;
    icons[1].label = "Exit to shell";

    mouse_set_bounds((int32_t)w, (int32_t)h);

    uint64_t last_frame = 0;
    int running = 1;
    int last_left = 0;

    while (running) {
        uint64_t now = pit_get_milliseconds();

        /* Keyboard: Esc leaves the desktop immediately. */
        while (keyboard_has_input()) {
            char c = keyboard_getc_nb();
            if (c == 27) { running = 0; }
        }

        mouse_state_t st;
        mouse_get_state(&st);

        int highlighted = -1;
        for (int i = 0; i < 2; i++) {
            if (point_in_rect(st.x, st.y, icons[i].x, icons[i].y,
                              icons[i].w, icons[i].h)) {
                highlighted = i;
            }
        }

        /* Rising edge of the left button over an icon = "click". */
        if (st.left_button && !last_left && highlighted != -1) {
            /* Both icons currently do the same thing: leave the
             * desktop and return to the text shell. "Terminal" and
             * "Exit to shell" are kept as two distinct, clickable
             * targets rather than collapsing to one so the desktop
             * reads as a real multi-icon environment rather than a
             * single dead-end button. */
            running = 0;
        }
        last_left = st.left_button;

        if (running && (now - last_frame >= 33 || last_frame == 0)) {
            draw_frame(w, h, icons, 2, highlighted);
            cursor_draw(st.x, st.y, st.left_button ? CURSOR_HAND : CURSOR_ARROW,
                        GRAPHICS_COLOR_WHITE);
            last_frame = now;
        }
    }

    /* Hand the screen back to the text terminal. */
    gterm_clear();
    io_put_string("Back in the shell. Type 'help' for a list of commands.\n");
}
