#include "trpgui.h"

#define ICON_DIR "/system/gui/icons/"
#define MAXPKG 96

typedef struct {
    char name[48];
    char version[16];
    char desc[100];
    char window[40];
    int gui;
    int src;
} pkg_t;

enum { SRC_BUILTIN, SRC_INSTALLED, SRC_AVAILABLE };
enum { TAB_INSTALLED, TAB_AVAILABLE, TAB_REPO };

static pkg_t g_pkgs[MAXPKG];
static int g_npkg;
static int g_tab;
static int g_sel = -1;
static int g_page;
static char g_cwd[256] = "/";
static char g_log[2048];
static char g_status[120];
static char g_file[96];
static char g_repo[96];
static char g_search[32];
static int g_shown[MAXPKG];
static int g_nshown;

static void strip(char *s) {
    char *w = s;
    for (char *r = s; *r; r++) {
        if (r[0] == '$' && r[1] == '{') { char *e = strchr(r, '}'); if (e) { r = e; continue; } }
        if (*r == '\r') continue;
        *w++ = *r;
    }
    *w = 0;
}

static void get_key(const char *text, const char *key, char *out, size_t max) {
    out[0] = 0;
    size_t kl = strlen(key);
    for (const char *p = text; *p; ) {
        const char *e = strchr(p, '\n');
        size_t ll = e ? (size_t)(e - p) : strlen(p);
        if (ll > kl && !strncasecmp(p, key, kl)) {
            const char *v = p + kl;
            while (*v == ' ' || *v == '\t') v++;
            size_t n = (size_t)(p + ll - v);
            if (n >= max) n = max - 1;
            memcpy(out, v, n);
            out[n] = 0;
            while (n && (out[n - 1] == '\r' || out[n - 1] == ' ')) out[--n] = 0;
            return;
        }
        if (!e) break;
        p = e + 1;
    }
}

static int read_manifest(const char *path, pkg_t *pk) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    u32 hdr[6];
    if (read(fd, hdr, sizeof(hdr)) != (long)sizeof(hdr) || hdr[0] != 0x4B505254u || hdr[3] == 0 || hdr[3] > 3000) { close(fd); return -1; }
    char text[3072];
    lseek(fd, (long)hdr[2], SEEK_SET);
    long n = read(fd, text, hdr[3]);
    close(fd);
    if (n <= 0) return -1;
    text[n] = 0;
    char tmp[32];
    get_key(text, "Version/:", pk->version, sizeof(pk->version));
    get_key(text, "Description/:", pk->desc, sizeof(pk->desc));
    get_key(text, "Window name/:", pk->window, sizeof(pk->window));
    get_key(text, "Appstat/:", tmp, sizeof(tmp));
    pk->gui = !strcasecmp(tmp, "gui");
    return 0;
}

static int has_pkg(const char *name, int src) {
    for (int i = 0; i < g_npkg; i++) if (g_pkgs[i].src == src && !strcmp(g_pkgs[i].name, name)) return 1;
    return 0;
}

static void scan_dir(const char *dir, int src) {
    char buf[4096];
    int cnt = listdir(dir, buf, sizeof(buf));
    if (cnt < 0) return;
    char *p = buf;
    for (int i = 0; i < cnt && g_npkg < MAXPKG; i++) {
        int type = (u8)p[0];
        char *name = p + 1;
        p += 1 + strlen(name) + 1;
        size_t l = strlen(name);
        if (type == DIRENT_DIR || l < 5 || l > 47 || strcmp(name + l - 4, ".trp")) continue;
        pkg_t pk;
        memset(&pk, 0, sizeof(pk));
        memcpy(pk.name, name, l - 4);
        pk.src = src;
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        if (read_manifest(path, &pk) != 0) continue;
        if (src == SRC_AVAILABLE && (has_pkg(pk.name, SRC_INSTALLED) || has_pkg(pk.name, SRC_BUILTIN))) continue;
        g_pkgs[g_npkg++] = pk;
    }
}

