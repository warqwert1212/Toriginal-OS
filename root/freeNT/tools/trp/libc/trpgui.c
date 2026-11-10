#include "trpgui.h"

u32 g_win;
int g_win_w, g_win_h;
ui_state_t ui;
static int g_closed;

int gui_open(int x, int y, int w, int h, const char *title) {
    gui_create_t r;
    memset(&r, 0, sizeof(r));
    if (syscall6(60, x, y, w, h, (long)title, (long)&r) != 0) return -1;
    g_win = r.id;
    g_win_w = r.cw;
    g_win_h = r.ch;
    ui.dirty = 1;
    ui.focus = -1;
    return 0;
}

int gui_poll(gui_event_t *ev) {
    long r = syscall6(62, g_win, (long)ev, 0, 0, 0, 0);
    if (r > 0 && ev->type == EV_CLOSE) g_closed = 1;
    if (r > 0 && ev->type == EV_RESIZE) { g_win_w = ev->x; g_win_h = ev->y; ui.dirty = 1; }
    return r > 0;
}

static void gcmd(int op, int x, int y, int w, int h, u32 fg, u32 bg, u32 flags, const void *ptr) {
    gui_cmd_t c;
    c.win = g_win;
    c.x = x; c.y = y; c.w = w; c.h = h;
    c.fg = fg; c.bg = bg; c.flags = flags;
    c.ptr = (unsigned long)ptr;
    syscall6(SYS_GUI, op, (long)&c, 0, 0, 0, 0);
}

void gui_fill(int x, int y, int w, int h, u32 color) {
    if (w <= 0 || h <= 0) return;
    gcmd(GUI_FILL, x, y, w, h, color, 0, 0, 0);
}

void gui_text(int x, int y, const char *s, u32 fg, u32 bg, u32 flags) {
    char chunk[257];
    int scale = (int)(flags >> GUI_F_SCALE_SHIFT);
    if (scale < 1) scale = 1;
    size_t len = strlen(s);
    size_t step = 256;
    for (size_t off = 0; off < len; off += step) {
        size_t n = len - off < step ? len - off : step;
        memcpy(chunk, s + off, n);
        chunk[n] = 0;
        gcmd(GUI_TEXT, x + (int)off * FONT_W * scale, y, 0, 0, fg, bg, flags, chunk);
    }
}

void gui_text_s(int x, int y, const char *s, u32 fg, int scale) {
    gui_text(x, y, s, fg, 0, GUI_F_TRANSPARENT | ((u32)scale << GUI_F_SCALE_SHIFT));
}

void gui_image(int x, int y, int w, int h, const char *path) { gcmd(GUI_IMAGE, x, y, w, h, 0, 0, 0, path); }
void gui_title(const char *s) { gcmd(GUI_TITLE, 0, 0, 0, 0, 0, 0, 0, s); }

void gui_frame(int x, int y, int w, int h, u32 color) {
    gui_fill(x, y, w, 1, color);
    gui_fill(x, y + h - 1, w, 1, color);
    gui_fill(x, y, 1, h, color);
    gui_fill(x + w - 1, y, 1, h, color);
}

int gui_text_w(const char *s, int scale) { return (int)strlen(s) * FONT_W * (scale < 1 ? 1 : scale); }

void ui_dirty(void) { ui.dirty = 1; }
int ui_closed(void) { return g_closed; }

void ui_pump(void) {
    gui_event_t e;
    ui.click = 0;
    ui.rclick = 0;
    ui.key = 0;
    ui.events = 0;
    while (gui_poll(&e)) {
        ui.events++;
        if (e.type == EV_MOVE) { ui.mx = e.x; ui.my = e.y; }
        else if (e.type == EV_DOWN && e.button == 1) { ui.mx = e.x; ui.my = e.y; ui.rclick = 1; }
        else if (e.type == EV_DOWN) { ui.mx = e.x; ui.my = e.y; ui.down = 1; ui.click = 1; }
        else if (e.type == EV_UP) { ui.mx = e.x; ui.my = e.y; ui.down = 0; }
        else if (e.type == EV_KEY_DOWN && !ui.key) ui.key = e.keycode;
    }
    if (ui.events) ui.dirty = 1;
}

int ui_hit(int x, int y, int w, int h) { return ui.mx >= x && ui.mx < x + w && ui.my >= y && ui.my < y + h; }

void ui_panel(int x, int y, int w, int h, u32 fill, u32 border) {
    gui_fill(x, y, w, h, fill);
    if (border != fill) gui_frame(x, y, w, h, border);
}

void ui_label(int x, int y, const char *s, u32 fg) { gui_text_s(x, y, s, fg, 1); }
void ui_label_s(int x, int y, const char *s, u32 fg, int scale) { gui_text_s(x, y, s, fg, scale); }

