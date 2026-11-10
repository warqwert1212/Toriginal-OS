#include "trpgui.h"

#define ICON_DIR "/system/gui/icons/"
#define MAXE 512
#define NAV_W 188
#define TOP_H 76
#define ROW_H 26
#define TILE_W 96
#define TILE_H 92
#define COL_SIZE_W 90
#define COL_TYPE_W 130
#define COL_DATE_W 120

typedef struct { char name[64]; u8 dir; u32 size; u32 mtime; } entry_t;

static entry_t g_e[MAXE];
static int g_n;
static char g_path[256] = "/";
static int g_pc = 0;
static char g_hist[32][256];
static int g_hpos = -1, g_hcount;
static char g_addr[256];
static int g_sel = -1;
static int g_scroll;
static int g_icons_view;
static char g_clip[256];
static char g_status[160];
static int g_confirm_del;
static int g_newdir;
static char g_newname[48];
static int g_viewer;
static int g_ctx, g_ctx_i, g_ctx_x, g_ctx_y;
static char g_vpath[256];
static char g_vtext[2048];
static u64 g_click_t;
static int g_click_i = -2;
static int g_drag_sb;
static char g_ncwd[256];

static const struct { const char *label; const char *path; const char *icon; } g_nav[] = {
    { "root", "/", "vista_pc_1.png" },
    { "System", "/system", "033.png" },
    { "Applications", "/system/trp", "vista_get_started.png" },
    { "Wallpapers", "/system/gui/wallpapers", "094.png" },
    { "Icons", "/system/gui/icons", "094.png" },
    { "Home", "/home", "033.png" },
};
#define NNAV ((int)(sizeof(g_nav) / sizeof(g_nav[0])))

static const char *ext_of(const char *n) {
    const char *d = strrchr(n, '.');
    return d && d != n ? d + 1 : "";
}

static int ext_is(const char *n, const char *e) { return !strcasecmp(ext_of(n), e); }

static int is_image(const char *n) { return ext_is(n, "png") || ext_is(n, "jpg") || ext_is(n, "jpeg") || ext_is(n, "bmp"); }
static int is_text(const char *n) {
    static const char *t[] = { "txt", "ini", "cfg", "c", "h", "md", "manifest", "log", "sh", "conf", NULL };
    for (int i = 0; t[i]; i++) if (ext_is(n, t[i])) return 1;
    return 0;
}

static const char *icon_for(const entry_t *e) {
    if (e->dir) return "033.png";
    if (ext_is(e->name, "trp")) return "vista_console.png";
    if (is_image(e->name)) return "094.png";
    if (is_text(e->name)) return "086.png";
    if (ext_is(e->name, "mp3") || ext_is(e->name, "wav")) return "007.png";
    if (ext_is(e->name, "mp4") || ext_is(e->name, "avi")) return "096.png";
    if (ext_is(e->name, "zip")) return "022.png";
    return "092.png";
}

static const char *type_for(const entry_t *e) {
    if (e->dir) return "File folder";
    if (ext_is(e->name, "trp")) return "TRP application";
    if (is_image(e->name)) return "Image";
    if (is_text(e->name)) return "Text document";
    return "File";
}

static void fmt_size(char *o, size_t m, u32 s) {
    if (s < 1024) snprintf(o, m, "%u B", s);
    else if (s < 1024 * 1024) snprintf(o, m, "%u KB", (s + 1023) / 1024);
    else snprintf(o, m, "%u.%u MB", s / (1024 * 1024), (s % (1024 * 1024)) * 10 / (1024 * 1024));
}

static void fmt_date(char *o, size_t m, u32 t) {
    if (t < 100000) { strlcpy(o, "-", m); return; }
    u32 days = t / 86400;
    int y = 1970;
    for (;;) {
        int yl = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
        if (days < (u32)yl) break;
        days -= (u32)yl;
        y++;
    }
    static const int md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int leap = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0);
    int mo = 0;
    while (mo < 11 && days >= (u32)(md[mo] + (mo == 1 && leap))) { days -= (u32)(md[mo] + (mo == 1 && leap)); mo++; }
    snprintf(o, m, "%d-%02d-%02d", y, mo + 1, (int)days + 1);
}

