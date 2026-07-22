
#ifndef _DESKTOP_MENU_H
#define _DESKTOP_MENU_H

#include <stdint.h>

#define MENU_MAX_ITEMS 16

typedef void (*menu_action_fn)(void *ctx);

typedef struct {
    char label[32];
    menu_action_fn action;
    void *item_ctx; /* NULL = "not set", falls back to the ctx passed
                      * into menu_handle_click() at click time - this
                      * keeps every existing call site (New Terminal,
                      * Settings, etc, which all pass NULL here)
                      * working exactly as before. Set this via
                      * menu_add_item_ctx() when different rows in the
                      * same menu need different data, e.g. the
                      * wallpaper picker where each row is a different
                      * file path. */
} menu_item_t;

typedef struct {
    int          open;
    int32_t      x, y;
    int          item_count;
    menu_item_t  items[MENU_MAX_ITEMS];
} desktop_menu_t;

void menu_init(desktop_menu_t *menu);
int  menu_add_item(desktop_menu_t *menu, const char *label, menu_action_fn action);
int  menu_add_item_ctx(desktop_menu_t *menu, const char *label, menu_action_fn action, void *item_ctx);
void menu_open_at(desktop_menu_t *menu, int32_t x, int32_t y);
void menu_close(desktop_menu_t *menu);


int  menu_handle_click(desktop_menu_t *menu, int32_t x, int32_t y, void *ctx);


void menu_draw(const desktop_menu_t *menu, int32_t mouse_x, int32_t mouse_y);

#endif 