static void refresh(void) {
    g_npkg = 0;
    scan_dir("/system/trp", SRC_BUILTIN);
    scan_dir("/pkgs", SRC_INSTALLED);
    scan_dir("/packages", SRC_AVAILABLE);
    for (int i = 0; i < g_npkg; i++)
        for (int j = i + 1; j < g_npkg; j++)
            if (strcmp(g_pkgs[i].name, g_pkgs[j].name) > 0) { pkg_t t = g_pkgs[i]; g_pkgs[i] = g_pkgs[j]; g_pkgs[j] = t; }
    g_sel = -1;
    g_page = 0;
}

static void rebuild_view(void) {
    g_nshown = 0;
    for (int i = 0; i < g_npkg; i++) {
        int want = g_tab == TAB_INSTALLED ? (g_pkgs[i].src != SRC_AVAILABLE) : (g_pkgs[i].src == SRC_AVAILABLE);
        if (!want) continue;
        if (g_search[0] && !strstr(g_pkgs[i].name, g_search)) continue;
        g_shown[g_nshown++] = i;
    }
}

static void run_trpm(const char *args) {
    char cmd[200];
    snprintf(cmd, sizeof(cmd), "trpm %s", args);
    long n = shell_run(g_cwd, cmd, g_log, sizeof(g_log) - 1);
    if (n < 0) strlcpy(g_log, "trpm could not run", sizeof(g_log));
    strip(g_log);
    char tmp[2048];
    strlcpy(tmp, g_log, sizeof(tmp));
    char *end = tmp + strlen(tmp);
    while (end > tmp && (end[-1] == '\n' || end[-1] == ' ')) end--;
    *end = 0;
    char *last = strrchr(tmp, '\n');
    strlcpy(g_status, last ? last + 1 : tmp, sizeof(g_status));
    refresh();
    ui_dirty();
}

static void load_repo(void) {
    g_repo[0] = 0;
    long n = file_read_all("/trpm_repo.txt", g_repo, sizeof(g_repo) - 1);
    if (n < 0) n = 0;
    g_repo[n] = 0;
    while (n > 0 && (g_repo[n - 1] == '\n' || g_repo[n - 1] == '\r')) g_repo[--n] = 0;
}

static void draw_log(int x, int y, int w, int h) {
    gui_fill(x, y, w, h, COL_PANEL);
    gui_frame(x, y, w, h, COL_BORDER);
    int maxl = (h - 8) / 17, total = 0;
    for (char *p = g_log; *p; p++) if (*p == '\n') total++;
    int skip = total > maxl ? total - maxl : 0;
    char *p = g_log;
    for (int i = 0; i < skip && p; i++) { p = strchr(p, '\n'); if (p) p++; }
    for (int line = 0; line < maxl && p && *p; line++) {
        char *e = strchr(p, '\n');
        char tmp[80];
        size_t n = e ? (size_t)(e - p) : strlen(p);
        int cols = (w - 16) / FONT_W;
        if (n > (size_t)cols) n = (size_t)cols;
        if (n > 78) n = 78;
        memcpy(tmp, p, n);
        tmp[n] = 0;
        ui_label(x + 8, y + 5 + line * 17, tmp, COL_TEXT);
        if (!e) break;
        p = e + 1;
    }
}