static void join(char *out, size_t m, const char *dir, const char *name) {
    if (!strcmp(dir, "/")) snprintf(out, m, "/%s", name);
    else snprintf(out, m, "%s/%s", dir, name);
}

static void disp_path(char *out, size_t m) {
    if (g_pc) strlcpy(out, "Computer", m);
    else snprintf(out, m, "root%s", g_path[1] ? g_path : "");
}

static int cmp_entry(const void *a, const void *b) {
    const entry_t *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}

static void load_dir(void) {
    g_n = 0;
    g_sel = -1;
    g_scroll = 0;
    if (g_pc) {
        strlcpy(g_addr, "Computer", sizeof(g_addr));
        return;
    }
    char *buf = malloc(16384);
    if (!buf) return;
    int cnt = listdir(g_path, buf, 16384);
    char *p = buf;
    for (int i = 0; i < cnt && g_n < MAXE; i++) {
        int type = (u8)p[0];
        char *name = p + 1;
        p += 1 + strlen(name) + 1;
        if (strlen(name) >= 63) continue;
        entry_t *e = &g_e[g_n++];
        memset(e, 0, sizeof(*e));
        strlcpy(e->name, name, sizeof(e->name));
        e->dir = type == DIRENT_DIR;
        char full[320];
        join(full, sizeof(full), g_path, name);
        app_stat_t st;
        if (fstat_path(full, &st) == 0) { e->size = (u32)st.size; e->mtime = (u32)st.mtime; }
    }
    free(buf);
    qsort(g_e, (size_t)g_n, sizeof(entry_t), cmp_entry);
    disp_path(g_addr, sizeof(g_addr));
}

static void push_hist(void) {
    char cur[256];
    strlcpy(cur, g_pc ? "" : g_path, sizeof(cur));
    if (g_hpos >= 0 && !strcmp(g_hist[g_hpos], cur)) return;
    g_hpos++;
    if (g_hpos >= 32) { memmove(g_hist, g_hist + 1, sizeof(g_hist[0]) * 31); g_hpos = 31; }
    strlcpy(g_hist[g_hpos], cur, 256);
    g_hcount = g_hpos + 1;
}

static void go(const char *path) {
    if (!path[0]) { g_pc = 1; strlcpy(g_path, "/", sizeof(g_path)); }
    else {
        if (!path_is_dir(path)) { snprintf(g_status, sizeof(g_status), "Cannot open %s", path); ui_dirty(); return; }
        g_pc = 0;
        strlcpy(g_path, path, sizeof(g_path));
    }
    push_hist();
    g_confirm_del = g_newdir = 0;
    load_dir();
    g_status[0] = 0;
    ui_dirty();
}

static void go_hist(int pos) {
    if (pos < 0 || pos >= g_hcount) return;
    g_hpos = pos;
    const char *p = g_hist[pos];
    if (!p[0]) { g_pc = 1; strlcpy(g_path, "/", sizeof(g_path)); }
    else { g_pc = 0; strlcpy(g_path, p, sizeof(g_path)); }
    g_confirm_del = g_newdir = 0;
    load_dir();
    ui_dirty();
}

static void go_up(void) {
    if (g_pc) return;
    if (!strcmp(g_path, "/")) return;
    char t[256];
    strlcpy(t, g_path, sizeof(t));
    char *s = strrchr(t, '/');
    if (s == t) strlcpy(t, "/", sizeof(t)); else if (s) *s = 0;
    go(t);
}

static int shell(const char *cmd) {
    strlcpy(g_ncwd, g_pc ? "/" : g_path, sizeof(g_ncwd));
    static char out[2048];
    long n = shell_run(g_ncwd, cmd, out, sizeof(out) - 1);
    if (n < 0) { strlcpy(g_status, "command failed", sizeof(g_status)); return -1; }
    char *e = out + strlen(out);
    while (e > out && (e[-1] == '\n' || e[-1] == ' ')) e--;
    *e = 0;
    char *last = strrchr(out, '\n');
    strlcpy(g_status, last ? last + 1 : out, sizeof(g_status));
    return 0;
}

