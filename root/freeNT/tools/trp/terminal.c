#include "trpgui.h"

#define LW 200
#define MAXLINES 1500
#define HIST_MAX 200
#define OUTMAX 15000

typedef struct { char t[LW + 1]; u8 c[LW + 1]; } tline_t;

static const u32 g_vga[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static tline_t *g_lines;
static int g_first, g_count, g_col, g_cols, g_rows, g_view;
static int g_dirty = 1, g_closed_win, g_sb_drag;
static u64 g_last_draw;
static char *g_hist[HIST_MAX];
static int g_nhist;
static int g_keyq[128], g_kh, g_kt;
static char g_cwd[256] = "/";
static char g_out[OUTMAX + 1];
static char g_edit[512];
static int g_elen, g_ecur, g_editing;
static const char *g_prompt = "";
static int g_fg_idx = 7, g_bg_idx = 0;
static int g_cur_col = 0xFF;
static int g_last_tab;

static const char *g_cmds[] = {
    "advancedhelp", "cat", "cd", "clear", "cls", "copy", "date", "echo", "exit", "force", "free", "halt",
    "hardware", "help", "ifconfig", "ls", "mem", "mkdir", "osimage", "ping", "pwd", "reboot", "rm", "run",
    "settings", "status", "sysver", "touch", "trpbuild", "trpm", "uname", "wallpaper", "write", NULL
};

static tline_t *line_at(int i) { return &g_lines[(g_first + i) % MAXLINES]; }

static void new_line(void) {
    if (g_count < MAXLINES) g_count++;
    else g_first = (g_first + 1) % MAXLINES;
    memset(line_at(g_count - 1), 0, sizeof(tline_t));
    g_col = 0;
}

static void term_clear(void) {
    g_first = 0;
    g_count = 0;
    new_line();
    g_view = 0;
    g_dirty = 1;
}

static void put_cell(char ch, int color) {
    if (g_col >= g_cols) new_line();
    tline_t *l = line_at(g_count - 1);
    int len = (int)strlen(l->t);
    while (len < g_col) { l->t[len] = ' '; l->c[len] = 0xFF; len++; }
    l->t[g_col] = ch;
    l->c[g_col] = (u8)color;
    g_col++;
    if (g_col > len) l->t[g_col] = 0;
}

static void term_write(const char *s, int color) {
    if (!g_count) new_line();
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '$' && s[1] == '{') {
            const char *e = strchr(s, '}');
            if (e) { s = e; continue; }
        }
        if (c == '\n') { new_line(); continue; }
        if (c == '\r') { g_col = 0; continue; }
        if (c == '\f') { term_clear(); continue; }
        if (c == '\b') { if (g_col > 0) g_col--; continue; }
        if (c == 27) {
            if (s[1] == '[') { s += 2; while (*s && !isalpha((u8)*s)) s++; if (!*s) break; }
            continue;
        }
        if (c == '\t') {
            int target = (g_col / 8 + 1) * 8;
            while (g_col < target) put_cell(' ', color);
            continue;
        }
        if (c < 32 || c > 126) continue;
        put_cell((char)c, color);
    }
    g_dirty = 1;
}

static void load_theme(void) {
    char b[16];
    if (cfg_get("text_color", b, sizeof(b)) == 0) { int v = atoi(b); if (v >= 0 && v < 16) g_fg_idx = v; }
    if (cfg_get("bg_color", b, sizeof(b)) == 0) { int v = atoi(b); if (v >= 0 && v < 16) g_bg_idx = v; }
}

static u32 cell_fg(int c) { return c == 0xFF ? g_vga[g_fg_idx] : g_vga[c & 15]; }

static int edit_rows(int *cur_row, int *cur_col) {
    int pl = (int)strlen(g_prompt);
    int total = pl + g_elen;
    int rows = (total > g_ecur + pl ? total : pl + g_ecur) / g_cols + 1;
    if (cur_row) *cur_row = (pl + g_ecur) / g_cols;
    if (cur_col) *cur_col = (pl + g_ecur) % g_cols;
    return rows;
}

static void draw_line_runs(int y, const tline_t *l) {
    int n = (int)strlen(l->t);
    int i = 0;
    while (i < n) {
        int j = i;
        char buf[LW + 2];
        while (j < n && l->c[j] == l->c[i]) { buf[j - i] = l->t[j]; j++; }
        buf[j - i] = 0;
        gui_text(6 + i * FONT_W, y, buf, cell_fg(l->c[i]), 0, GUI_F_TRANSPARENT);
        i = j;
    }
}

static void fit(void) {
    g_cols = (g_win_w - 24) / FONT_W;
    if (g_cols > LW - 2) g_cols = LW - 2;
    if (g_cols < 10) g_cols = 10;
    g_rows = (g_win_h - 8) / FONT_H;
    if (g_rows < 2) g_rows = 2;
}

