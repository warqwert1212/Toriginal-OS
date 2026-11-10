#include "trpgui.h"

#define M_EDIT 0
#define M_OPEN 1
#define M_SAVE 2
#define M_FIND 3
#define M_ASK  4

#define A_NONE 0
#define A_NEW  1
#define A_OPEN 2

#define TB_H 34
#define SB_H 22
#define GW   52
#define MAXFILE (16 * 1024 * 1024)
#define MAXUN 16
#define MAXDE 256

typedef struct { char *d; int n, cur; } snap_t;
typedef struct { char name[64]; int dir; } de_t;

static char *T;
static int len, cap;
static int cur, anc = -1;
static int top, lx, want = -1;
static int *vs, *vln;
static unsigned char *vc;
static int nv, vcap, vdirty = 1, lay_cols = -1;
static int wrap = 1;
static char path[256];
static int dirty, crlf;
static char *clip;
static int clen;
static snap_t un[MAXUN];
static int nun, lastop = -1;
static int mode, after;
static char fb[256];
static char dd[256] = "/";
static de_t de[MAXDE];
static int nde, hi, lscroll;
static char msg[96];
static int dragging, sbdrag, follow;
static char last_title[96];
static char last_find[128];
static u64 last_click_ms;
static int last_click_idx = -1;

static int ensure(int need) {
    if (need + 1 <= cap) return 1;
    int nc = cap ? cap : 4096;
    while (nc < need + 1) nc *= 2;
    char *n = realloc(T, (size_t)nc);
    if (!n) return 0;
    T = n;
    cap = nc;
    return 1;
}

static void setmsg(const char *s) { strlcpy(msg, s, sizeof(msg)); ui.dirty = 1; }

static void push_undo(int op) {
    if (op != 0 && op == lastop) return;
    lastop = op;
    if (len > 2 * 1024 * 1024) return;
    if (nun == MAXUN) {
        free(un[0].d);
        memmove(&un[0], &un[1], sizeof(snap_t) * (MAXUN - 1));
        nun--;
    }
    char *d = malloc((size_t)len + 1);
    if (!d) return;
    memcpy(d, T, (size_t)len);
    un[nun].d = d;
    un[nun].n = len;
    un[nun].cur = cur;
    nun++;
}

static void clear_undo(void) {
    for (int i = 0; i < nun; i++) free(un[i].d);
    nun = 0;
    lastop = -1;
}

static void do_undo(void) {
    if (nun == 0) { setmsg("Nothing to undo"); return; }
    snap_t s = un[--nun];
    if (ensure(s.n)) {
        memcpy(T, s.d, (size_t)s.n);
        len = s.n;
        cur = s.cur > len ? len : s.cur;
        anc = -1;
        dirty = 1;
        vdirty = 1;
    }
    free(s.d);
    lastop = -1;
    want = -1;
}

static int sel_lo(void) { return anc < 0 ? cur : (anc < cur ? anc : cur); }
static int sel_hi(void) { return anc < 0 ? cur : (anc < cur ? cur : anc); }
static int has_sel(void) { return anc >= 0 && anc != cur; }

static void del_range(int a, int b) {
    if (b <= a) return;
    memmove(T + a, T + b, (size_t)(len - b));
    len -= b - a;
    cur = a;
    anc = -1;
    dirty = 1;
    vdirty = 1;
}

static void ins(const char *s, int n) {
    if (has_sel()) del_range(sel_lo(), sel_hi());
    anc = -1;
    if (n <= 0) return;
    if (len + n > MAXFILE || !ensure(len + n)) { setmsg("Out of memory"); return; }
    memmove(T + cur + n, T + cur, (size_t)(len - cur));
    memcpy(T + cur, s, (size_t)n);
    len += n;
    cur += n;
    dirty = 1;
    vdirty = 1;
}

static void do_copy(void) {
    if (!has_sel()) return;
    int a = sel_lo(), b = sel_hi();
    char *c = malloc((size_t)(b - a) + 1);
    if (!c) return;
    memcpy(c, T + a, (size_t)(b - a));
    free(clip);
    clip = c;
    clen = b - a;
}

