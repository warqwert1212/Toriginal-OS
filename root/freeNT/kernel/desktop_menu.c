
#include "desktop_menu.h"
#include "graphics_2d.h"
#include "font8x16.h"
#include "string.h"

#define MENU_ITEM_H   22
#define MENU_ITEM_W   200
#define MENU_PAD_X    8
#define MENU_PAD_Y    3

void menu_init(desktop_menu_t *menu) {
    memset(menu, 0, sizeof(*menu));
}

int menu_add_item(desktop_menu_t *menu, const char *label, menu_action_fn action) {
    if (menu->item_count >= MENU_MAX_ITEMS) return 0;
    menu_item_t *it = &menu->items[menu->item_count++];
    strncpy(it->label, label, sizeof(it->label) - 1);
    it->label[sizeof(it->label) - 1] = '\0';
    it->action = action;
    return 1;
}

void menu_open_at(desktop_menu_t *menu, int32_t x, int32_t y) {
    int32_t menu_h = menu->item_count * MENU_ITEM_H;
    int32_t screen_w = (int32_t)g_framebuffer.width;
    int32_t screen_h = (int32_t)g_framebuffer.height;

    if (x + MENU_ITEM_W > screen_w) x = screen_w - MENU_ITEM_W;
    if (y + menu_h > screen_h) y = screen_h - menu_h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    menu->open = 1;
    menu->x = x;
    menu->y = y;
}

void menu_close(desktop_menu_t *menu) {
    menu->open = 0;
}

int menu_handle_click(desktop_menu_t *menu, int32_t x, int32_t y, void *ctx) {
    if (!menu->open) return 0;

    int32_t rel_x = x - menu->x;
    int32_t rel_y = y - menu->y;
    int32_t menu_h = menu->item_count * MENU_ITEM_H;

    if (rel_x < 0 || rel_x >= MENU_ITEM_W || rel_y < 0 || rel_y >= menu_h) {
        return 0;
    }

    int idx = rel_y / MENU_ITEM_H;
    if (idx >= 0 && idx < menu->item_count) {
        if (menu->items[idx].action) menu->items[idx].action(ctx);
        return 1;
    }
    return 0;
}

void menu_draw(const desktop_menu_t *menu) {
    if (!menu->open) return;

    int32_t menu_h = menu->item_count * MENU_ITEM_H;

    color_t bg     = graphics_rgb(0xE8, 0xE8, 0xEA);
    color_t border = graphics_rgb(0x40, 0x40, 0x40);
    color_t text_color = graphics_rgb(0x10, 0x10, 0x10);
    color_t hover_bg   = graphics_rgb(0x3A, 0x7A, 0xD6); 
    (void)hover_bg;

    gfx2d_rect_t body = { menu->x, menu->y, (uint32_t)MENU_ITEM_W, (uint32_t)menu_h };
    gfx2d_fill_rect(body, bg);

    for (int i = 0; i < menu->item_count; i++) {
        int32_t iy = menu->y + i * MENU_ITEM_H;

        if (i > 0) {
            gfx2d_rect_t sep = { menu->x + 4, iy, (uint32_t)(MENU_ITEM_W - 8), 1 };
            gfx2d_fill_rect(sep, graphics_rgb(0xC8, 0xC8, 0xCC));
        }

        uint32_t tx = (uint32_t)(menu->x + MENU_PAD_X);
        uint32_t ty = (uint32_t)(iy + MENU_PAD_Y);
        for (const char *p = menu->items[i].label; *p; p++) {
            font_draw_glyph(tx, ty, *p, text_color, bg, 1, 1);
            tx += FONT8X16_WIDTH;
        }
    }

    gfx2d_draw_rect(body, border);
}