static void open_entry(int i) {
    entry_t *e = &g_e[i];
    char full[320];
    join(full, sizeof(full), g_path, e->name);
    if (e->dir) { go(full); return; }
    if (ext_is(e->name, "trp")) {
        if (app_spawn(full) < 0) snprintf(g_status, sizeof(g_status), "Could not start %s", e->name);
        else snprintf(g_status, sizeof(g_status), "Started %s", e->name);
        return;
    }
    if (is_image(e->name)) { g_viewer = 1; strlcpy(g_vpath, full, sizeof(g_vpath)); return; }
    if (is_text(e->name)) {
        long n = file_read_all(full, g_vtext, sizeof(g_vtext) - 1);
        if (n < 0) n = 0;
        g_vtext[n] = 0;
        for (long k = 0; k < n; k++) if (g_vtext[k] == '\r' || (g_vtext[k] != '\n' && (u8)g_vtext[k] < 32) || (u8)g_vtext[k] > 126) g_vtext[k] = g_vtext[k] == '\n' ? '\n' : ' ';
        g_viewer = 2;
        strlcpy(g_vpath, full, sizeof(g_vpath));
        return;
    }
    snprintf(g_status, sizeof(g_status), "No program is associated with %s", e->name);
}

static void do_delete(void) {
    if (g_sel < 0) return;
    char full[320], cmd[360];
    join(full, sizeof(full), g_path, g_e[g_sel].name);
    snprintf(cmd, sizeof(cmd), "rm %s", full);
    shell(cmd);
    g_confirm_del = 0;
    load_dir();
}

static void do_paste(void) {
    if (!g_clip[0] || g_pc) return;
    const char *b = strrchr(g_clip, '/');
    char dst[320], cmd[640];
    join(dst, sizeof(dst), g_path, b ? b + 1 : g_clip);
    snprintf(cmd, sizeof(cmd), "copy %s %s", g_clip, dst);
    shell(cmd);
    load_dir();
}

#define CTX_W 190
#define CTX_H 26

static int ctx_items(char *keys, const char **labels) {
    entry_t *e = &g_e[g_ctx_i];
    int n = 0;
    keys[n] = 'o'; labels[n++] = "Open";
    if (!e->dir && !ext_is(e->name, "trp")) { keys[n] = 'n'; labels[n++] = "Open with Notepad"; }
    if (!e->dir && (is_image(e->name) || is_text(e->name))) { keys[n] = 'v'; labels[n++] = "Open with Viewer"; }
    if (!e->dir) { keys[n] = 'c'; labels[n++] = "Copy"; }
    if (g_clip[0]) { keys[n] = 'p'; labels[n++] = "Paste"; }
    keys[n] = 'd'; labels[n++] = "Delete";
    return n;
}

static void ctx_run(char k) {
    entry_t *e = &g_e[g_ctx_i];
    char full[320];
    join(full, sizeof(full), g_path, e->name);
    if (k == 'o') open_entry(g_ctx_i);
    else if (k == 'n') {
        if (file_write_all("/toriginal_os/notepad_open", full, strlen(full), 0) != 0) strlcpy(g_status, "Could not pass the file to Notepad", sizeof(g_status));
        else if (app_spawn("/system/trp/notepad.trp") < 0) strlcpy(g_status, "Could not start Notepad", sizeof(g_status));
        else snprintf(g_status, sizeof(g_status), "Opened %s in Notepad", e->name);
    } else if (k == 'v') open_entry(g_ctx_i);
    else if (k == 'c') { strlcpy(g_clip, full, sizeof(g_clip)); snprintf(g_status, sizeof(g_status), "Copied %s", e->name); }
    else if (k == 'p') do_paste();
    else if (k == 'd') g_confirm_del = 1;
}

static void ctx_click(void) {
    if (!g_ctx || g_ctx_i < 0 || g_ctx_i >= g_n) { g_ctx = 0; return; }
    char keys[8]; const char *lb[8];
    int n = ctx_items(keys, lb);
    for (int i = 0; i < n; i++) {
        if (ui_hit(g_ctx_x, g_ctx_y + i * CTX_H, CTX_W, CTX_H)) { g_ctx = 0; ctx_run(keys[i]); return; }
    }
    g_ctx = 0;
}