static void layout(void) {
    int cols = (g_win_w - GW - 6 - 16 - 4) / FONT_W;
    if (cols < 1) cols = 1;
    if (!vdirty && cols == lay_cols) return;
    lay_cols = cols;
    vdirty = 0;
    if (!vs || vcap < 1024) {
        free(vs); free(vln); free(vc);
        vcap = 1024;
        vs = malloc(sizeof(int) * (size_t)vcap);
        vln = malloc(sizeof(int) * (size_t)vcap);
        vc = malloc((size_t)vcap);
        if (!vs || !vln || !vc) { nv = 0; return; }
    }
    nv = 0;
    int col = 0, ln = 1, cont = 0, start = 1;
    for (int i = 0;; i++) {
        if (start) {
            if (nv == vcap) {
                int nc = vcap * 2;
                int *x = realloc(vs, sizeof(int) * (size_t)nc);
                if (x) vs = x;
                int *y = realloc(vln, sizeof(int) * (size_t)nc);
                if (y) vln = y;
                unsigned char *z = realloc(vc, (size_t)nc);
                if (z) vc = z;
                if (!x || !y || !z) break;
                vcap = nc;
            }
            vs[nv] = i;
            vln[nv] = ln;
            vc[nv] = (unsigned char)cont;
            nv++;
            start = 0;
        }
        if (i == len) break;
        if (T[i] == '\n') { ln++; cont = 0; col = 0; start = 1; continue; }
        col++;
        if (wrap && col >= cols) { col = 0; cont = 1; start = 1; }
    }
    if (nv == 0) { vs[0] = 0; vln[0] = 1; vc[0] = 0; nv = 1; }
}

static int vline_of(int idx) {
    int lo = 0, hi2 = nv - 1;
    while (lo < hi2) {
        int m = (lo + hi2 + 1) / 2;
        if (vs[m] <= idx) lo = m; else hi2 = m - 1;
    }
    return lo;
}

static int vlen(int k) {
    int e = k + 1 < nv ? vs[k + 1] : len;
    if (e > vs[k] && T[e - 1] == '\n') e--;
    return e - vs[k];
}

static int rows_vis(void) {
    int r = (g_win_h - TB_H - SB_H) / FONT_H;
    return r < 1 ? 1 : r;
}

static void clamp_top(void) {
    int r = rows_vis();
    int mx = nv - r;
    if (mx < 0) mx = 0;
    if (top > mx) top = mx;
    if (top < 0) top = 0;
}

static void ensure_visible(void) {
    layout();
    int vl = vline_of(cur);
    int r = rows_vis();
    if (vl < top) top = vl;
    if (vl >= top + r) top = vl - r + 1;
    clamp_top();
    if (wrap) lx = 0;
    else {
        int col = cur - vs[vl];
        if (col < lx) lx = col;
        if (col >= lx + lay_cols) lx = col - lay_cols + 1;
        if (lx < 0) lx = 0;
    }
}

static int pos_at(int mx, int my) {
    layout();
    int row = (my - TB_H) / FONT_H;
    if (my < TB_H) row = -1;
    int r = rows_vis();
    if (row < 0) row = 0;
    if (row >= r) row = r - 1;
    int k = top + row;
    if (k >= nv) k = nv - 1;
    if (k < 0) k = 0;
    int col = (mx - GW - 6 + FONT_W / 2) / FONT_W;
    if (mx < GW + 6) col = 0;
    col += lx;
    int l = vlen(k);
    if (col > l) col = l;
    if (col < 0) col = 0;
    return vs[k] + col;
}

static const char *base_name(void) {
    const char *b = path;
    for (const char *p = path; *p; p++) if (*p == '/') b = p + 1;
    return path[0] ? b : "Untitled";
}

static void new_doc(void) {
    len = 0; cur = 0; anc = -1; top = 0; lx = 0; want = -1;
    path[0] = 0; dirty = 0; crlf = 0; vdirty = 1;
    clear_undo();
    last_title[0] = 0;
}