static int button(int x, int y, int w, int h, const char *label, int primary) {
    int hot = ui_hit(x, y, w, h);
    u32 fill = primary ? (hot ? COL_ACCENT_D : COL_ACCENT) : (hot ? COL_BTN_HOT : COL_BTN);
    u32 fg = primary ? 0xFFFFFF : COL_TEXT;
    gui_fill(x, y, w, h, fill);
    gui_frame(x, y, w, h, primary ? COL_ACCENT_D : COL_BORDER);
    int tw = gui_text_w(label, 1);
    gui_text_s(x + (w - tw) / 2, y + (h - FONT_H) / 2, label, fg, 1);
    if (hot && ui.click) { ui.click = 0; ui.dirty = 1; return 1; }
    return 0;
}

int ui_button(int x, int y, int w, int h, const char *label) { return button(x, y, w, h, label, 0); }
int ui_button_primary(int x, int y, int w, int h, const char *label) { return button(x, y, w, h, label, 1); }

int ui_check(int x, int y, const char *label, int *value) {
    int w = 24 + gui_text_w(label, 1);
    int hot = ui_hit(x, y, w, 20);
    gui_fill(x, y + 2, 16, 16, COL_PANEL);
    gui_frame(x, y + 2, 16, 16, hot ? COL_ACCENT : COL_BORDER);
    if (*value) gui_fill(x + 4, y + 6, 8, 8, COL_ACCENT);
    gui_text_s(x + 24, y + 2, label, COL_TEXT, 1);
    if (hot && ui.click) { ui.click = 0; *value = !*value; ui.dirty = 1; return 1; }
    return 0;
}

int ui_radio(int x, int y, const char *label, int selected) {
    int w = 24 + gui_text_w(label, 1);
    int hot = ui_hit(x, y, w, 20);
    gui_fill(x, y + 2, 16, 16, COL_PANEL);
    gui_frame(x, y + 2, 16, 16, hot || selected ? COL_ACCENT : COL_BORDER);
    if (selected) gui_fill(x + 4, y + 6, 8, 8, COL_ACCENT);
    gui_text_s(x + 24, y + 2, label, COL_TEXT, 1);
    if (hot && ui.click) { ui.click = 0; ui.dirty = 1; return 1; }
    return 0;
}

int ui_item(int x, int y, int w, int h, const char *label, const char *icon, int selected) {
    int hot = ui_hit(x, y, w, h);
    if (selected) gui_fill(x, y, w, h, COL_ACCENT_L);
    else if (hot) gui_fill(x, y, w, h, COL_BTN_HOT);
    int tx = x + 10;
    if (icon) {
        int is = h - 8 < 32 ? h - 8 : 32;
        gui_image(x + 8, y + (h - is) / 2, is, is, icon);
        tx = x + 16 + is;
    }
    gui_text_s(tx, y + (h - FONT_H) / 2, label, COL_TEXT, 1);
    if (hot && ui.click) { ui.click = 0; ui.dirty = 1; return 1; }
    return 0;
}

void ui_progress(int x, int y, int w, int h, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    gui_fill(x, y, w, h, COL_BTN);
    gui_frame(x, y, w, h, COL_BORDER);
    gui_fill(x + 1, y + 1, (w - 2) * percent / 100, h - 2, percent > 85 ? COL_BAD : COL_ACCENT);
}

int ui_field(int id, int x, int y, int w, char *buf, int max) {
    int hot = ui_hit(x, y, w, 24);
    if (hot && ui.click) { ui.focus = id; ui.click = 0; ui.dirty = 1; }
    int focused = ui.focus == id;
    gui_fill(x, y, w, 24, COL_PANEL);
    gui_frame(x, y, w, 24, focused ? COL_ACCENT : COL_BORDER);
    int len = (int)strlen(buf);
    int cols = (w - 12) / FONT_W;
    const char *shown = len > cols ? buf + (len - cols) : buf;
    gui_text_s(x + 6, y + 4, shown, COL_TEXT, 1);
    if (focused) {
        int cx = x + 6 + (int)strlen(shown) * FONT_W;
        gui_fill(cx, y + 4, 2, 16, COL_ACCENT);
    }
    int submitted = 0;
    if (focused && ui.key) {
        int k = ui.key;
        if (k == '\n' || k == '\r') submitted = 1;
        else if ((k == 8 || k == 127) && len > 0) buf[len - 1] = 0;
        else if (k >= 32 && k < 127 && len < max - 1) { buf[len] = (char)k; buf[len + 1] = 0; }
        else k = 0;
        if (k) ui.key = 0;
        ui.dirty = 1;
    }
    return submitted;
}

void ui_splash(const char *msg) {
    gui_fill(0, 0, g_win_w, g_win_h, COL_BG);
    ui_label_s(24, 24, msg, COL_TEXT, 2);
}