static void draw(void) {
    fit();
    int er = 0, ec = 0;
    int erows = g_editing ? edit_rows(&er, &ec) : 0;
    int total = g_count + erows;
    int top = total - g_rows - g_view;
    if (top < 0) top = 0;
    u32 bg = g_vga[g_bg_idx], fg = g_vga[g_fg_idx];
    u32 pc = g_vga[10];

    gui_fill(0, 0, g_win_w, g_win_h, bg);
    int sbw = 10;
    for (int r = 0; r < g_rows; r++) {
        int idx = top + r;
        if (idx >= total) break;
        int y = 4 + r * FONT_H;
        if (idx < g_count) {
            draw_line_runs(y, line_at(idx));
        } else {
            int er_i = idx - g_count;
            int pl = (int)strlen(g_prompt);
            int start = er_i * g_cols;
            for (int p = start; p < start + g_cols && p < pl + g_elen; p++) {
                char ch[2] = { p < pl ? g_prompt[p] : g_edit[p - pl], 0 };
                gui_text(6 + (p - start) * FONT_W, y, ch, p < pl ? pc : fg, 0, GUI_F_TRANSPARENT);
            }
            if (er_i == er) {
                int cx = 6 + ec * FONT_W;
                gui_fill(cx, y, FONT_W, FONT_H, fg);
                int pos = g_ecur;
                if (pos < g_elen) { char ch[2] = { g_edit[pos], 0 }; gui_text(cx, y, ch, bg, 0, GUI_F_TRANSPARENT); }
            }
        }
    }
    if (total > g_rows) {
        int th = g_win_h - 8;
        gui_fill(g_win_w - sbw - 2, 4, sbw, th, 0x2A313B);
        int bar = th * g_rows / total;
        if (bar < 16) bar = 16;
        int maxoff = total - g_rows;
        int pos = (th - bar) * (maxoff - g_view) / (maxoff ? maxoff : 1);
        gui_fill(g_win_w - sbw - 2, 4 + pos, sbw, bar, 0x566070);
    }
    g_dirty = 0;
    g_last_draw = uptime_ms();
}

static void set_view_from_y(int y) {
    int er, ec;
    int total = g_count + (g_editing ? edit_rows(&er, &ec) : 0);
    int maxoff = total - g_rows;
    if (maxoff <= 0) { g_view = 0; return; }
    int th = g_win_h - 8;
    int v = maxoff - (y - 4) * maxoff / (th ? th : 1);
    if (v < 0) v = 0;
    if (v > maxoff) v = maxoff;
    g_view = v;
    g_dirty = 1;
}

static void poll_events(void) {
    gui_event_t e;
    while (gui_poll(&e)) {
        if (e.type == EV_CLOSE) g_closed_win = 1;
        else if (e.type == EV_RESIZE) g_dirty = 1;
        else if (e.type == EV_KEY_DOWN) {
            int nt = (g_kt + 1) % 128;
            if (nt != g_kh) { g_keyq[g_kt] = e.keycode; g_kt = nt; }
        } else if (e.type == EV_DOWN && e.button == 0) {
            if (e.x >= g_win_w - 16) { g_sb_drag = 1; set_view_from_y(e.y); }
        } else if (e.type == EV_MOVE) {
            if (g_sb_drag) set_view_from_y(e.y);
        } else if (e.type == EV_UP) g_sb_drag = 0;
    }
}

static int next_key(void) {
    if (g_kh == g_kt) return 0;
    int k = g_keyq[g_kh];
    g_kh = (g_kh + 1) % 128;
    return k;
}

