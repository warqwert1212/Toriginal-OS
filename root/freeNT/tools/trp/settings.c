#include "trpgui.h"

#define ICON_DIR "/system/gui/icons/"
#define WALL_DIR "/system/gui/wallpapers"
#define OUTMAX 3072

enum { PG_DISPLAY, PG_COLORS, PG_WALLPAPER, PG_CURSORS, PG_NETWORK, PG_ABOUT, PG_COUNT };

static const char *g_page_name[PG_COUNT] = { "Display", "Colors", "Wallpaper", "Cursors", "Network", "About" };
static const char *g_page_icon[PG_COUNT] = { "170.png", "081.png", "146.png", "vista_accessibility.png", "098.png", "152.png" };

static const char *g_color_names[16] = {
    "black", "blue", "green", "cyan", "red", "magenta", "brown", "lightgrey",
    "darkgrey", "lightblue", "lightgreen", "lightcyan", "lightred", "lightmagenta", "yellow", "white"
};

static const u32 g_vga_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static const char *g_res_presets[] = {
    "800x600", "1024x768", "1280x720", "1280x800", "1280x1024", "1366x768", "1440x900", "1600x900", "1920x1080"
};
#define NRES ((int)(sizeof(g_res_presets) / sizeof(g_res_presets[0])))

static int g_page;
static char g_cwd[256] = "/";
static char g_out[OUTMAX];
static char g_status[160];
static char g_cur_res[24] = "?";
static int g_color_idx[3] = { 7, 0, 15 };
static int g_font_mono;
static char g_custom_res[24];
static char g_net_ip[20], g_net_mask[20], g_net_gw[20], g_net_dns[20];
static int g_net_mode;
static char g_ping_host[40];
static char g_wall[128][128];
static int g_nwall;
static int g_wall_sel = -1;
static int g_wall_page;
static char g_about[OUTMAX];

static void clean(char *s) {
    char *w = s;
    for (char *r = s; *r; r++) {
        if (r[0] == '$' && r[1] == '{') {
            char *e = strchr(r, '}');
            if (e) { r = e; continue; }
        }
        if (*r == '\r') continue;
        *w++ = *r;
    }
    *w = 0;
}

static void run_cmd(const char *cmd) {
    long n = shell_run(g_cwd, cmd, g_out, sizeof(g_out) - 1);
    if (n < 0) { strlcpy(g_out, "command failed", sizeof(g_out)); }
    clean(g_out);
}

static void set_status_from_out(void) {
    char *end = g_out + strlen(g_out);
    while (end > g_out && (end[-1] == '\n' || end[-1] == ' ')) end--;
    *end = 0;
    char *last = strrchr(g_out, '\n');
    strlcpy(g_status, last ? last + 1 : g_out, sizeof(g_status));
}

static void apply(const char *cmd) {
    run_cmd(cmd);
    set_status_from_out();
    ui_dirty();
}

static int color_index(const char *name) {
    for (int i = 0; i < 16; i++) if (!strcmp(name, g_color_names[i])) return i;
    return -1;
}

static void load_state(void) {
    run_cmd("settings");
    char *line = g_out;
    while (*line) {
        char *e = strchr(line, '\n');
        if (e) *e = 0;
        char *colon = strchr(line, ':');
        if (colon) {
            char *v = colon + 1;
            while (*v == ' ') v++;
            if (!strncmp(line, "resolution", 10)) strlcpy(g_cur_res, v, sizeof(g_cur_res));
            else if (!strncmp(line, "text color", 10)) { int i = color_index(v); if (i >= 0) g_color_idx[0] = i; }
            else if (!strncmp(line, "background color", 16)) { int i = color_index(v); if (i >= 0) g_color_idx[1] = i; }
            else if (!strncmp(line, "status bar color", 16)) { int i = color_index(v); if (i >= 0) g_color_idx[2] = i; }
            else if (!strncmp(line, "font", 4)) g_font_mono = !strncmp(v, "mono", 4);
        }
        if (!e) break;
        line = e + 1;
    }
    g_status[0] = 0;

    static char buf[16384];
    g_nwall = 0;
    int cnt = listdir(WALL_DIR, buf, sizeof(buf));
    char *p = buf;
    for (int i = 0; i < cnt && g_nwall < 128; i++) {
        int type = (u8)p[0];
        char *name = p + 1;
        p += 1 + strlen(name) + 1;
        if (type == DIRENT_DIR || strlen(name) >= 127) continue;
        strlcpy(g_wall[g_nwall++], name, sizeof(g_wall[0]));
    }
    for (int i = 0; i < g_nwall; i++)
        for (int j = i + 1; j < g_nwall; j++)
            if (strcmp(g_wall[i], g_wall[j]) > 0) { char t[128]; strlcpy(t, g_wall[i], 128); strlcpy(g_wall[i], g_wall[j], 128); strlcpy(g_wall[j], t, 128); }

    run_cmd("sysver");
    strlcpy(g_about, g_out, sizeof(g_about));
}

