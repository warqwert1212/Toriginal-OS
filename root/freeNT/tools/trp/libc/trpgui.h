#ifndef TRPGUI_H
#define TRPGUI_H

#include "trplibc.h"

#define EV_NONE      0
#define EV_MOVE      1
#define EV_DOWN      2
#define EV_UP        3
#define EV_KEY_DOWN  4
#define EV_KEY_UP    5
#define EV_CLOSE     6
#define EV_RESIZE    7

#define KEY_LEFT  0x81
#define KEY_RIGHT 0x82
#define KEY_UP    0x83
#define KEY_DOWN  0x84
#define KEY_HOME  0x85
#define KEY_END   0x86
#define KEY_DEL   0x87

#define FONT_W 8
#define FONT_H 16

typedef struct {
    int type, x, y, button, keycode;
} gui_event_t;

typedef struct {
    u32 id, _p0;
    u64 pixels;
    int pitch, cw, ch;
    int _p1;
} gui_create_t;

extern u32 g_win;
extern int g_win_w, g_win_h;

int  gui_open(int x, int y, int w, int h, const char *title);
int  gui_poll(gui_event_t *ev);
void gui_fill(int x, int y, int w, int h, u32 color);
void gui_text(int x, int y, const char *s, u32 fg, u32 bg, u32 flags);
void gui_text_s(int x, int y, const char *s, u32 fg, int scale);
void gui_image(int x, int y, int w, int h, const char *path);
void gui_title(const char *s);
void gui_frame(int x, int y, int w, int h, u32 color);
int  gui_text_w(const char *s, int scale);

#define COL_BG        0xF3F3F3
#define COL_PANEL     0xFFFFFF
#define COL_SIDEBAR   0xE6EAF0
#define COL_BORDER    0xB4B9C2
#define COL_TEXT      0x1B1F27
#define COL_DIM       0x6A7280
#define COL_ACCENT    0x2F7DE1
#define COL_ACCENT_D  0x1F5DB0
#define COL_ACCENT_L  0xD6E6FB
#define COL_BTN       0xE4E7EC
#define COL_BTN_HOT   0xD3DCEA
#define COL_GOOD      0x2E9E5B
#define COL_BAD       0xC7362F

typedef struct {
    int mx, my;
    int down;
    int click;
    int key;
    int dirty;
    int focus;
    int events;
    int rclick;
} ui_state_t;

extern ui_state_t ui;

void ui_pump(void);
int  ui_hit(int x, int y, int w, int h);
int  ui_closed(void);
void ui_panel(int x, int y, int w, int h, u32 fill, u32 border);
void ui_label(int x, int y, const char *s, u32 fg);
void ui_label_s(int x, int y, const char *s, u32 fg, int scale);
int  ui_button(int x, int y, int w, int h, const char *label);
int  ui_button_primary(int x, int y, int w, int h, const char *label);
int  ui_check(int x, int y, const char *label, int *value);
int  ui_radio(int x, int y, const char *label, int selected);
int  ui_item(int x, int y, int w, int h, const char *label, const char *icon, int selected);
void ui_progress(int x, int y, int w, int h, int percent);
int  ui_field(int id, int x, int y, int w, char *buf, int max);
void ui_dirty(void);
void ui_splash(const char *msg);

#endif