static void add_hist(const char *l) {
    if (!l[0]) return;
    if (g_nhist && !strcmp(g_hist[g_nhist - 1], l)) return;
    if (g_nhist == HIST_MAX) { free(g_hist[0]); memmove(g_hist, g_hist + 1, sizeof(char *) * (HIST_MAX - 1)); g_nhist--; }
    g_hist[g_nhist++] = strdup(l);
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void complete(void) {
    int ws = g_ecur;
    while (ws > 0 && g_edit[ws - 1] != ' ') ws--;
    char word[200];
    int wl = g_ecur - ws;
    if (wl >= (int)sizeof(word)) return;
    memcpy(word, g_edit + ws, (size_t)wl);
    word[wl] = 0;
    int first = 1;
    for (int i = 0; i < ws; i++) if (g_edit[i] != ' ') first = 0;

    char *cand[256];
    int nc = 0;
    char *dirbuf = NULL;
    if (first && !strchr(word, '/')) {
        for (int i = 0; g_cmds[i] && nc < 256; i++)
            if (!strncmp(g_cmds[i], word, (size_t)wl)) cand[nc++] = strdup(g_cmds[i]);
    } else {
        char dirpart[200] = "", base[200];
        const char *slash = strrchr(word, '/');
        if (slash) {
            size_t n = (size_t)(slash - word) + 1;
            memcpy(dirpart, word, n); dirpart[n] = 0;
            strlcpy(base, slash + 1, sizeof(base));
        } else strlcpy(base, word, sizeof(base));
        char full[300];
        path_resolve(g_cwd, dirpart[0] ? dirpart : ".", full, sizeof(full));
        dirbuf = malloc(8192);
        if (dirbuf) {
            int cnt = listdir(full, dirbuf, 8192);
            char *p = dirbuf;
            size_t bl = strlen(base);
            for (int i = 0; i < cnt && nc < 256; i++) {
                int type = (u8)p[0];
                char *name = p + 1;
                p += 1 + strlen(name) + 1;
                if (name[0] == '.' && base[0] != '.') continue;
                if (strncmp(name, base, bl)) continue;
                char *m = malloc(strlen(dirpart) + strlen(name) + 2);
                strcpy(m, dirpart); strcat(m, name);
                if (type == DIRENT_DIR) strcat(m, "/");
                cand[nc++] = m;
            }
        }
    }
    if (nc) {
        qsort(cand, (size_t)nc, sizeof(char *), cmp_str);
        size_t common = strlen(cand[0]);
        for (int i = 1; i < nc; i++) { size_t k = 0; while (k < common && cand[i][k] == cand[0][k]) k++; common = k; }
        if ((int)common > wl) {
            int add = (int)common - wl;
            if (g_elen + add < (int)sizeof(g_edit) - 1) {
                memmove(g_edit + g_ecur + add, g_edit + g_ecur, (size_t)(g_elen - g_ecur + 1));
                memcpy(g_edit + g_ecur, cand[0] + wl, (size_t)add);
                g_elen += add; g_ecur += add;
            }
        }
        if (nc == 1) {
            size_t l = strlen(cand[0]);
            if (cand[0][l - 1] != '/' && g_elen + 1 < (int)sizeof(g_edit) - 1) {
                memmove(g_edit + g_ecur + 1, g_edit + g_ecur, (size_t)(g_elen - g_ecur + 1));
                g_edit[g_ecur++] = ' '; g_elen++;
            }
        } else if (g_last_tab) {
            term_write(g_prompt, 10);
            term_write(g_edit, 0xFF);
            term_write("\n", 0xFF);
            for (int i = 0; i < nc; i++) { term_write(cand[i], 0xFF); term_write("  ", 0xFF); }
            term_write("\n", 0xFF);
        }
    }
    for (int i = 0; i < nc; i++) free(cand[i]);
    free(dirbuf);
}

static int edit_line(const char *prompt) {
    g_prompt = prompt;
    g_elen = g_ecur = 0;
    g_edit[0] = 0;
    g_editing = 1;
    g_view = 0;
    int hpos = g_nhist;
    char saved[512] = "";
    g_dirty = 1;
    for (;;) {
        poll_events();
        if (g_closed_win) { g_editing = 0; return -1; }
        int k = next_key();
        if (!k) {
            if (g_dirty && uptime_ms() - g_last_draw > 30) draw();
            app_yield();
            continue;
        }
        if (k != 9) g_last_tab = 0;
        g_view = 0;
        g_dirty = 1;
        if (k == '\n' || k == '\r') break;
        else if (k == 3) {
            g_editing = 0;
            term_write(prompt, 10); term_write(g_edit, 0xFF); term_write("^C\n", 0xFF);
            return -2;
        }
        else if (k == 4 && g_elen == 0) { g_editing = 0; return -1; }
        else if (k == 12) { term_clear(); }
        else if (k == 1 || k == KEY_HOME) g_ecur = 0;
        else if (k == 5 || k == KEY_END) g_ecur = g_elen;
        else if (k == 21) { memmove(g_edit, g_edit + g_ecur, (size_t)(g_elen - g_ecur + 1)); g_elen -= g_ecur; g_ecur = 0; }
        else if (k == 11) { g_edit[g_ecur] = 0; g_elen = g_ecur; }
        else if (k == 23) {
            int s = g_ecur;
            while (s > 0 && g_edit[s - 1] == ' ') s--;
            while (s > 0 && g_edit[s - 1] != ' ') s--;
            memmove(g_edit + s, g_edit + g_ecur, (size_t)(g_elen - g_ecur + 1));
            g_elen -= g_ecur - s;
            g_ecur = s;
        }
        else if (k == KEY_LEFT) { if (g_ecur > 0) g_ecur--; }
        else if (k == KEY_RIGHT) { if (g_ecur < g_elen) g_ecur++; }
        else if (k == 8 || k == 127) {
            if (g_ecur > 0) { memmove(g_edit + g_ecur - 1, g_edit + g_ecur, (size_t)(g_elen - g_ecur + 1)); g_ecur--; g_elen--; }
        } else if (k == KEY_DEL) {
            if (g_ecur < g_elen) { memmove(g_edit + g_ecur, g_edit + g_ecur + 1, (size_t)(g_elen - g_ecur)); g_elen--; }
        } else if (k == KEY_UP) {
            if (hpos > 0) {
                if (hpos == g_nhist) strlcpy(saved, g_edit, sizeof(saved));
                hpos--;
                strlcpy(g_edit, g_hist[hpos], sizeof(g_edit));
                g_elen = g_ecur = (int)strlen(g_edit);
            }
        } else if (k == KEY_DOWN) {
            if (hpos < g_nhist) {
                hpos++;
                strlcpy(g_edit, hpos == g_nhist ? saved : g_hist[hpos], sizeof(g_edit));
                g_elen = g_ecur = (int)strlen(g_edit);
            }
        } else if (k == 9) {
            complete();
            g_last_tab = 1;
        } else if (k >= 32 && k < 127 && g_elen < (int)sizeof(g_edit) - 1) {
            memmove(g_edit + g_ecur + 1, g_edit + g_ecur, (size_t)(g_elen - g_ecur + 1));
            g_edit[g_ecur++] = (char)k;
            g_elen++;
        }
    }
    g_editing = 0;
    term_write(prompt, 10);
    term_write(g_edit, 0xFF);
    term_write("\n", 0xFF);
    return g_elen;
}

static void build_prompt(char *out, size_t max) {
    snprintf(out, max, "root%s~$ ", g_cwd[1] ? g_cwd : "");
}

static int is_word(const char *line, const char *w) {
    size_t n = strlen(w);
    return !strncmp(line, w, n) && (line[n] == 0 || line[n] == ' ');
}

static int builtin(const char *line) {
    while (*line == ' ') line++;
    if (is_word(line, "exit")) { g_closed_win = 1; return 1; }
    if (is_word(line, "clear") || is_word(line, "cls")) { term_clear(); return 1; }
    if (is_word(line, "desktop")) { term_write("already in the desktop\n", 0xFF); return 1; }
    if (is_word(line, "setup") || is_word(line, "oobe") || is_word(line, "install")) {
        term_write("setup needs the full-screen console, run it from the boot console\n", 12);
        return 1;
    }
    if (is_word(line, "run")) {
        const char *p = line + 3;
        while (*p == ' ') p++;
        if (!*p) { term_write("usage: run <path.trp>\n", 0xFF); return 1; }
        char path[300];
        path_resolve(g_cwd, p, path, sizeof(path));
        if (app_spawn(path) < 0) { term_write("run: could not start ", 12); term_write(path, 12); term_write("\n", 12); }
        return 1;
    }
    return 0;
}

static void run_line(const char *line) {
    if (builtin(line)) return;
    long n = shell_run(g_cwd, line, g_out, OUTMAX);
    if (n < 0) { term_write("command failed\n", 12); return; }
    term_write(g_out, 0xFF);
    load_theme();
}

int main(void) {
    app_sysinfo_t si;
    int w = 720, h = 500;
    if (sysinfo(&si) == 0) {
        if ((int)si.fb_width - 60 < w) w = (int)si.fb_width - 60;
        if ((int)si.fb_height - 110 < h) h = (int)si.fb_height - 110;
    }
    if (gui_open(30, 30, w, h, "Terminal") != 0) return 1;
    ui_splash("Starting terminal...");
    g_cols = (g_win_w - 12 - 12) / FONT_W;
    if (g_cols > LW - 2) g_cols = LW - 2;
    g_rows = (g_win_h - 8) / FONT_H;
    g_lines = calloc(MAXLINES, sizeof(tline_t));
    if (!g_lines) return 1;
    new_line();
    load_theme();

    char ver[96];
    snprintf(ver, sizeof(ver), "%s %s (%s)\n", si.os_name, si.os_version, si.kernel_name);
    term_write(ver, 0xFF);
    term_write("Type 'help' for the command list.\n\n", 0xFF);

    while (!g_closed_win) {
        char prompt[300];
        load_theme();
        build_prompt(prompt, sizeof(prompt));
        char title[300];
        snprintf(title, sizeof(title), "Terminal - root%s", g_cwd[1] ? g_cwd : "");
        gui_title(title);
        int r = edit_line(prompt);
        if (r == -1) break;
        if (r == -2) continue;
        add_hist(g_edit);
        char line[512];
        strlcpy(line, g_edit, sizeof(line));
        run_line(line);
        g_dirty = 1;
    }
    return 0;
}