static void load_network(void) {
    run_cmd("ifconfig");
    g_net_ip[0] = g_net_mask[0] = g_net_gw[0] = g_net_dns[0] = 0;
    char *p;
    struct { const char *key; char *dst; } f[4] = { {"inet ", g_net_ip}, {"netmask ", g_net_mask}, {"gateway ", g_net_gw}, {"dns ", g_net_dns} };
    for (int i = 0; i < 4; i++) {
        p = strstr(g_out, f[i].key);
        if (!p) continue;
        p += strlen(f[i].key);
        int n = 0;
        while (*p && *p != ' ' && *p != '\n' && n < 19) f[i].dst[n++] = *p++;
        f[i].dst[n] = 0;
    }
}

static void icon(int x, int y, int s, const char *name) {
    char path[64];
    snprintf(path, sizeof(path), ICON_DIR "%s", name);
    gui_image(x, y, s, s, path);
}

static void row(int x, int y, const char *k, const char *v) {
    ui_label(x, y, k, COL_DIM);
    ui_label(x + 150, y, v, COL_TEXT);
}

static void page_display(int x, int y) {
    char t[64];
    snprintf(t, sizeof(t), "Current resolution: %s", g_cur_res);
    ui_label(x, y, t, COL_TEXT);
    ui_label(x, y + 22, "Applies immediately and is saved for the next boot.", COL_DIM);
    for (int i = 0; i < NRES; i++) {
        int bx = x + (i % 3) * 140, by = y + 54 + (i / 3) * 40;
        if (!strcmp(g_res_presets[i], g_cur_res)) gui_fill(bx - 2, by - 2, 134, 36, COL_ACCENT);
        if (ui_button(bx, by, 130, 32, g_res_presets[i])) {
            char c[48];
            snprintf(c, sizeof(c), "settings resolution %s", g_res_presets[i]);
            apply(c);
            strlcpy(g_cur_res, g_res_presets[i], sizeof(g_cur_res));
        }
    }
    int cy = y + 54 + 3 * 40 + 10;
    ui_label(x, cy, "Custom (WxH)", COL_DIM);
    ui_field(1, x, cy + 22, 150, g_custom_res, sizeof(g_custom_res));
    if (ui_button_primary(x + 162, cy + 22, 90, 24, "Apply") && g_custom_res[0]) {
        char c[64];
        snprintf(c, sizeof(c), "settings resolution %s", g_custom_res);
        apply(c);
    }
    int fy = cy + 66;
    ui_label(x, fy, "Console font", COL_DIM);
    if (ui_radio(x, fy + 22, "Default", !g_font_mono)) { g_font_mono = 0; apply("settings font default"); }
    if (ui_radio(x + 130, fy + 22, "Monospace", g_font_mono)) { g_font_mono = 1; apply("settings font mono"); }
}

static void page_colors(int x, int y) {
    static const char *labels[3] = { "Text color", "Background color", "Status bar color" };
    static const char *cmds[3] = { "textcolor", "bgcolor", "statusbarcolor" };
    ui_label(x, y, "Colors used by the text console and the Terminal app.", COL_DIM);
    for (int r = 0; r < 3; r++) {
        int ry = y + 34 + r * 90;
        char t[64];
        snprintf(t, sizeof(t), "%s: %s", labels[r], g_color_names[g_color_idx[r]]);
        ui_label(x, ry, t, COL_TEXT);
        for (int i = 0; i < 16; i++) {
            int sx = x + (i % 8) * 48, sy = ry + 24 + (i / 8) * 28;
            if (g_color_idx[r] == i) gui_fill(sx - 2, sy - 2, 44, 28, COL_ACCENT);
            gui_fill(sx, sy, 40, 24, g_vga_rgb[i]);
            gui_frame(sx, sy, 40, 24, COL_BORDER);
            if (ui_hit(sx, sy, 40, 24) && ui.click) {
                char c[64];
                snprintf(c, sizeof(c), "settings %s %d", cmds[r], i);
                g_color_idx[r] = i;
                ui.click = 0;
                apply(c);
            }
        }
    }
}