static void ctx_draw(void) {
    if (!g_ctx || g_ctx_i < 0 || g_ctx_i >= g_n) return;
    char keys[8]; const char *lb[8];
    int n = ctx_items(keys, lb);
    int x = g_ctx_x, y = g_ctx_y;
    gui_fill(x - 1, y - 1, CTX_W + 2, n * CTX_H + 2, COL_BORDER);
    gui_fill(x, y, CTX_W, n * CTX_H, COL_PANEL);
    for (int i = 0; i < n; i++) {
        int hot = ui_hit(x, y + i * CTX_H, CTX_W, CTX_H);
        if (hot) gui_fill(x, y + i * CTX_H, CTX_W, CTX_H, COL_ACCENT_L);
        ui_label(x + 12, y + i * CTX_H + 5, lb[i], COL_TEXT);
    }
}

static void ctx_open(int i) {
    g_sel = i;
    g_ctx = 1;
    g_ctx_i = i;
    int n = 6;
    g_ctx_x = ui.mx;
    g_ctx_y = ui.my;
    if (g_ctx_x + CTX_W > g_win_w) g_ctx_x = g_win_w - CTX_W - 2;
    if (g_ctx_y + n * CTX_H > g_win_h) g_ctx_y = g_win_h - n * CTX_H - 2;
    if (g_ctx_y < 0) g_ctx_y = 0;
    ui.rclick = 0;
}

static int rows_visible(void) { return (g_win_h - TOP_H - 28 - (g_icons_view ? 0 : 24)) / (g_icons_view ? TILE_H : ROW_H); }
static int tiles_per_row(void) { int c = (g_win_w - NAV_W - 24) / TILE_W; return c < 1 ? 1 : c; }
static int total_rows(void) { return g_icons_view ? (g_n + tiles_per_row() - 1) / tiles_per_row() : g_n; }

static void clamp_scroll(void) {
    int maxs = total_rows() - rows_visible();
    if (maxs < 0) maxs = 0;
    if (g_scroll > maxs) g_scroll = maxs;
    if (g_scroll < 0) g_scroll = 0;
}

static void ensure_visible(void) {
    if (g_sel < 0) return;
    int row = g_icons_view ? g_sel / tiles_per_row() : g_sel;
    if (row < g_scroll) g_scroll = row;
    if (row >= g_scroll + rows_visible()) g_scroll = row - rows_visible() + 1;
    clamp_scroll();
}

static void click_entry(int i) {
    u64 now = uptime_ms();
    if (g_click_i == i && now - g_click_t < 450) { g_click_i = -2; g_sel = i; open_entry(i); return; }
    g_click_i = i;
    g_click_t = now;
    g_sel = i;
}

static void draw_nav(void) {
    gui_fill(0, TOP_H, NAV_W, g_win_h - TOP_H, COL_SIDEBAR);
    gui_fill(NAV_W, TOP_H, 1, g_win_h - TOP_H, COL_BORDER);
    for (int i = 0; i < NNAV; i++) {
        char ic[64];
        snprintf(ic, sizeof(ic), ICON_DIR "%s", g_nav[i].icon);
        int sel = g_nav[i].path[0] ? (!g_pc && !strcmp(g_path, g_nav[i].path)) : g_pc;
        if (ui_item(6, TOP_H + 8 + i * 34, NAV_W - 12, 32, g_nav[i].label, ic, sel)) go(g_nav[i].path);
    }
}

static void draw_details(void) {
    int x0 = NAV_W + 1, y0 = TOP_H;
    int w = g_win_w - x0 - 14;
    gui_fill(x0, y0, w + 14, 24, COL_BG);
    gui_fill(x0, y0 + 23, w + 14, 1, COL_BORDER);
    int cn = x0 + 8, ct = x0 + w - COL_SIZE_W - COL_TYPE_W - COL_DATE_W, cd = x0 + w - COL_SIZE_W - COL_DATE_W, cs = x0 + w - COL_SIZE_W;
    ui_label(cn, y0 + 4, "Name", COL_DIM);
    ui_label(ct, y0 + 4, "Type", COL_DIM);
    ui_label(cd, y0 + 4, "Modified", COL_DIM);
    ui_label(cs, y0 + 4, "Size", COL_DIM);
    int vis = rows_visible();
    for (int r = 0; r < vis; r++) {
        int i = g_scroll + r;
        if (i >= g_n) break;
        entry_t *e = &g_e[i];
        int y = y0 + 24 + r * ROW_H;
        int hot = ui_hit(x0, y, w, ROW_H);
        if (i == g_sel) gui_fill(x0, y, w, ROW_H, COL_ACCENT_L);
        else if (hot) gui_fill(x0, y, w, ROW_H, COL_BTN_HOT);
        char ic[64];
        snprintf(ic, sizeof(ic), ICON_DIR "%s", icon_for(e));
        gui_image(cn, y + 2, 22, 22, ic);
        gui_text_s(cn + 30, y + 5, e->name, COL_TEXT, 1);
        ui_label(ct, y + 5, type_for(e), COL_DIM);
        char t[24];
        fmt_date(t, sizeof(t), e->mtime);
        ui_label(cd, y + 5, t, COL_DIM);
        if (!e->dir) { fmt_size(t, sizeof(t), e->size); ui_label(cs, y + 5, t, COL_DIM); }
        if (hot && ui.rclick && !g_ctx) ctx_open(i);
        if (hot && ui.click) { ui.click = 0; click_entry(i); ui.dirty = 1; }
    }
}