static void draw_list(int x, int y, int w) {
    int per = 8, rowh = 46;
    int pages = (g_nshown + per - 1) / per;
    if (g_page >= pages && pages) g_page = pages - 1;
    if (!g_nshown) {
        ui_label(x + 8, y + 12, g_tab == TAB_INSTALLED ? "Nothing installed yet." : "No packages found in /packages.", COL_DIM);
        if (g_tab == TAB_AVAILABLE) ui_label(x + 8, y + 34, "Copy .trp files there, or install from a file below.", COL_DIM);
    }
    for (int i = 0; i < per; i++) {
        int k = g_page * per + i;
        if (k >= g_nshown) break;
        pkg_t *pk = &g_pkgs[g_shown[k]];
        int ry = y + i * rowh;
        char path[96], label[80];
        snprintf(path, sizeof(path), ICON_DIR "052.png");
        snprintf(label, sizeof(label), "%s %s", pk->name, pk->version);
        if (ui_item(x, ry, w, rowh - 4, label, path, g_sel == g_shown[k])) g_sel = g_shown[k];
        const char *tag = pk->src == SRC_BUILTIN ? "built-in" : pk->src == SRC_INSTALLED ? "installed" : "available";
        ui_label(x + w - (int)strlen(tag) * FONT_W - 10, ry + 14, tag, pk->src == SRC_INSTALLED ? COL_GOOD : COL_DIM);
    }
    if (pages > 1) {
        char t[32];
        snprintf(t, sizeof(t), "%d / %d", g_page + 1, pages);
        ui_label(x + w / 2 - 20, y + per * rowh + 8, t, COL_DIM);
        if (ui_button(x, y + per * rowh + 2, 50, 26, "<") && g_page > 0) g_page--;
        if (ui_button(x + w - 50, y + per * rowh + 2, 50, 26, ">") && g_page + 1 < pages) g_page++;
    }
}

static void draw_details(int x, int y, int w) {
    if (g_sel < 0 || g_sel >= g_npkg) { ui_label(x, y + 8, "Select a package.", COL_DIM); return; }
    pkg_t *pk = &g_pkgs[g_sel];
    gui_image(x, y, 64, 64, ICON_DIR "052.png");
    ui_label_s(x + 76, y + 6, pk->name, COL_TEXT, 2);
    char t[96];
    snprintf(t, sizeof(t), "Version %s  -  %s", pk->version[0] ? pk->version : "?", pk->gui ? "graphical app" : "terminal program");
    ui_label(x + 76, y + 40, t, COL_DIM);
    if (pk->desc[0]) {
        int cols = (w - 8) / FONT_W;
        char line[80];
        size_t n = strlen(pk->desc);
        for (size_t off = 0, ln = 0; off < n && ln < 3; off += (size_t)cols, ln++) {
            size_t c = n - off < (size_t)cols ? n - off : (size_t)cols;
            if (c > 78) c = 78;
            memcpy(line, pk->desc + off, c);
            line[c] = 0;
            ui_label(x, y + 78 + (int)ln * 17, line, COL_TEXT);
        }
    }
    int by = y + 140;
    if (pk->src == SRC_AVAILABLE) {
        if (ui_button_primary(x, by, 130, 32, "Install")) { char a[80]; snprintf(a, sizeof(a), "install %s", pk->name); run_trpm(a); }
    } else {
        if (ui_button_primary(x, by, 130, 32, "Open")) {
            char path[96];
            snprintf(path, sizeof(path), "%s/%s.trp", pk->src == SRC_BUILTIN ? "/system/trp" : "/pkgs", pk->name);
            if (pk->gui) { if (app_spawn(path) < 0) strlcpy(g_status, "could not start the app", sizeof(g_status)); else snprintf(g_status, sizeof(g_status), "started %s", pk->name); }
            else strlcpy(g_status, "terminal program: run it from Terminal with  trpm run <name>", sizeof(g_status));
            ui_dirty();
        }
        if (pk->src == SRC_INSTALLED) {
            if (ui_button(x + 140, by, 100, 32, "Update")) { char a[80]; snprintf(a, sizeof(a), "update %s", pk->name); run_trpm(a); }
            if (ui_button(x + 250, by, 100, 32, "Remove")) { char a[80]; snprintf(a, sizeof(a), "remove %s", pk->name); run_trpm(a); }
        }
    }
    if (ui_button(x, by + 42, 130, 26, "Details")) { char a[80]; snprintf(a, sizeof(a), "info %s", pk->name); run_trpm(a); }
}