static void page_wallpaper(int x, int y) {
    int per = 9;
    int pages = (g_nwall + per - 1) / per;
    if (!g_nwall) { ui_label(x, y, "No wallpapers found in " WALL_DIR, COL_DIM); return; }
    for (int i = 0; i < per; i++) {
        int idx = g_wall_page * per + i;
        if (idx >= g_nwall) break;
        char disp[32];
        size_t nl = strlen(g_wall[idx]);
        if (nl > 30) { memcpy(disp, g_wall[idx], 28); disp[28] = '.'; disp[29] = '.'; disp[30] = 0; }
        else strlcpy(disp, g_wall[idx], sizeof(disp));
        if (ui_item(x, y + i * 30, 250, 28, disp, NULL, idx == g_wall_sel)) g_wall_sel = idx;
    }
    char t[40];
    snprintf(t, sizeof(t), "Page %d of %d", g_wall_page + 1, pages ? pages : 1);
    ui_label(x, y + per * 30 + 8, t, COL_DIM);
    if (ui_button(x + 120, y + per * 30 + 2, 60, 26, "<") && g_wall_page > 0) g_wall_page--;
    if (ui_button(x + 190, y + per * 30 + 2, 60, 26, ">") && g_wall_page + 1 < pages) g_wall_page++;

    int px = x + 280;
    gui_fill(px, y, 256, 148, COL_PANEL);
    gui_frame(px, y, 256, 148, COL_BORDER);
    if (g_wall_sel >= 0) {
        char path[192];
        snprintf(path, sizeof(path), WALL_DIR "/%s", g_wall[g_wall_sel]);
        gui_image(px + 2, y + 2, 252, 144, path);
        char nm[36];
        if (strlen(g_wall[g_wall_sel]) > 34) { memcpy(nm, g_wall[g_wall_sel], 32); nm[32] = '.'; nm[33] = '.'; nm[34] = 0; }
        else strlcpy(nm, g_wall[g_wall_sel], sizeof(nm));
        ui_label(px, y + 156, nm, COL_TEXT);
        if (ui_button_primary(px, y + 184, 150, 32, "Set wallpaper")) {
            char c[160];
            snprintf(c, sizeof(c), "wallpaper set %s", g_wall[g_wall_sel]);
            apply(c);
        }
    } else ui_label(px + 20, y + 66, "Select a wallpaper", COL_DIM);
    if (ui_button(px + 160, y + 184, 96, 32, "Next")) apply("wallpaper next");
}

#define CUR_DIR "/system/gui/cursors/"
#define NROLE 8
#define MAXCUR 256
#define GRID_COLS 8
#define GRID_ROWS 5

static const char *g_role_label[NROLE] = {
    "Normal select", "Link / pressed", "Resize vertical", "Resize horizontal",
    "Resize diagonal \\", "Resize diagonal /", "Move", "Busy"
};
static const char *g_role_key[NROLE] = {
    "cursor_normal", "cursor_link", "cursor_resize_ns", "cursor_resize_we",
    "cursor_resize_nwse", "cursor_resize_nesw", "cursor_move", "cursor_busy"
};
static const char *g_role_def[NROLE] = {
    "default_arrow", "default_link", "default_size4", "default_size3",
    "default_size2", "default_size1", "default_move", "default_busy"
};
static const char *g_cur_presets[][NROLE + 1] = {
    { "Windows XP", "default_arrow", "default_link", "default_size4", "default_size3", "default_size2", "default_size1", "default_move", "default_busy" },
    { "3D Gold", "3dgarro", "default_link", "3dgns", "3dgwe", "3dgnwse", "3dgnesw", "3dgmove", "hourglas_st" },
    { "3D Silver", "3dsarro", "default_link", "3dsns", "3dswe", "3dsnwse", "3dsnesw", "3dsmove", "hourglas_st" },
    { "3D White", "3dwarro", "default_link", "3dwns", "3dwwe", "3dwnwse", "3dwnesw", "3dwmove", "hourglas_st" },
    { "High contrast", "harrow", "default_link", "hns", "hwe", "hnwse", "hnesw", "hmove", "hourglas_st" },
    { "Large", "larrow", "llink", "lns", "lwe", "lnwse", "lnesw", "lmove", "lwait" },
};
#define NPRESET 6
static const int g_cur_sizes[4] = { 24, 32, 48, 64 };
static const char *g_cur_size_name[4] = { "Small", "Normal", "Large", "Huge" };

