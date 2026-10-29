/* teroobe.trp - terminal out-of-box setup (second boot).
 *
 * Collects: account (username, password, computer name, timezone),
 * terminal look (text / background / status-bar colour), screen
 * resolution, network, boot options and memory mode. Nothing is applied
 * here: everything is sent to the kernel in one SYS_OOBE_SUBMIT call and
 * the OS applies + saves it, then asks for a restart.
 */
#include "trp_rt.h"

static const char *color_names[16] = {
    "black", "blue", "green", "cyan", "red", "magenta", "brown", "light grey",
    "dark grey", "light blue", "light green", "light cyan", "light red",
    "light magenta", "yellow", "white"
};

static char user[32], pass[32], host[32] = "toriginal", tz[16] = "UTC";
static int  c_text = 7, c_bg = 0, c_bar = 1;
static int  font_mono;
static char res[16] = "1024x768";
static int  net_mode = 1;                    /* 0 off, 1 dhcp, 2 static */
static char ip[20], mask[20], gw[20], dns[20];
static int  boot_verbose, boot_safe;
static int  mem_mode;                        /* 0 max, 1 custom, 2 least */
static u64  ram_kb;
static sysinfo_t sinfo;

static void banner(const char *step) {
    clear_();
    color_(15, 1);
    puts_("  TORIGINAL OS  -  SETUP                      \n");
    color_(c_text, c_bg);
    putc_('\n');
    color_(14, c_bg); puts_(step); putc_('\n'); color_(c_text, c_bg);
    putc_('\n');
}

static int name_ok(const char *s) {
    int n = t_strlen(s);
    if (n < 1 || n > 31) return 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static int ip_ok(const char *s) {
    for (int part = 0; part < 4; part++) {
        int digits = 0; int v = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; digits++; if (v > 255) return 0; }
        if (!digits) return 0;
        if (part < 3) { if (*s != '.') return 0; s++; }
    }
    return *s == 0;
}

static void err(const char *m) { color_(12, c_bg); puts_(m); putc_('\n'); color_(c_text, c_bg); }

static void ask_text(const char *prompt, char *out, int max, int (*valid)(const char *), const char *hint) {
    char b[64];
    for (;;) {
        puts_(prompt); readline_(b, sizeof(b), 0);
        if (!b[0] && out[0]) return;                 /* keep default */
        if (valid(b)) { t_strcpy(out, b, max); return; }
        err(hint);
    }
}

static int res_ok(const char *s) {
    u64 w = 0, h = 0; int i = 0;
    if (!(s[0] >= '0' && s[0] <= '9')) return 0;
    while (s[i] >= '0' && s[i] <= '9') { w = w * 10 + (u64)(s[i] - '0'); i++; if (w > 99999) return 0; }
    if (s[i] != 'x') return 0;
    i++;
    if (!(s[i] >= '0' && s[i] <= '9')) return 0;
    while (s[i] >= '0' && s[i] <= '9') { h = h * 10 + (u64)(s[i] - '0'); i++; if (h > 99999) return 0; }
    if (s[i]) return 0;
    return w >= 640 && w <= 4096 && h >= 400 && h <= 2304;
}

static int tz_ok(const char *s) { int n = t_strlen(s); return n >= 1 && n <= 15; }

static void step_welcome(void) {
    banner("Welcome");
    puts_("Toriginal OS is installed. A few quick questions and you are done:\n\n");
    puts_("  1. your account\n  2. how the terminal looks\n  3. screen, network and boot options\n\n");
    puts_("Press any key to begin.\n");
    getch_();
}

static void step_account(void) {
    banner("1/3  Your account");
    ask_text("Username: ", user, sizeof(user), name_ok, "Use 1-31 letters, digits, '_' or '-'.");
    for (;;) {
        char a[32], b[32];
        puts_("Password: ");  readline_(a, sizeof(a), 1);
        puts_("Repeat:   ");  readline_(b, sizeof(b), 1);
        if (!t_streq(a, b)) { err("Passwords do not match."); continue; }
        t_strcpy(pass, a, sizeof(pass));
        break;
    }
    ask_text("Computer name [toriginal]: ", host, sizeof(host), name_ok, "Use 1-31 letters, digits, '_' or '-'.");
    ask_text("Timezone [UTC]: ", tz, sizeof(tz), tz_ok, "Up to 15 characters, e.g. UTC or EST.");
}