static void draw(void) {
    ui.dirty = 0;
    gui_fill(0, 0, g_win_w, g_win_h, COL_BG);
    gui_fill(0, 0, g_win_w, 56, COL_SIDEBAR);
    gui_fill(0, 56, g_win_w, 1, COL_BORDER);
    gui_image(12, 10, 36, 36, ICON_DIR "052.png");
    ui_label_s(58, 16, "Packages", COL_TEXT, 2);

    static const char *tabs[3] = { "Installed", "Available", "Repository" };
    for (int i = 0; i < 3; i++) {
        int tx = g_win_w - 3 * 112 + i * 112 - 12;
        if (g_tab == i) gui_fill(tx, 14, 108, 32, COL_ACCENT_L);
        if (ui_button(tx + 2, 16, 104, 28, tabs[i])) { g_tab = i; g_page = 0; g_sel = -1; }
    }

    rebuild_view();
    int lx = 16, ly = 72, lw = g_win_w / 2 - 24;
    if (g_tab != TAB_REPO) {
        ui_label(lx, ly, "Search", COL_DIM);
        ui_field(1, lx + 60, ly - 4, lw - 60, g_search, sizeof(g_search));
        draw_list(lx, ly + 28, lw);
        draw_details(g_win_w / 2 + 8, ly, g_win_w / 2 - 24);
    } else {
        int x = 16;
        ui_label(x, ly, "Package repository (plain HTTP host[:port])", COL_DIM);
        ui_field(2, x, ly + 22, 360, g_repo, sizeof(g_repo));
        if (ui_button_primary(x + 372, ly + 22, 110, 24, "Save repo")) { char a[160]; snprintf(a, sizeof(a), "repo %s", g_repo); run_trpm(a); }
        if (ui_button(x + 492, ly + 22, 130, 24, "Update all")) run_trpm("update");
        ui_label(x, ly + 70, "Install a package by name from the repo, or from a file path", COL_DIM);
        ui_field(3, x, ly + 92, 360, g_file, sizeof(g_file));
        if (ui_button_primary(x + 372, ly + 92, 110, 24, "Install") && g_file[0]) { char a[160]; snprintf(a, sizeof(a), "install %s", g_file); run_trpm(a); }
        ui_label(x, ly + 134, "Downloads are verified with the repo's HMAC and each package's sha256 checksum.", COL_DIM);
    }
    int ly2 = g_win_h - 170;
    if (g_tab == TAB_AVAILABLE) {
        ui_label(16, ly2 - 52, "Install from file", COL_DIM);
        ui_field(3, 16, ly2 - 32, lw - 120, g_file, sizeof(g_file));
        if (ui_button_primary(16 + lw - 110, ly2 - 32, 110, 24, "Install") && g_file[0]) { char a[160]; snprintf(a, sizeof(a), "install %s", g_file); run_trpm(a); }
    }
    if (ui_button(g_win_w - 128, ly2 - 32, 112, 24, "Refresh")) { refresh(); strlcpy(g_status, "refreshed", sizeof(g_status)); }
    draw_log(16, ly2, g_win_w - 32, 124);
    if (g_status[0]) ui_label(16, g_win_h - 28, g_status, COL_DIM);
}

int main(void) {
    app_sysinfo_t si;
    int w = 800, h = 580;
    if (sysinfo(&si) == 0) {
        if ((int)si.fb_width - 40 < w) w = (int)si.fb_width - 40;
        if ((int)si.fb_height - 90 < h) h = (int)si.fb_height - 90;
    }
    if (gui_open(70, 40, w, h, "Packages") != 0) return 1;
    ui_splash("Loading packages...");
    refresh();
    load_repo();
    ui.dirty = 1;
    while (!ui_closed()) {
        ui_pump();
        if (ui.dirty) {
            draw();
            if (ui.dirty) draw();
        }
        app_yield();
    }
    return 0;
}