static char g_cr[NROLE][48];
static int g_csize = 32;
static int g_csel;
static int g_cpage;
static char g_cnames[MAXCUR][32];
static int g_ncur;

static void load_cursors(void) {
    for (int i = 0; i < NROLE; i++) {
        char b[48];
        if (cfg_get(g_role_key[i], b, sizeof(b)) == 0 && b[0]) strlcpy(g_cr[i], b, sizeof(g_cr[i]));
        else strlcpy(g_cr[i], g_role_def[i], sizeof(g_cr[i]));
    }
    char sb[16];
    g_csize = 32;
    if (cfg_get("cursor_size", sb, sizeof(sb)) == 0 && sb[0]) {
        int v = 0;
        for (char *p = sb; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0');
        if (v >= 16 && v <= 96) g_csize = v;
    }
    static char buf[12288];
    g_ncur = 0;
    int cnt = listdir(CUR_DIR, buf, sizeof(buf));
    char *p = buf;
    for (int i = 0; i < cnt && g_ncur < MAXCUR; i++) {
        int type = (u8)p[0];
        char *name = p + 1;
        p += 1 + strlen(name) + 1;
        size_t l = strlen(name);
        if (type == DIRENT_DIR || l < 5 || l >= 36 || strcmp(name + l - 4, ".png")) continue;
        strlcpy(g_cnames[g_ncur], name, sizeof(g_cnames[0]));
        g_cnames[g_ncur][l - 4] = 0;
        g_ncur++;
    }
    for (int i = 0; i < g_ncur; i++)
        for (int j = i + 1; j < g_ncur; j++)
            if (strcmp(g_cnames[i], g_cnames[j]) > 0) { char t[32]; strlcpy(t, g_cnames[i], 32); strlcpy(g_cnames[i], g_cnames[j], 32); strlcpy(g_cnames[j], t, 32); }
}

static void cur_show(int x, int y, int s, const char *stem) {
    char path[96];
    snprintf(path, sizeof(path), CUR_DIR "%s.png", stem);
    gui_image(x, y, s, s, path);
}

static void cur_reload(const char *msg) {
    run_cmd("cursor reload");
    strlcpy(g_status, msg, sizeof(g_status));
    ui_dirty();
}

static void cur_set_role(int r, const char *stem) {
    strlcpy(g_cr[r], stem, sizeof(g_cr[r]));
    cfg_set(g_role_key[r], stem);
}

static void page_cursors(int x, int y) {
    for (int i = 0; i < NPRESET; i++) {
        int bx = x + (i % 3) * 132, by = y + (i / 3) * 34;
        if (ui_button(bx, by, 126, 28, g_cur_presets[i][0])) {
            for (int r = 0; r < NROLE; r++) cur_set_role(r, g_cur_presets[i][r + 1]);
            char m[64];
            snprintf(m, sizeof(m), "Cursor theme: %s", g_cur_presets[i][0]);
            cur_reload(m);
        }
    }
    int ry = y + 76;
    for (int r = 0; r < NROLE; r++) {
        int yy = ry + r * 38;
        if (r == g_csel) gui_fill(x - 4, yy - 2, 246, 36, COL_ACCENT_L);
        cur_show(x, yy, 32, g_cr[r]);
        ui_label(x + 42, yy + 8, g_role_label[r], COL_TEXT);
        if (ui_hit(x - 4, yy - 2, 246, 36) && ui.click) { g_csel = r; ui.click = 0; ui_dirty(); }
    }
    int sy = ry + NROLE * 38 + 8;
    ui_label(x, sy, "Size", COL_DIM);
    for (int i = 0; i < 4; i++) {
        int bx = x + 44 + i * 50;
        if (g_csize == g_cur_sizes[i]) gui_fill(bx - 2, sy - 6, 48, 28, COL_ACCENT);
        if (ui_button(bx, sy - 4, 44, 24, g_cur_size_name[i])) {
            char v[8];
            snprintf(v, sizeof(v), "%d", g_cur_sizes[i]);
            g_csize = g_cur_sizes[i];
            cfg_set("cursor_size", v);
            cur_reload("Cursor size changed");
        }
    }

    int gx = x + 262;
    char t[96];
    snprintf(t, sizeof(t), "Pick a cursor for: %s", g_role_label[g_csel]);
    ui_label(gx, ry, t, COL_TEXT);
    int per = GRID_COLS * GRID_ROWS;
    int pages = (g_ncur + per - 1) / per;
    if (g_cpage >= pages) g_cpage = pages ? pages - 1 : 0;
    for (int i = 0; i < per; i++) {
        int idx = g_cpage * per + i;
        if (idx >= g_ncur) break;
        int cx = gx + (i % GRID_COLS) * 42, cy = ry + 26 + (i / GRID_COLS) * 42;
        int cur = !strcmp(g_cnames[idx], g_cr[g_csel]);
        if (cur) gui_fill(cx - 2, cy - 2, 40, 40, COL_ACCENT);
        else if (ui_hit(cx, cy, 36, 36)) gui_fill(cx - 2, cy - 2, 40, 40, COL_BTN);
        cur_show(cx, cy, 36, g_cnames[idx]);
        if (ui_hit(cx, cy, 36, 36) && ui.click) {
            ui.click = 0;
            cur_set_role(g_csel, g_cnames[idx]);
            cur_reload(g_cnames[idx]);
        }
    }
    int py = ry + 26 + GRID_ROWS * 42 + 6;
    snprintf(t, sizeof(t), "Page %d of %d", g_cpage + 1, pages ? pages : 1);
    ui_label(gx, py + 6, t, COL_DIM);
    if (ui_button(gx + 150, py, 60, 26, "<") && g_cpage > 0) g_cpage--;
    if (ui_button(gx + 216, py, 60, 26, ">") && g_cpage + 1 < pages) g_cpage++;
    ui_label(gx, py + 38, g_cr[g_csel], COL_TEXT);
    if (ui_button(gx, py + 66, 150, 28, "Reset this one")) {
        cur_set_role(g_csel, g_role_def[g_csel]);
        cur_reload("Reset to default");
    }
}

static void page_network(int x, int y) {
    ui_label(x, y, "Address", COL_DIM);
    ui_field(2, x, y + 20, 150, g_net_ip, sizeof(g_net_ip));
    ui_label(x + 170, y, "Netmask", COL_DIM);
    ui_field(3, x + 170, y + 20, 150, g_net_mask, sizeof(g_net_mask));
    ui_label(x, y + 56, "Gateway", COL_DIM);
    ui_field(4, x, y + 76, 150, g_net_gw, sizeof(g_net_gw));
    ui_label(x + 170, y + 56, "DNS", COL_DIM);
    ui_field(5, x + 170, y + 76, 150, g_net_dns, sizeof(g_net_dns));
    if (ui_button_primary(x + 340, y + 20, 150, 24, "Apply now")) {
        char c[128];
        snprintf(c, sizeof(c), "ifconfig %s %s %s %s", g_net_ip, g_net_mask, g_net_gw, g_net_dns);
        apply(c);
    }
    if (ui_button(x + 340, y + 76, 150, 24, "Refresh")) { load_network(); strlcpy(g_status, "refreshed", sizeof(g_status)); }

    ui_label(x, y + 116, "At boot", COL_DIM);
    if (ui_radio(x, y + 138, "Off", g_net_mode == 0)) g_net_mode = 0;
    if (ui_radio(x + 90, y + 138, "DHCP", g_net_mode == 1)) g_net_mode = 1;
    if (ui_radio(x + 190, y + 138, "Static (uses the fields above)", g_net_mode == 2)) g_net_mode = 2;
    if (ui_button(x + 340, y + 112, 150, 24, "Save boot setting")) {
        char c[160];
        if (g_net_mode == 0) snprintf(c, sizeof(c), "settings network off");
        else if (g_net_mode == 1) snprintf(c, sizeof(c), "settings network dhcp");
        else snprintf(c, sizeof(c), "settings network static %s %s %s %s", g_net_ip, g_net_mask, g_net_gw, g_net_dns);
        apply(c);
    }

    ui_label(x, y + 180, "Ping", COL_DIM);
    ui_field(6, x, y + 200, 220, g_ping_host, sizeof(g_ping_host));
    if (ui_button(x + 232, y + 200, 90, 24, "Ping") && g_ping_host[0]) {
        char c[64];
        snprintf(c, sizeof(c), "ping %s", g_ping_host);
        apply(c);
    }
    if (g_out[0] && g_page == PG_NETWORK) {
        gui_fill(x, y + 238, 500, 110, COL_PANEL);
        gui_frame(x, y + 238, 500, 110, COL_BORDER);
        char *p = g_out;
        for (int line = 0; line < 6 && *p; line++) {
            char *e = strchr(p, '\n');
            char tmp[64];
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (n > 60) n = 60;
            memcpy(tmp, p, n);
            tmp[n] = 0;
            ui_label(x + 8, y + 244 + line * 17, tmp, COL_TEXT);
            if (!e) break;
            p = e + 1;
        }
    }
}

static void page_about(int x, int y) {
    icon(x, y, 64, "142.png");
    ui_label_s(x + 80, y + 8, "Toriginal OS", COL_TEXT, 2);
    {
        char bl[16] = "?";
        long bn = file_read_all("/system/osbuild", bl, sizeof(bl) - 1);
        if (bn > 0) { bl[bn] = 0; for (long i = 0; i < bn; i++) if (bl[i] == '\n' || bl[i] == '\r') bl[i] = 0; }
        char bt[32];
        snprintf(bt, sizeof(bt), "Build %s", bl);
        ui_label_s(x + 300, y + 8, bt, COL_ACCENT, 2);
    }
    ui_label(x + 80, y + 40, "Built from scratch: freeNT kernel, TRBL bootloader, TRPFS", COL_DIM);
    gui_fill(x, y + 84, 500, 180, COL_PANEL);
    gui_frame(x, y + 84, 500, 180, COL_BORDER);
    char *p = g_about;
    for (int line = 0; line < 10 && *p; line++) {
        char *e = strchr(p, '\n');
        char tmp[64];
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n > 60) n = 60;
        memcpy(tmp, p, n);
        tmp[n] = 0;
        ui_label(x + 10, y + 92 + line * 17, tmp, COL_TEXT);
        if (!e) break;
        p = e + 1;
    }
}