static int load_file(const char *p) {
    app_stat_t st;
    if (fstat_path(p, &st) != 0 || st.type == DIRENT_DIR) { setmsg("File not found"); return 0; }
    if (st.size > MAXFILE) { setmsg("File too large"); return 0; }
    int n = (int)st.size;
    char *b = malloc((size_t)n + 1);
    if (!b) { setmsg("Out of memory"); return 0; }
    long got = n ? file_read_all(p, b, (size_t)n) : 0;
    if (got < 0) { free(b); setmsg("Cannot read file"); return 0; }
    n = (int)got;
    for (int i = 0; i < n; i++) if (b[i] == 0) { free(b); setmsg("Binary file - not opened"); return 0; }
    int cr = 0, w = 0;
    for (int i = 0; i < n; i++) {
        if (b[i] == '\r' && i + 1 < n && b[i + 1] == '\n') { cr = 1; continue; }
        b[w++] = b[i];
    }
    if (!ensure(w)) { free(b); setmsg("Out of memory"); return 0; }
    memcpy(T, b, (size_t)w);
    free(b);
    new_doc();
    len = w;
    crlf = cr;
    strlcpy(path, p, sizeof(path));
    vdirty = 1;
    setmsg("Opened");
    return 1;
}

static int save_file(const char *p) {
    int extra = 0;
    if (crlf) for (int i = 0; i < len; i++) if (T[i] == '\n') extra++;
    char *o = malloc((size_t)(len + extra) + 1);
    if (!o) { setmsg("Out of memory"); return 0; }
    int w = 0;
    for (int i = 0; i < len; i++) {
        if (crlf && T[i] == '\n') o[w++] = '\r';
        o[w++] = T[i];
    }
    int r = file_write_all(p, o, (size_t)w, 0);
    free(o);
    if (r != 0) { setmsg("Save failed"); return 0; }
    strlcpy(path, p, sizeof(path));
    dirty = 0;
    last_title[0] = 0;
    setmsg("Saved");
    return 1;
}

static int cmp_de(const void *a, const void *b) {
    const de_t *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcmp(x->name, y->name);
}

static void join(char *out, size_t max, const char *d, const char *n) {
    strlcpy(out, d, max);
    size_t l = strlen(out);
    if (l && out[l - 1] != '/') strlcat(out, "/", max);
    strlcat(out, n, max);
}

static void load_dir(void) {
    nde = 0; hi = -1; lscroll = 0;
    if (strcmp(dd, "/")) { strlcpy(de[0].name, "..", sizeof(de[0].name)); de[0].dir = 1; nde = 1; }
    char *buf = malloc(16384);
    if (!buf) return;
    int cnt = listdir(dd, buf, 16384);
    char *p = buf;
    int start = nde;
    for (int i = 0; i < cnt && nde < MAXDE; i++) {
        int type = (u8)p[0];
        char *name = p + 1;
        p += 1 + strlen(name) + 1;
        if (strlen(name) >= sizeof(de[0].name)) continue;
        strlcpy(de[nde].name, name, sizeof(de[nde].name));
        de[nde].dir = type == DIRENT_DIR;
        nde++;
    }
    free(buf);
    qsort(de + start, (size_t)(nde - start), sizeof(de_t), cmp_de);
}

static void dir_up(void) {
    char *s = strrchr(dd, '/');
    if (s && s != dd) *s = 0; else strlcpy(dd, "/", sizeof(dd));
}

static void open_dialog(int m) {
    mode = m;
    if (path[0]) {
        strlcpy(dd, path, sizeof(dd));
        dir_up();
        strlcpy(fb, path, sizeof(fb));
    } else {
        strlcpy(dd, "/", sizeof(dd));
        fb[0] = 0;
    }
    load_dir();
    ui.dirty = 1;
}

static void do_after(void) {
    int a = after;
    after = A_NONE;
    if (a == A_NEW) new_doc();
    else if (a == A_OPEN) open_dialog(M_OPEN);
}

