#include "trpgui.h"

static void row(int x, int y, const char *k, const char *v) {
    ui_label(x, y, k, COL_DIM);
    ui_label(x + 150, y, v, COL_TEXT);
}

static void draw(void) {
    ui.dirty = 0;
    gui_fill(0, 0, g_win_w, g_win_h, COL_BG);
    int lw = 250;
    gui_fill(0, 0, lw, g_win_h, COL_SIDEBAR);
    gui_fill(lw, 0, 1, g_win_h, COL_BORDER);

    app_sysinfo_t si;
    sysinfo(&si);
    char bld[16] = "?";
    long bn = file_read_all("/system/osbuild", bld, sizeof(bld) - 1);
    if (bn > 0) { bld[bn] = 0; for (long i = 0; i < bn; i++) if (bld[i] == '\n' || bld[i] == '\r') bld[i] = 0; }

    int ls = 168;
    int ly = (g_win_h - ls - 90) / 2;
    if (ly < 16) ly = 16;
    gui_image((lw - ls) / 2, ly, ls, ls, "/system/gui/startmenu/orb.png");
    int tw = gui_text_w(si.os_name, 2);
    ui_label_s((lw - tw * 1) / 2 - 0, ly + ls + 16, si.os_name, COL_TEXT, 2);
    char t[64];
    snprintf(t, sizeof(t), "Version %s  -  Build %s", si.os_version, bld);
    int vw = gui_text_w(t, 1);
    ui_label((lw - vw) / 2, ly + ls + 48, t, COL_DIM);

    int x = lw + 32, y = 28;
    ui_label_s(x, y, "Welcome", COL_TEXT, 2);
    y += 52;
    row(x, y, "Operating system", si.os_name);
    row(x, y + 26, "Version", si.os_version);
    snprintf(t, sizeof(t), "%s", bld);
    row(x, y + 52, "Build", t);
    row(x, y + 78, "Kernel", si.kernel_name);
    row(x, y + 104, "Architecture", si.arch);
    snprintf(t, sizeof(t), "%ux%u", si.fb_width, si.fb_height);
    row(x, y + 130, "Display", t);
    u64 s = si.uptime_ms / 1000;
    snprintf(t, sizeof(t), "%lu min %lu s", (unsigned long)(s / 60), (unsigned long)(s % 60));
    row(x, y + 156, "Uptime", t);
    if (si.has_nic) snprintf(t, sizeof(t), "%u.%u.%u.%u", (si.ip >> 24) & 255, (si.ip >> 16) & 255, (si.ip >> 8) & 255, si.ip & 255);
    else strlcpy(t, "no network adapter", sizeof(t));
    row(x, y + 182, "IP address", t);

    unsigned long total = (unsigned long)si.ram_total_kb / 1024, freem = (unsigned long)si.ram_free_kb / 1024;
    unsigned long used = total > freem ? total - freem : 0;
    ui_label(x, y + 222, "Memory", COL_DIM);
    snprintf(t, sizeof(t), "%lu MiB used of %lu MiB", used, total);
    ui_label(x + 150, y + 222, t, COL_TEXT);
    int pw = g_win_w - x - 32;
    if (pw > 420) pw = 420;
    ui_progress(x, y + 248, pw, 18, total ? (int)(used * 100 / total) : 0);

    int by = y + 292;
    if (ui_button_primary(x, by, 130, 34, "Settings")) app_spawn("/system/trp/settings.trp");
    if (ui_button(x + 142, by, 130, 34, "Terminal")) app_spawn("/system/trp/terminal.trp");
    if (ui_button(x + 284, by, 130, 34, "Computer")) app_spawn("/system/trp/computer.trp");
}

int main(void) {
    app_sysinfo_t si;
    int w = 760, h = 440;
    if (sysinfo(&si) == 0) {
        if ((int)si.fb_width - 40 < w) w = (int)si.fb_width - 40;
        if ((int)si.fb_height - 90 < h) h = (int)si.fb_height - 90;
    }
    if (gui_open(60, 50, w, h, "Welcome") != 0) return 1;
    ui_splash("Loading...");
    ui.dirty = 1;
    u64 last = 0;
    while (!ui_closed()) {
        ui_pump();
        u64 now = uptime_ms();
        if (now - last > 1000) { last = now; ui.dirty = 1; }
        if (ui.dirty) {
            draw();
            if (ui.dirty) draw();
        }
        app_yield();
    }
    return 0;
}
