#include "desktop_menu.h"
#include "ui_draw.h"
#include <string.h>

#define MENU_ITEM_H   22
#define MENU_ITEM_W   200
#define MENU_PAD_X    8
#define MENU_PAD_Y    4

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

void menu_draw(const desktop_menu_t *menu, uint32_t *fb, int32_t fb_w, int32_t fb_h) {
    if (!menu->open) return;

    int32_t menu_h = menu->item_count * MENU_ITEM_H;

    uint32_t bg = ui_argb(255, 0xC8, 0xC8, 0xC8);
    uint32_t border = ui_argb(255, 0x40, 0x40, 0x40);
    uint32_t text_color = ui_argb(255, 0x10, 0x10, 0x10);

    ui_fill_rect(fb, fb_w, fb_h, menu->x, menu->y, MENU_ITEM_W, menu_h, bg);

    for (int i = 0; i < menu->item_count; i++) {
        int32_t iy = menu->y + i * MENU_ITEM_H;
        ui_draw_text(fb, fb_w, fb_h, menu->x + MENU_PAD_X, iy + MENU_PAD_Y + 4,
                     menu->items[i].label, text_color);
    }

    ui_fill_rect(fb, fb_w, fb_h, menu->x, menu->y, MENU_ITEM_W, 1, border);
    ui_fill_rect(fb, fb_w, fb_h, menu->x, menu->y + menu_h - 1, MENU_ITEM_W, 1, border);
    ui_fill_rect(fb, fb_w, fb_h, menu->x, menu->y, 1, menu_h, border);
    ui_fill_rect(fb, fb_w, fb_h, menu->x + MENU_ITEM_W - 1, menu->y, 1, menu_h, border);
}