static void draw_tiles(void) {
    int x0 = NAV_W + 12, y0 = TOP_H + 8;
    int per = tiles_per_row();
    int vis = rows_visible();
    for (int r = 0; r < vis; r++) {
        for (int c = 0; c < per; c++) {
            int i = (g_scroll + r) * per + c;
            if (i >= g_n) break;
            entry_t *e = &g_e[i];
            int x = x0 + c * TILE_W, y = y0 + r * TILE_H;
            int hot = ui_hit(x, y, TILE_W - 4, TILE_H - 4);
            if (i == g_sel) gui_fill(x, y, TILE_W - 4, TILE_H - 4, COL_ACCENT_L);
            else if (hot) gui_fill(x, y, TILE_W - 4, TILE_H - 4, COL_BTN_HOT);
            char ic[64];
            snprintf(ic, sizeof(ic), ICON_DIR "%s", icon_for(e));
            gui_image(x + (TILE_W - 4 - 48) / 2, y + 6, 48, 48, ic);
            char nm[16];
            size_t L = strlen(e->name);
            if (L > 11) { memcpy(nm, e->name, 9); nm[9] = '.'; nm[10] = '.'; nm[11] = 0; } else strlcpy(nm, e->name, sizeof(nm));
            int tw = gui_text_w(nm, 1);
            ui_label(x + (TILE_W - 4 - tw) / 2, y + 62, nm, COL_TEXT);
            if (hot && ui.rclick && !g_ctx) ctx_open(i);
        if (hot && ui.click) { ui.click = 0; click_entry(i); ui.dirty = 1; }
        }
    }
}

static void draw_scrollbar(void) {
    int total = total_rows(), vis = rows_visible();
    if (total <= vis) return;
    int x = g_win_w - 12, y = TOP_H + (g_icons_view ? 0 : 24), h = g_win_h - y - 28;
    gui_fill(x, y, 10, h, COL_BTN);
    int bar = h * vis / total;
    if (bar < 20) bar = 20;
    int maxs = total - vis;
    int pos = (h - bar) * g_scroll / (maxs ? maxs : 1);
    gui_fill(x, y + pos, 10, bar, COL_BORDER);
    if (ui.down && (g_drag_sb || ui_hit(x, y, 10, h))) {
        g_drag_sb = 1;
        int ny = ui.my - y - bar / 2;
        g_scroll = ny * maxs / (h - bar > 0 ? h - bar : 1);
        clamp_scroll();
        ui.dirty = 1;
    } else g_drag_sb = 0;
}