static int pick_color(const char *what, int current) {
    for (;;) {
        puts_("\n"); puts_(what); puts_(" colour:\n");
        for (int i = 0; i < 16; i++) {
            puts_("  "); put_upad((u64)i, 3); color_(i ? i : 8, 0); put_pad(color_names[i], 14); color_(c_text, c_bg);
            if (i % 2) putc_('\n');
        }
        puts_("Number 0-15 [");  putu((u64)current); puts_("]: ");
        char b[8]; u64 v; readline_(b, sizeof(b), 0);
        if (!b[0]) return current;
        if (parse_u64(b, &v) && v < 16) return (int)v;
        err("Pick a number from 0 to 15.");
    }
}

static void step_terminal(void) {
    for (;;) {
        banner("2/3  Terminal look");
        c_text = pick_color("Text", c_text);
        c_bg   = pick_color("Background", c_bg);
        if (c_text == c_bg) { err("Text and background are the same - pick different colours."); getch_(); continue; }
        c_bar  = pick_color("Status bar", c_bar);
        color_(c_text, c_bg);
        puts_("\nTerminal font:\n  1) Default\n  2) Monospace (DejaVu Sans Mono)\nChoice [");
        putc_(font_mono ? '2' : '1'); puts_("]: ");
        { char fb[8]; readline_(fb, sizeof(fb), 0); if (fb[0] == '1') font_mono = 0; else if (fb[0] == '2') font_mono = 1; }
        banner("2/3  Terminal look - preview");
        puts_("This is how your terminal will look.\n");
        color_(15, c_bar); puts_(" status bar sample "); color_(c_text, c_bg);
        puts_("\n\nKeep this look? [Y/n]: ");
        char b[8]; readline_(b, sizeof(b), 0);
        if (t_lower(b[0]) != 'n') break;
    }
}

static void step_system(void) {
    banner("3/3  Screen, network and boot");
    puts_("Screen resolution:\n  1) 800x600\n  2) 1024x768\n  3) 1280x720\n  4) 1280x1024\n  5) 1920x1080\n  6) Custom (any size, e.g. 1366x768)\n");
    puts_("Choice [2]: ");
    char b[16]; readline_(b, sizeof(b), 0);
    switch (b[0]) { case '1': t_strcpy(res, "800x600", 16); break; case '3': t_strcpy(res, "1280x720", 16); break;
                    case '4': t_strcpy(res, "1280x1024", 16); break; case '5': t_strcpy(res, "1920x1080", 16); break;
                    case '6': ask_text("  Width x height: ", res, sizeof(res), res_ok, "Use WIDTHxHEIGHT (lowercase x) between 640x400 and 4096x2304."); break;
                    default: t_strcpy(res, "1024x768", 16); break; }

    puts_("\nNetwork:\n  1) Automatic (DHCP)\n  2) Static address\n  3) Off\nChoice [1]: ");
    readline_(b, sizeof(b), 0);
    net_mode = (b[0] == '2') ? 2 : (b[0] == '3') ? 0 : 1;
    if (net_mode == 2) {
        ask_text("  IP address:  ", ip,   sizeof(ip),   ip_ok, "Enter a dotted address like 10.0.2.15");
        ask_text("  Netmask:     ", mask, sizeof(mask), ip_ok, "Enter a dotted mask like 255.255.255.0");
        ask_text("  Gateway:     ", gw,   sizeof(gw),   ip_ok, "Enter a dotted address like 10.0.2.2");
        ask_text("  DNS server:  ", dns,  sizeof(dns),  ip_ok, "Enter a dotted address like 10.0.2.3");
    }

    puts_("\nShow detailed boot messages? [y/N]: "); readline_(b, sizeof(b), 0);
    boot_verbose = (t_lower(b[0]) == 'y');
    puts_("Start in safe mode next boot? [y/N]: "); readline_(b, sizeof(b), 0);
    boot_safe = (t_lower(b[0]) == 'y');

    puts_("\nMemory use (this machine has "); putu(sinfo.ram_kb / 1024); puts_(" MiB):\n");
    puts_("  1) Use all RAM\n  2) Limit to a custom amount\n  3) Use as little as possible\nChoice [1]: ");
    readline_(b, sizeof(b), 0);
    mem_mode = (b[0] == '2') ? 1 : (b[0] == '3') ? 2 : 0;
    if (mem_mode == 1) {
        for (;;) {
            puts_("  Limit in MiB: "); readline_(b, sizeof(b), 0);
            u64 v;
            if (parse_u64(b, &v) && v >= 32 && v * 1024 <= sinfo.ram_kb) { ram_kb = v * 1024; break; }
            err("Enter a number between 32 and the amount of RAM installed.");
        }
    }
}

