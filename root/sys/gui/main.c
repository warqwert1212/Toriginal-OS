#include "platform/platform.h"
#include "wm/wm.h"
#include "compositor/compositor.h"
#include "image/image_loader.h"
#include "desktop_env/desktop_menu.h"
#include "desktop_env/wallpaper_picker.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SCREEN_W 1024
#define SCREEN_H 768

static image_t g_cursor_normal;
static image_t g_cursor_click;
static int g_cursor_loaded = 0;
static int g_mouse_x = 0, g_mouse_y = 0;
static int g_left_down = 0;

static desktop_menu_t g_menu;
static int g_running = 1;
static int g_term_count = 0;

static void action_open_terminal(void *ctx) {
    (void)ctx;
    char title[64];
    snprintf(title, sizeof(title), "TERMINAL %d", ++g_term_count);

    uint32_t win = wm_create_window(150 + g_term_count * 20, 120 + g_term_count * 20,
                                     500, 350, title);
    wm_window_t *w = wm_get_window(win);
    if (w) {
        for (int y = 0; y < w->client_h; y++) {
            for (int x = 0; x < w->client_w; x++) {
                w->pixels[y * (w->pitch / 4) + x] = 0xFF101010; /* black terminal bg */
            }
        }
    }
}

static void action_exit(void *ctx) {
    (void)ctx;
    g_running = 0;
}

static void action_change_wallpaper(void *ctx) {
    (void)ctx;
    const fb_t *pfb = plat_get_framebuffer();
    char path[PICKER_PATH_MAX];

    if (picker_run(pfb->pixels, pfb->width, pfb->height, path, sizeof(path))) {
        image_t wallpaper = {0};
        if (image_load(path, &wallpaper)) {
            comp_set_wallpaper(wallpaper.pixels, wallpaper.width, wallpaper.height);
            image_free(&wallpaper);
            picker_save_choice(path);
            printf("Wallpaper changed: %s\n", path);
        }
    }
    /* picker_run drives its own draw/present loop, so the desktop's own
     * framebuffer contents underneath are stale now -- force a redraw of
     * whatever's still open (windows, menu) on the very next composite. */
    comp_composite();
}

static void blit_cursor_frame(uint32_t *fb, int32_t fb_w, int32_t fb_h) {
    if (!g_cursor_loaded) return;
    image_t *cur = g_left_down ? &g_cursor_click : &g_cursor_normal;
    if (!cur->pixels) cur = &g_cursor_normal;
    if (!cur->pixels) return;

    for (int32_t y = 0; y < cur->height; y++) {
        int32_t fy = g_mouse_y + y;
        if (fy < 0 || fy >= fb_h) continue;
        for (int32_t x = 0; x < cur->width; x++) {
            int32_t fx = g_mouse_x + x;
            if (fx < 0 || fx >= fb_w) continue;

            uint32_t src = cur->pixels[y * cur->width + x];
            uint8_t sa = (src >> 24) & 0xFF;
            if (sa == 0) continue;

            uint32_t *dst = &fb[fy * fb_w + fx];
            if (sa == 255) {
                *dst = src;
                continue;
            }
            uint8_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
            uint8_t dr = (*dst >> 16) & 0xFF, dg = (*dst >> 8) & 0xFF, db = *dst & 0xFF;
            int inv = 255 - sa;
            uint8_t or_ = (sr * sa + dr * inv) / 255;
            uint8_t og = (sg * sa + dg * inv) / 255;
            uint8_t ob = (sb * sa + db * inv) / 255;
            *dst = 0xFF000000 | (or_ << 16) | (og << 8) | ob;
        }
    }
}