static void draw_viewer(void) {
    int x = NAV_W + 30, y = TOP_H + 20, w = g_win_w - x - 30, h = g_win_h - y - 50;
    gui_fill(x, y, w, h, COL_PANEL);
    gui_frame(x, y, w, h, COL_ACCENT);
    const char *b = strrchr(g_vpath, '/');
    ui_label_s(x + 12, y + 10, b ? b + 1 : g_vpath, COL_TEXT, 1);
    if (g_viewer == 1) {
        int s = h - 90 < w - 24 ? h - 90 : w - 24;
        if (s > 512) s = 512;
        if (s > 32) gui_image(x + 12, y + 36, s, s, g_vpath);
        if (ui_button_primary(x + w - 330, y + h - 40, 150, 30, "Set as wallpaper")) {
            const char *n = strrchr(g_vpath, '/');
            if (!strncmp(g_vpath, "/system/gui/wallpapers/", 23) && n) {
                char c[160];
                snprintf(c, sizeof(c), "wallpaper set %s", n + 1);
                shell(c);
            } else strlcpy(g_status, "Only images in /system/gui/wallpapers can be set", sizeof(g_status));
        }
    } else {
        char *p = g_vtext;
        for (int line = 0; line < (h - 90) / 17 && *p; line++) {
            char *e = strchr(p, '\n');
            char tmp[120];
            size_t n = e ? (size_t)(e - p) : strlen(p);
            size_t maxc = (size_t)(w - 24) / FONT_W;
            if (n > maxc) n = maxc;
            if (n > sizeof(tmp) - 1) n = sizeof(tmp) - 1;
            memcpy(tmp, p, n);
            tmp[n] = 0;
            ui_label(x + 12, y + 38 + line * 17, tmp, COL_TEXT);
            if (!e) break;
            p = e + 1;
        }
    }
    if (ui_button(x + w - 170, y + h - 40, 150, 30, "Close")) g_viewer = 0;
}

static void draw(void) {
    ui.dirty = 0;
    if (g_ctx && ui.click) { ctx_click(); ui.click = 0; }
    if (g_ctx && ui.rclick) g_ctx = 0;
    gui_fill(0, 0, g_win_w, g_win_h, COL_PANEL);
    gui_fill(0, 0, g_win_w, TOP_H, COL_BG);
    gui_fill(0, TOP_H - 1, g_win_w, 1, COL_BORDER);

    if (ui_button(8, 8, 32, 28, "<") && g_hpos > 0) go_hist(g_hpos - 1);
    if (ui_button(44, 8, 32, 28, ">") && g_hpos + 1 < g_hcount) go_hist(g_hpos + 1);
    if (ui_button(80, 8, 32, 28, "^")) go_up();
    if (ui_button(g_win_w - 40, 8, 32, 28, "R")) { load_dir(); strlcpy(g_status, "Refreshed", sizeof(g_status)); }
    if (ui_field(1, 120, 8, g_win_w - 168, g_addr, sizeof(g_addr))) {
        const char *p = g_addr;
        char t[256];
        if (!strncmp(p, "root", 4)) p += 4;
        strlcpy(t, *p ? p : "/", sizeof(t));
        go(t);
        ui.focus = -1;
    }

    int bx = 8;
    if (ui_button(bx, 42, 100, 28, "New folder") && !g_pc) { g_newdir = 1; g_newname[0] = 0; ui.focus = 2; }
    bx += 108;
    if (ui_button(bx, 42, 70, 28, "Delete") && g_sel >= 0) g_confirm_del = 1;
    bx += 78;
    if (ui_button(bx, 42, 60, 28, "Copy") && g_sel >= 0 && !g_e[g_sel].dir) {
        join(g_clip, sizeof(g_clip), g_path, g_e[g_sel].name);
        snprintf(g_status, sizeof(g_status), "Copied %s", g_e[g_sel].name);
    }
    bx += 68;
    if (ui_button(bx, 42, 64, 28, "Paste") && g_clip[0]) do_paste();
    bx += 72;
    if (ui_button(bx, 42, 110, 28, g_icons_view ? "Details" : "Icons")) { g_icons_view = !g_icons_view; g_scroll = 0; }
    bx += 118;
    if (!g_pc && ui_button(bx, 42, 90, 28, "Open") && g_sel >= 0) open_entry(g_sel);

    draw_nav();
    {
        clamp_scroll();
        if (g_icons_view) draw_tiles(); else draw_details();
        draw_scrollbar();
        if (!g_n) ui_label(NAV_W + 24, TOP_H + 40, "This folder is empty.", COL_DIM);
    }

    int sy = g_win_h - 26;
    gui_fill(NAV_W + 1, sy, g_win_w - NAV_W - 1, 26, COL_SIDEBAR);
    gui_fill(NAV_W + 1, sy, g_win_w - NAV_W - 1, 1, COL_BORDER);
    char st[200];
    if (g_status[0]) strlcpy(st, g_status, sizeof(st));
    else if (g_sel >= 0 && g_sel < g_n) {
        char sz[24];
        fmt_size(sz, sizeof(sz), g_e[g_sel].size);
        snprintf(st, sizeof(st), "%s  -  %s%s%s", g_e[g_sel].name, type_for(&g_e[g_sel]), g_e[g_sel].dir ? "" : "  -  ", g_e[g_sel].dir ? "" : sz);
    } else snprintf(st, sizeof(st), "%d items", g_n);
    ui_label(NAV_W + 12, sy + 5, st, COL_DIM);

    if (g_confirm_del && g_sel >= 0) {
        int x = NAV_W + 30, y = g_win_h - 88;
        gui_fill(x, y, g_win_w - x - 30, 52, COL_PANEL);
        gui_frame(x, y, g_win_w - x - 30, 52, COL_BAD);
        char m[120];
        snprintf(m, sizeof(m), "Delete %s?", g_e[g_sel].name);
        ui_label(x + 12, y + 18, m, COL_TEXT);
        if (ui_button_primary(g_win_w - 270, y + 11, 100, 30, "Delete")) do_delete();
        if (ui_button(g_win_w - 160, y + 11, 100, 30, "Cancel")) g_confirm_del = 0;
    }
    if (g_newdir) {
        int x = NAV_W + 30, y = g_win_h - 88;
        gui_fill(x, y, g_win_w - x - 30, 52, COL_PANEL);
        gui_frame(x, y, g_win_w - x - 30, 52, COL_ACCENT);
        ui_label(x + 12, y + 18, "Folder name", COL_DIM);
        if (ui_field(2, x + 110, y + 14, 220, g_newname, sizeof(g_newname)) || ui_button_primary(g_win_w - 270, y + 11, 100, 30, "Create")) {
            if (g_newname[0]) {
                char cmd[100];
                snprintf(cmd, sizeof(cmd), "mkdir %s", g_newname);
                shell(cmd);
                load_dir();
            }
            g_newdir = 0;
            ui.focus = -1;
        }
        if (ui_button(g_win_w - 160, y + 11, 100, 30, "Cancel")) { g_newdir = 0; ui.focus = -1; }
    }
    if (g_viewer) draw_viewer();
    ctx_draw();
}