static void req(int a) {
    if (dirty) { after = a; mode = M_ASK; ui.dirty = 1; }
    else { after = a; do_after(); }
}

static void do_save(void) {
    if (path[0]) { if (save_file(path)) do_after(); }
    else open_dialog(M_SAVE);
}

static void find_next(void) {
    int fl = (int)strlen(last_find);
    if (!fl) return;
    int s = has_sel() ? sel_hi() : cur;
    for (int pass = 0; pass < 2; pass++) {
        int from = pass == 0 ? s : 0;
        int to = pass == 0 ? len : (s + fl < len ? s + fl : len);
        for (int i = from; i + fl <= to; i++) {
            int ok = 1;
            for (int j = 0; j < fl; j++) {
                char a = T[i + j], b = last_find[j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { ok = 0; break; }
            }
            if (ok) {
                anc = i; cur = i + fl; want = -1; lastop = -1;
                setmsg(pass ? "Wrapped to start" : "Found");
                ensure_visible();
                return;
            }
        }
    }
    setmsg("Not found");
}

static void move_to(int idx) { cur = idx; anc = -1; lastop = -1; }

static void edit_key(int k) {
    layout();
    if (k == 1) { anc = 0; cur = len; want = -1; lastop = -1; return; }
    if (k == 3) { do_copy(); return; }
    if (k == 24) { if (has_sel()) { do_copy(); push_undo(0); del_range(sel_lo(), sel_hi()); } return; }
    if (k == 22) { if (clen) { push_undo(0); ins(clip, clen); } return; }
    if (k == 19) { do_save(); return; }
    if (k == 15) { req(A_OPEN); return; }
    if (k == 14) { req(A_NEW); return; }
    if (k == 26) { do_undo(); return; }
    if (k == 6) { mode = M_FIND; strlcpy(fb, last_find, sizeof(fb)); ui.dirty = 1; return; }
    if (k == 7) { find_next(); return; }
    int vl = vline_of(cur);
    if (k == KEY_LEFT) {
        if (has_sel()) move_to(sel_lo()); else if (cur > 0) move_to(cur - 1);
        want = -1; return;
    }
    if (k == KEY_RIGHT) {
        if (has_sel()) move_to(sel_hi()); else if (cur < len) move_to(cur + 1);
        want = -1; return;
    }
    if (k == KEY_UP || k == KEY_DOWN) {
        int col = cur - vs[vl];
        if (want < 0) want = col;
        int t = k == KEY_UP ? vl - 1 : vl + 1;
        if (t < 0) move_to(0);
        else if (t >= nv) move_to(len);
        else { int l = vlen(t); move_to(vs[t] + (want < l ? want : l)); }
        return;
    }
    if (k == KEY_HOME) { move_to(vs[vl]); want = -1; return; }
    if (k == KEY_END) {
        int e = vs[vl] + vlen(vl);
        move_to(e);
        want = -1; return;
    }
    if (k == KEY_DEL) {
        if (has_sel()) { push_undo(0); del_range(sel_lo(), sel_hi()); }
        else if (cur < len) { push_undo(3); del_range(cur, cur + 1); }
        want = -1; return;
    }
    if (k == 8 || k == 127) {
        if (has_sel()) { push_undo(0); del_range(sel_lo(), sel_hi()); }
        else if (cur > 0) { push_undo(2); del_range(cur - 1, cur); }
        want = -1; return;
    }
    if (k == '\n' || k == '\r') {
        push_undo(0);
        int ls = cur;
        while (ls > 0 && T[ls - 1] != '\n') ls--;
        int ind = 0;
        while (ls + ind < cur && (T[ls + ind] == ' ' || T[ls + ind] == '\t')) ind++;
        char b[130];
        int n = 0;
        b[n++] = '\n';
        for (int i = 0; i < ind && n < 128; i++) b[n++] = T[ls + i];
        ins(b, n);
        want = -1; lastop = -1; return;
    }
    if (k == 9) { push_undo(0); ins("    ", 4); want = -1; return; }
    if (k >= 32 && k < 127) {
        if (has_sel()) push_undo(0); else push_undo(1);
        char c = (char)k;
        ins(&c, 1);
        want = -1;
    }
}

static void draw_field(int x, int y, int w) {
    gui_fill(x, y, w, 24, COL_PANEL);
    gui_frame(x, y, w, 24, COL_ACCENT);
    int l = (int)strlen(fb);
    int cols = (w - 12) / FONT_W;
    const char *sh = l > cols ? fb + (l - cols) : fb;
    gui_text_s(x + 6, y + 4, sh, COL_TEXT, 1);
    gui_fill(x + 6 + (int)strlen(sh) * FONT_W, y + 4, 2, 16, COL_ACCENT);
}

static int field_key(int k) {
    int l = (int)strlen(fb);
    if (k == 8 || k == 127) { if (l) fb[l - 1] = 0; return 0; }
    if (k >= 32 && k < 127 && l < (int)sizeof(fb) - 1) { fb[l] = (char)k; fb[l + 1] = 0; }
    return 0;
}

static void dlg_enter(void) {
    if (mode == M_FIND) {
        strlcpy(last_find, fb, sizeof(last_find));
        mode = M_EDIT;
        find_next();
        return;
    }
    if (!fb[0]) return;
    char full[256];
    if (fb[0] == '/') strlcpy(full, fb, sizeof(full)); else join(full, sizeof(full), dd, fb);
    size_t fl = strlen(full);
    while (fl > 1 && full[fl - 1] == '/') full[--fl] = 0;
    if (path_is_dir(full)) {
        strlcpy(dd, full, sizeof(dd));
        load_dir();
        strlcpy(fb, dd, sizeof(fb));
        if (strcmp(dd, "/")) strlcat(fb, "/", sizeof(fb));
        return;
    }
    if (mode == M_OPEN) {
        if (load_file(full)) mode = M_EDIT;
    } else {
        if (save_file(full)) { mode = M_EDIT; do_after(); }
    }
}

static void dlg_pick(int i) {
    if (i < 0 || i >= nde) return;
    hi = i;
    if (!strcmp(de[i].name, "..")) { dir_up(); load_dir(); strlcpy(fb, dd, sizeof(fb)); if (strcmp(dd, "/")) strlcat(fb, "/", sizeof(fb)); return; }
    char full[256];
    join(full, sizeof(full), dd, de[i].name);
    if (de[i].dir) {
        strlcpy(dd, full, sizeof(dd));
        load_dir();
        strlcpy(fb, dd, sizeof(fb));
        strlcat(fb, "/", sizeof(fb));
    } else strlcpy(fb, full, sizeof(fb));
}

static void dlg_key(int k) {
    if (k == 27) { mode = M_EDIT; after = A_NONE; ui.dirty = 1; return; }
    if (mode == M_ASK) {
        if (k == '\n' || k == '\r') { mode = M_EDIT; do_save(); }
        return;
    }
    if (k == '\n' || k == '\r') { dlg_enter(); return; }
    if ((k == KEY_UP || k == KEY_DOWN) && mode != M_FIND) {
        if (nde == 0) return;
        int n = hi < 0 ? (k == KEY_DOWN ? 0 : nde - 1) : hi + (k == KEY_DOWN ? 1 : -1);
        if (n < 0 || n >= nde) return;
        int old = hi;
        dlg_pick(n);
        if (old >= 0 && de[n].dir) hi = -1;
        return;
    }
    field_key(k);
}

static void draw_dialog(int clk) {
    int W = g_win_w, H = g_win_h;
    int dw = W - 24 < 540 ? W - 24 : 540;
    int dh;
    if (mode == M_FIND) dh = 96;
    else if (mode == M_ASK) dh = 110;
    else dh = H - 24 < 380 ? H - 24 : 380;
    int dx = (W - dw) / 2, dy = (H - dh) / 2;
    gui_fill(dx - 1, dy - 1, dw + 2, dh + 2, COL_BORDER);
    gui_fill(dx, dy, dw, dh, COL_BG);
    ui.click = clk;
    if (mode == M_ASK) {
        char t[128];
        snprintf(t, sizeof(t), "Save changes to %s?", base_name());
        ui_label(dx + 16, dy + 18, t, COL_TEXT);
        int by = dy + dh - 44;
        if (ui_button_primary(dx + 16, by, 90, 30, "Save")) { mode = M_EDIT; do_save(); }
        else if (ui_button(dx + 116, by, 120, 30, "Don't save")) { mode = M_EDIT; do_after(); }
        else if (ui_button(dx + 246, by, 90, 30, "Cancel")) { mode = M_EDIT; after = A_NONE; }
        return;
    }
    if (mode == M_FIND) {
        ui_label(dx + 16, dy + 12, "Find", COL_TEXT);
        draw_field(dx + 16, dy + 34, dw - 32);
        if (ui_button_primary(dx + dw - 196, dy + 62, 90, 28, "Find")) dlg_enter();
        else if (ui_button(dx + dw - 100, dy + 62, 84, 28, "Cancel")) mode = M_EDIT;
        return;
    }
    ui_label(dx + 16, dy + 10, mode == M_OPEN ? "Open file" : "Save file as", COL_TEXT);
    ui_label(dx + 16, dy + 30, dd, COL_DIM);
    int ly = dy + 54, lh = dh - 54 - 74;
    gui_fill(dx + 16, ly, dw - 32, lh, COL_PANEL);
    gui_frame(dx + 16, ly, dw - 32, lh, COL_BORDER);
    int rows = (lh - 4) / 18;
    if (rows < 1) rows = 1;
    if (hi >= 0) { if (hi < lscroll) lscroll = hi; if (hi >= lscroll + rows) lscroll = hi - rows + 1; }
    for (int r = 0; r < rows; r++) {
        int i = lscroll + r;
        if (i >= nde) break;
        int ry = ly + 2 + r * 18;
        int hot = ui_hit(dx + 18, ry, dw - 36, 18);
        if (i == hi) gui_fill(dx + 18, ry, dw - 36, 18, COL_ACCENT_L);
        else if (hot) gui_fill(dx + 18, ry, dw - 36, 18, COL_BTN);
        char t[80];
        snprintf(t, sizeof(t), "%s%s", de[i].dir ? "[+] " : "    ", de[i].name);
        ui_label(dx + 24, ry + 1, t, COL_TEXT);
        if (hot && ui.click) { ui.click = 0; dlg_pick(i); ui.dirty = 1; }
    }
    ui_label(dx + 16, dy + dh - 62, "File name", COL_DIM);
    draw_field(dx + 16, dy + dh - 44, dw - 32 - 190);
    if (ui_button_primary(dx + dw - 186, dy + dh - 46, 84, 28, mode == M_OPEN ? "Open" : "Save")) dlg_enter();
    else if (ui_button(dx + dw - 94, dy + dh - 46, 78, 28, "Cancel")) { mode = M_EDIT; after = A_NONE; }
}

static void draw_text_area(void) {
    layout();
    if (follow) { follow = 0; ensure_visible(); }
    clamp_top();
    int r = rows_vis();
    int ex = GW + 6;
    int eh = r * FONT_H;
    gui_fill(0, TB_H, g_win_w, g_win_h - TB_H - SB_H, COL_PANEL);
    gui_fill(0, TB_H, GW, g_win_h - TB_H - SB_H, COL_SIDEBAR);
    gui_fill(GW, TB_H, 1, g_win_h - TB_H - SB_H, COL_BORDER);
    int s = sel_lo(), e = sel_hi();
    int sel = has_sel();
    char ln[1100];
    for (int row = 0; row < r; row++) {
        int k = top + row;
        if (k >= nv) break;
        int y = TB_H + row * FONT_H;
        int ls = vs[k], l = vlen(k);
        if (!vc[k]) {
            char nb[16];
            snprintf(nb, sizeof(nb), "%d", vln[k]);
            gui_text_s(GW - 8 - (int)strlen(nb) * FONT_W, y, nb, COL_DIM, 1);
        }
        int c0 = lx, c1 = lx + lay_cols;
        if (c1 > l) c1 = l;
        if (sel && e > ls && s <= ls + l) {
            int a = (s > ls ? s - ls : 0), b = (e < ls + l ? e - ls : l);
            if (e > ls + l) b = l + 1;
            if (a < c0) a = c0;
            if (b > lx + lay_cols) b = lx + lay_cols;
            if (b > a) gui_fill(ex + (a - lx) * FONT_W, y, (b - a) * FONT_W, FONT_H, COL_ACCENT_L);
        }
        int n = 0;
        for (int i = c0; i < c1 && n < 1090; i++) {
            char c = T[ls + i];
            ln[n++] = (c == '\t' || (u8)c < 32 || (u8)c > 126) ? ' ' : c;
        }
        ln[n] = 0;
        if (n) gui_text_s(ex, y, ln, COL_TEXT, 1);
    }
    if (mode == M_EDIT) {
        int vl = vline_of(cur);
        int col = cur - vs[vl] - lx;
        if (vl >= top && vl < top + r && col >= 0 && col <= lay_cols)
            gui_fill(ex + col * FONT_W, TB_H + (vl - top) * FONT_H, 2, FONT_H, COL_ACCENT_D);
    }
    int sx = g_win_w - 14;
    gui_fill(sx, TB_H, 14, eh, COL_SIDEBAR);
    if (nv > r) {
        int th = eh * r / nv;
        if (th < 24) th = 24;
        int mx = nv - r;
        int ty = TB_H + (eh - th) * top / mx;
        gui_fill(sx + 2, ty, 10, th, COL_BORDER);
    }
}

static void editor_mouse(int clk) {
    if (mode != M_EDIT) { dragging = 0; sbdrag = 0; return; }
    layout();
    int r = rows_vis();
    int eh = r * FONT_H;
    int sx = g_win_w - 14;
    if (!ui.down) { dragging = 0; sbdrag = 0; }
    if (clk && ui.my >= TB_H && ui.my < TB_H + eh) {
        if (ui.mx >= sx) sbdrag = 1;
        else if (ui.mx >= GW) {
            int idx = pos_at(ui.mx, ui.my);
            u64 now = uptime_ms();
            if (idx == last_click_idx && now - last_click_ms < 450 && idx <= len) {
                int a = idx, b = idx;
                while (a > 0 && T[a - 1] != ' ' && T[a - 1] != '\n') a--;
                while (b < len && T[b] != ' ' && T[b] != '\n') b++;
                anc = a; cur = b;
                last_click_idx = -1;
            } else {
                cur = idx; anc = idx; dragging = 1;
                last_click_idx = idx; last_click_ms = now;
            }
            want = -1; lastop = -1;
        }
        ui.click = 0;
    }
    if (sbdrag && ui.down && nv > r) {
        int th = eh * r / nv;
        if (th < 24) th = 24;
        int mx = nv - r;
        int p = ui.my - TB_H - th / 2;
        int rng = eh - th;
        top = rng > 0 ? p * mx / rng : 0;
        clamp_top();
    } else if (dragging && ui.down) {
        if (ui.my < TB_H && top > 0) top--;
        else if (ui.my > TB_H + eh && top < nv - r) top++;
        cur = pos_at(ui.mx, ui.my);
        if (anc == cur) { }
        want = -1;
    }
}

static void draw_toolbar(void) {
    gui_fill(0, 0, g_win_w, TB_H, COL_BG);
    gui_fill(0, TB_H - 1, g_win_w, 1, COL_BORDER);
    int x = 6, y = 4, h = 26;
    if (ui_button(x, y, 50, h, "New")) req(A_NEW);
    x += 54;
    if (ui_button(x, y, 56, h, "Open")) req(A_OPEN);
    x += 60;
    if (ui_button(x, y, 54, h, "Save")) do_save();
    x += 58;
    if (ui_button(x, y, 80, h, "Save As")) open_dialog(M_SAVE);
    x += 90;
    if (ui_button(x, y, 50, h, "Undo")) do_undo();
    x += 58;
    if (ui_button(x, y, 50, h, "Find")) { mode = M_FIND; strlcpy(fb, last_find, sizeof(fb)); ui.dirty = 1; }
    x += 58;
    if (ui_button(x, y, 90, h, wrap ? "Wrap: On" : "Wrap: Off")) { wrap = !wrap; vdirty = 1; lx = 0; }
}

static void draw_status(void) {
    int y = g_win_h - SB_H;
    gui_fill(0, y, g_win_w, SB_H, COL_BG);
    gui_fill(0, y, g_win_w, 1, COL_BORDER);
    layout();
    int vl = vline_of(cur);
    int ls = cur;
    while (ls > 0 && T[ls - 1] != '\n') ls--;
    char t[160];
    snprintf(t, sizeof(t), "Ln %d, Col %d    %d chars    %s", vln[vl], cur - ls + 1, len, crlf ? "CRLF" : "LF");
    ui_label(8, y + 3, t, COL_DIM);
    if (msg[0]) {
        int w = gui_text_w(msg, 1);
        ui_label(g_win_w - w - 16, y + 3, msg, COL_DIM);
    }
}

static void draw(int clk) {
    ui.dirty = 0;
    gui_fill(0, 0, g_win_w, g_win_h, COL_PANEL);
    ui.click = mode == M_EDIT ? clk : 0;
    draw_toolbar();
    if (clk && ui.my < TB_H) follow = 1;
    editor_mouse(ui.click);
    draw_text_area();
    draw_status();
    if (mode != M_EDIT) draw_dialog(clk);
    char t[96];
    snprintf(t, sizeof(t), "%s%s - Notepad", dirty ? "*" : "", base_name());
    if (strcmp(t, last_title)) { strlcpy(last_title, t, sizeof(last_title)); gui_title(t); }
}

int main(void) {
    app_sysinfo_t si;
    int w = 760, h = 520;
    if (sysinfo(&si) == 0) {
        if ((int)si.fb_width - 40 < w) w = (int)si.fb_width - 40;
        if ((int)si.fb_height - 90 < h) h = (int)si.fb_height - 90;
    }
    if (gui_open(80, 40, w, h, "Untitled - Notepad") != 0) return 1;
    ui_splash("Loading...");
    if (!ensure(0)) return 1;
    {
        char ob[260];
        long on = file_read_all("/toriginal_os/notepad_open", ob, sizeof(ob) - 1);
        if (on > 0) {
            ob[on] = 0;
            unlink("/toriginal_os/notepad_open");
            if (load_file(ob)) { dirty = 0; last_title[0] = 0; }
        }
    }
    ui.dirty = 1;
    int keys[64];
    while (!ui_closed()) {
        gui_event_t e;
        int nk = 0, clk = 0, ev = 0;
        while (gui_poll(&e)) {
            ev++;
            if (e.type == EV_MOVE) { ui.mx = e.x; ui.my = e.y; }
            else if (e.type == EV_DOWN && e.button == 1) { ui.mx = e.x; ui.my = e.y; }
            else if (e.type == EV_DOWN) { ui.mx = e.x; ui.my = e.y; ui.down = 1; clk = 1; }
            else if (e.type == EV_UP) { ui.mx = e.x; ui.my = e.y; ui.down = 0; }
            else if (e.type == EV_KEY_DOWN && nk < 64) keys[nk++] = e.keycode;
        }
        if (ev) ui.dirty = 1;
        for (int i = 0; i < nk; i++) {
            if (mode == M_EDIT) { edit_key(keys[i]); msg[0] = 0; }
            else dlg_key(keys[i]);
        }
        if (nk) follow = 1;
        if (ui.dirty || clk) {
            draw(clk);
            if (ui.dirty) draw(0);
        }
        app_yield();
    }
    return 0;
}