int main(void) {
    printf("Initializing desktop environment...\n");

    if (!plat_init(SCREEN_W, SCREEN_H)) {
        fprintf(stderr, "Platform init failed\n");
        return 1;
    }

    wm_init(SCREEN_W, SCREEN_H);
    comp_init(SCREEN_W, SCREEN_H);

    /* Wallpaper: file-driven only, chosen by the user out of assets/desktop/wallpapers/.
     * First boot (no saved config) triggers the graphical picker; every boot
     * after that just re-loads the saved choice. Never hardcoded art. */
    char wallpaper_path[PICKER_PATH_MAX];
    int have_choice = picker_load_saved_choice(wallpaper_path, sizeof(wallpaper_path));

    if (!have_choice) {
        const fb_t *pfb = plat_get_framebuffer();
        if (picker_run(pfb->pixels, pfb->width, pfb->height,
                       wallpaper_path, sizeof(wallpaper_path))) {
            picker_save_choice(wallpaper_path);
            have_choice = 1;
        }
    }

    image_t wallpaper = {0};
    if (have_choice && image_load(wallpaper_path, &wallpaper)) {
        comp_set_wallpaper(wallpaper.pixels, wallpaper.width, wallpaper.height);
        image_free(&wallpaper);
        printf("Wallpaper loaded: %s\n", wallpaper_path);
    } else {
        printf("No wallpaper selected, using default fill.\n");
    }

    /* Cursor: also file-driven -- the XP-style PNGs already in assets/. */
    if (image_load("assets/toriginal cursor.png", &g_cursor_normal)) {
        g_cursor_loaded = 1;
        if (!image_load("assets/toriginal cursor click.png", &g_cursor_click)) {
            g_cursor_click.pixels = NULL;
        }
        printf("Cursor loaded from file.\n");
    } else {
        printf("No cursor file found -- mouse will be invisible.\n");
    }

    menu_init(&g_menu);
    menu_add_item(&g_menu, "TERMINAL", action_open_terminal);
    menu_add_item(&g_menu, "CHANGE WALLPAPER", action_change_wallpaper);
    menu_add_item(&g_menu, "EXIT", action_exit);

    printf("Right-click desktop for menu. Left-click drags/focuses windows.\n");

    while (g_running) {
        event_t evt;
        while (plat_poll_event(&evt)) {
            switch (evt.type) {
                case EVT_QUIT:
                    g_running = 0;
                    break;

                case EVT_KEY:
                    if (evt.key == 27) {
                        if (g_menu.open) menu_close(&g_menu);
                        else g_running = 0;
                    }
                    wm_handle_key(evt.key, evt.is_pressed);
                    break;

                case EVT_MOUSE_MOVE:
                    g_mouse_x = evt.x;
                    g_mouse_y = evt.y;
                    if (!g_menu.open) {
                        wm_handle_mouse_move(evt.x, evt.y);
                    }
                    break;

                case EVT_MOUSE_DOWN:
                    if (evt.button == 1) {
                        /* Right-click: open desktop menu at cursor.
                         * (Real desktop icons/right-click-on-window context
                         * menus come later; this is desktop background only.) */
                        menu_open_at(&g_menu, evt.x, evt.y);
                    } else if (evt.button == 0) {
                        g_left_down = 1;
                        if (g_menu.open) {
                            menu_handle_click(&g_menu, evt.x, evt.y, NULL);
                            menu_close(&g_menu);
                        } else {
                            wm_handle_mouse_down(evt.x, evt.y, evt.button);
                        }
                    }
                    break;

                case EVT_MOUSE_UP:
                    if (evt.button == 0) {
                        g_left_down = 0;
                        if (!g_menu.open) {
                            wm_handle_mouse_up(evt.x, evt.y, evt.button);
                        }
                    }
                    break;

                default:
                    break;
            }
        }

        comp_composite();

        uint32_t *fb;
        int32_t fb_w, fb_h, fb_pitch;
        comp_get_framebuffer(&fb, &fb_w, &fb_h, &fb_pitch);

        menu_draw(&g_menu, fb, fb_w, fb_h);
        blit_cursor_frame(fb, fb_w, fb_h);

        fb_t plat_fb = { .pixels = fb, .width = fb_w, .height = fb_h, .pitch = fb_pitch };
        plat_present(&plat_fb);

        plat_delay(16);
    }

    printf("Shutting down...\n");
    image_free(&g_cursor_normal);
    image_free(&g_cursor_click);
    comp_shutdown();
    wm_shutdown();
    plat_shutdown();

    return 0;
}