static void keys(void) {
    int k = ui.key;
    if (!k || ui.focus >= 0) return;
    ui.key = 0;
    if (g_ctx) { if (k == 27) g_ctx = 0; return; }
    if (g_viewer) { if (k == 27 || k == '\n') g_viewer = 0; return; }
    int per = g_icons_view ? tiles_per_row() : 1;
    if (k == KEY_DOWN && g_sel + per < g_n) g_sel += per;
    else if (k == KEY_DOWN && g_sel < 0 && g_n) g_sel = 0;
    else if (k == KEY_UP && g_sel - per >= 0) g_sel -= per;
    else if (k == KEY_LEFT && g_icons_view && g_sel > 0) g_sel--;
    else if (k == KEY_RIGHT && g_icons_view && g_sel + 1 < g_n) g_sel++;
    else if (k == KEY_HOME && g_n) g_sel = 0;
    else if (k == KEY_END && g_n) g_sel = g_n - 1;
    else if ((k == '\n' || k == '\r') && g_sel >= 0) open_entry(g_sel);
    else if (k == 8) go_up();
    else if (k == KEY_DEL && g_sel >= 0) g_confirm_del = 1;
    ensure_visible();
    ui.dirty = 1;
}

int main(void) {
    app_sysinfo_t si;
    int w = 860, h = 580;
    if (sysinfo(&si) == 0) {
        if ((int)si.fb_width - 40 < w) w = (int)si.fb_width - 40;
        if ((int)si.fb_height - 90 < h) h = (int)si.fb_height - 90;
    }
    if (gui_open(50, 36, w, h, "Computer") != 0) return 1;
    ui_splash("Loading computer...");
    g_pc = 0;
    push_hist();
    load_dir();
    ui.dirty = 1;
    while (!ui_closed()) {
        ui_pump();
        keys();
        u64 now = uptime_ms();
        if (ui.dirty) {
            draw();
            if (ui.dirty) draw();
        }
        app_yield();
    }
    return 0;
}
