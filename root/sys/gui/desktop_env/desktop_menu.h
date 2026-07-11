#ifndef _DESKTOP_MENU_H
#define _DESKTOP_MENU_H

#include <stdint.h>

#define MENU_MAX_ITEMS 16

typedef void (*menu_action_fn)(void *ctx);

typedef struct {
    char label[32];
    menu_action_fn action;
} menu_item_t;

typedef struct {
    int          open;
    int32_t      x, y;
    int          item_count;
    menu_item_t  items[MENU_MAX_ITEMS];
} desktop_menu_t;

void menu_init(desktop_menu_t *menu);
int  menu_add_item(desktop_menu_t *menu, const char *label, menu_action_fn action);
void menu_open_at(desktop_menu_t *menu, int32_t x, int32_t y);
void menu_close(desktop_menu_t *menu);

/* Returns 1 and fires the action if (x,y) hit an item; 0 if click missed
 * (caller should then close the menu). Only call while menu->open. */
int  menu_handle_click(desktop_menu_t *menu, int32_t x, int32_t y, void *ctx);

/* Draws the menu directly into an ARGB8888 framebuffer. */
void menu_draw(const desktop_menu_t *menu, uint32_t *fb, int32_t fb_w, int32_t fb_h);

#endif