static void draw(void) {
    ui.dirty = 0;
    gui_fill(0, 0, g_win_w, g_win_h, COL_BG);
    gui_fill(0, 0, 196, g_win_h, COL_SIDEBAR);
    gui_fill(196, 0, 1, g_win_h, COL_BORDER);
    ui_label_s(16, 14, "Settings", COL_TEXT, 2);
    for (int i = 0; i < PG_COUNT; i++) {
        char path[64];
        snprintf(path, sizeof(path), ICON_DIR "%s", g_page_icon[i]);
        if (ui_item(8, 56 + i * 44, 180, 40, g_page_name[i], path, g_page == i)) {
            g_page = i;
            g_status[0] = 0;
            if (i == PG_NETWORK) load_network();
            if (i == PG_CURSORS) load_cursors();
            if (i != PG_NETWORK) g_out[0] = 0;
        }
    }
    if (ui_button(12, g_win_h - 122, 172, 30, "Welcome")) app_spawn("/system/trp/welcome.trp");
    if (ui_button(12, g_win_h - 84, 172, 30, "Terminal")) app_spawn("/system/trp/terminal.trp");
    if (ui_button(12, g_win_h - 46, 172, 30, "Package Manager")) app_spawn("/system/trp/packages.trp");

    int x = 224, y = 20;
    icon(x, y, 36, g_page_icon[g_page]);
    ui_label_s(x + 48, y + 6, g_page_name[g_page], COL_TEXT, 2);
    int cy = y + 64;
    switch (g_page) {
    case PG_DISPLAY: page_display(x, cy); break;
    case PG_COLORS: page_colors(x, cy); break;
    case PG_WALLPAPER: page_wallpaper(x, cy); break;
    case PG_CURSORS: page_cursors(x, cy); break;
    case PG_NETWORK: page_network(x, cy); break;
    case PG_ABOUT: page_about(x, cy); break;
    }
    if (g_status[0]) {
        gui_fill(197, g_win_h - 26, g_win_w - 197, 26, COL_SIDEBAR);
        ui_label(208, g_win_h - 22, g_status, COL_DIM);
    }
}

int main(void) {
    app_sysinfo_t si;
    int w = 800, h = 560;
    if (sysinfo(&si) == 0) {
        if ((int)si.fb_width - 40 < w) w = (int)si.fb_width - 40;
        if ((int)si.fb_height - 90 < h) h = (int)si.fb_height - 90;
    }
    if (gui_open(40, 30, w, h, "Settings") != 0) return 1;
    ui_splash("Loading settings...");
    load_state();
    load_network();
    load_cursors();
    g_out[0] = 0;
    ui.dirty = 1;
    while (!ui_closed()) {
        ui_pump();
        u64 now = uptime_ms();
        if (ui.dirty) {
            draw();
            if (ui.dirty) draw();
        }
        app_yield();
    }
    return 0;
}