/* Append "key=value\n" to the submit buffer. */
static char out[1024]; static int outn;
static void kv(const char *k, const char *v) {
    int kl = t_strlen(k), vl = t_strlen(v);
    if (outn + kl + vl + 2 >= (int)sizeof(out)) return;
    t_memcpy(out + outn, k, (u64)kl); outn += kl; out[outn++] = '=';
    t_memcpy(out + outn, v, (u64)vl); outn += vl; out[outn++] = '\n';
}
static void kvn(const char *k, u64 v) {
    char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + v % 10); v /= 10; }
    kv(k, b + i);
}

static int step_confirm(void) {
    banner("Summary");
    puts_("Username:   "); puts_(user); puts_("\nComputer:   "); puts_(host); puts_("\nTimezone:   "); puts_(tz);
    puts_("\nColours:    text "); puts_(color_names[c_text]); puts_(" on "); puts_(color_names[c_bg]);
    puts_(", bar "); puts_(color_names[c_bar]);
    puts_("\nFont:       "); puts_(font_mono ? "monospace" : "default");
    puts_("\nResolution: "); puts_(res);
    puts_("\nNetwork:    "); puts_(net_mode == 0 ? "off" : net_mode == 1 ? "DHCP" : ip);
    puts_("\nBoot:       "); puts_(boot_verbose ? "verbose" : "quiet"); puts_(boot_safe ? ", safe mode" : "");
    puts_("\nMemory:     "); if (mem_mode == 0) puts_("all RAM"); else if (mem_mode == 2) puts_("minimum");
    else { putu(ram_kb / 1024); puts_(" MiB limit"); }
    puts_("\n\nApply these settings? [Y]es / [R]edo all: ");
    char b[8]; readline_(b, sizeof(b), 0);
    return t_lower(b[0]) != 'r';
}

int trp_main(void) {
    trp_sys(SYS_SYSINFO, (long)&sinfo, 0, 0);
    step_welcome();
    for (;;) {
        step_account();
        step_terminal();
        step_system();
        if (step_confirm()) break;
    }

    outn = 0;
    kv("username", user); kv("password", pass); kv("hostname", host); kv("timezone", tz);
    kvn("text_color", (u64)c_text); kvn("bg_color", (u64)c_bg); kvn("statusbar_color", (u64)c_bar);
    kv("font", font_mono ? "mono" : "default");
    kv("resolution", res);
    kv("net_mode", net_mode == 0 ? "off" : net_mode == 1 ? "dhcp" : "static");
    if (net_mode == 2) { kv("net_ip", ip); kv("net_mask", mask); kv("net_gw", gw); kv("net_dns", dns); }
    kvn("boot_verbose", (u64)boot_verbose); kvn("boot_safe", (u64)boot_safe);
    kv("mem_mode", mem_mode == 0 ? "max" : mem_mode == 1 ? "custom" : "least");
    if (mem_mode == 1) kvn("ram_kb", ram_kb);

    if (trp_sys(SYS_OOBE_SUBMIT, (long)out, outn, 0) != 0) {
        err("The system could not apply those settings. Starting setup again...");
        puts_("(press any key)\n"); getch_();
        return 1;
    }
    puts_("\nSaving your settings...\n");
    return 0;
}
