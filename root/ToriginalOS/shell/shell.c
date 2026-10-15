/* sys/shell/shell.c - Toriginal OS CLI + package manager */

#include "io.h"
#include "string.h"
#include "fs.h"
#include "trpfs.h"
#include "trploader.h"
#include "process.h"
#include "loader.h"
#include "serial.h"
#include "installer.h"
#include "memory.h"
#include "vga.h"
#include "rtc.h"
#include "net.h"
#include "dns.h"
#include "http.h"
#include "icmp.h"
#include "pit.h"
#include "sha256.h"
#include "config.h"
#include "desktop.h"
#include "syscfg.h"
#include "graphics_core.h"
#include "vbe_dispi.h"
#include "trbl_settings.h"
#include "pmm.h"
#include "cpu.h"
#include "pci.h"
#include "ata.h"
#include "uhci.h"
#include "shell.h"

#define OS_NAME    "Toriginal OS"
#define OS_VERSION "1.2"
#define KERNEL_NAME "freeNT"
#define BUILD_ARCH  "x86-64"

/* TRP_MAGIC and the on-disk header struct (trp_file_header_t) now come
 * from trploader.h — this used to be a second, independent copy here,
 * which is exactly the kind of drift that breaks package loading. */

/* Package index lives at /pkgs/index.txt - one entry per line: name */
#define PKG_DIR   "/pkgs"
#define PKG_INDEX "/pkgs/index.txt"

static char g_cwd[256] = "/";
static char g_username[32] = "user";
static int  g_username_loaded = 0;

/* ── Real, saveable display settings ─────────────────────────────────────
 * Backed by /toriginal_os/config.ini via syscfg.h. g_resolution here is
 * the *label* for whatever's currently active (e.g. "1024x768"), not a
 * fake cosmetic string - it always matches g_framebuffer's real
 * width/height once graphics is up, because cmd_settings() only updates
 * it after graphics_set_mode() reports success. */
static char g_resolution[16] = "1024x768";

typedef struct { const char *name; vga_color_t value; } named_color_t;
static const named_color_t COLOR_NAMES[] = {
    { "black",       VGA_BLACK },
    { "blue",        VGA_BLUE },
    { "green",       VGA_GREEN },
    { "cyan",        VGA_CYAN },
    { "red",         VGA_RED },
    { "magenta",     VGA_MAGENTA },
    { "brown",       VGA_BROWN },
    { "lightgrey",   VGA_LIGHT_GREY },
    { "lightgray",   VGA_LIGHT_GREY },
    { "darkgrey",    VGA_DARK_GREY },
    { "darkgray",    VGA_DARK_GREY },
    { "lightblue",   VGA_LIGHT_BLUE },
    { "lightgreen",  VGA_LIGHT_GREEN },
    { "lightcyan",   VGA_LIGHT_CYAN },
    { "lightred",    VGA_LIGHT_RED },
    { "pink",        VGA_LIGHT_RED },
    { "lightmagenta",VGA_LIGHT_MAGENTA },
    { "yellow",      VGA_YELLOW },
    { "white",       VGA_WHITE },
};
#define COLOR_NAME_COUNT (sizeof(COLOR_NAMES) / sizeof(COLOR_NAMES[0]))

/* Parses either a color name ("cyan") or a raw 0-15 VGA index ("3") -
 * both are reasonable things to type, and this codebase's palette is
 * always exactly the 16-entry VGA set (see vga.h), so a plain numeric
 * index is unambiguous and worth supporting for anyone who already
 * knows the standard VGA color table. Returns 1 and fills *out on
 * success, 0 if arg matches neither form. */
static int parse_color(const char *arg, vga_color_t *out) {
    if (!arg || !arg[0]) return 0;

    /* Numeric form: exactly digits, 0-15. */
    int is_numeric = 1;
    for (const char *p = arg; *p; p++) {
        if (*p < '0' || *p > '9') { is_numeric = 0; break; }
    }
    if (is_numeric) {
        int v = 0;
        for (const char *p = arg; *p; p++) v = v * 10 + (*p - '0');
        if (v >= 0 && v <= 15) { *out = (vga_color_t)v; return 1; }
        return 0;
    }

    for (unsigned i = 0; i < COLOR_NAME_COUNT; i++) {
        if (strcmp(arg, COLOR_NAMES[i].name) == 0) {
            *out = COLOR_NAMES[i].value;
            return 1;
        }
    }
    return 0;
}

static const char *color_name(vga_color_t c) {
    for (unsigned i = 0; i < COLOR_NAME_COUNT; i++) {
        if (COLOR_NAMES[i].value == c) return COLOR_NAMES[i].name;
    }
    return "unknown";
}

/* Applies (from config.ini) the text/background/status-bar colors
 * saved by a previous 'settings' run. Safe to call every boot even on
 * a fresh install with no saved colors yet - syscfg_get_or() falls
 * back to this codebase's original hardcoded defaults (white text,
 * black background, white-on-blue status bar), so a never-configured
 * system looks exactly like it always did. Called once from
 * kernel_os_shell() at startup (kernel/shell.c) - color changes made
 * afterward via 'settings' apply live and re-save immediately, they
 * don't need a second call to this. */
void sys_shell_apply_saved_display_settings(void) {
    if (!trpfs_is_mounted()) return;

    char buf[8];
    vga_color_t fg, bg, sfg, sbg;

    syscfg_get_or("text_color", buf, sizeof(buf), "7"); /* VGA_LIGHT_GREY */
    if (!parse_color(buf, &fg)) fg = VGA_LIGHT_GREY;
    syscfg_get_or("bg_color", buf, sizeof(buf), "0"); /* VGA_BLACK */
    if (!parse_color(buf, &bg)) bg = VGA_BLACK;
    vga_set_color(fg, bg);

    syscfg_get_or("statusbar_color", buf, sizeof(buf), "15"); /* VGA_WHITE fg */
    if (!parse_color(buf, &sfg)) sfg = VGA_WHITE;
    sbg = VGA_BLUE; /* status bar background stays fixed; only its
                      * foreground/text color is user-configurable for
                      * now, matching what was actually asked for
                      * ("time bar colour") without also risking an
                      * unreadable bar from two independently-chosen
                      * colors landing on the same value. */
    vga_set_statusbar_color(sfg, sbg);

    syscfg_get_or("resolution", g_resolution, sizeof(g_resolution), "1024x768");
}

/* Exposed so kernel/shell.c can build the prompt (os~$ vs os/folder~$) */
const char *sys_shell_get_cwd(void) { return g_cwd; }

/* Read "username=..." out of /toriginal_os/config.ini, once, cached. */
static void load_username(void) {
    if (g_username_loaded) return;
    g_username_loaded = 1;
    if (!trpfs_is_mounted()) return;

    fd_t fd = fs_open("/toriginal_os/config.ini", O_RDONLY, 0);
    if (fd < 0) return;
    char buf[256];
    ssize_t n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    const char *key = "username=";
    size_t klen = strlen(key);
    for (char *p = buf; *p; p++) {
        if (strncmp(p, key, klen) == 0) {
            p += klen;
            int i = 0;
            while (*p && *p != '\n' && i < (int)sizeof(g_username) - 1) {
                g_username[i++] = *p++;
            }
            g_username[i] = '\0';
            return;
        }
    }
}

static const char *month_name(uint8_t m) {
    static const char *names[] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
    };
    if (m < 1 || m > 12) return "???";
    return names[m - 1];
}

static void u2s(uint32_t v, char *out) {
    out[0] = (char)('0' + (v / 10) % 10);
    out[1] = (char)('0' + v % 10);
    out[2] = '\0';
}

/* Writes v (0-99, which is all this file ever needs it for - VGA
 * color indices are 0-15) as decimal into out, no leading zero.
 * Unlike u2s() above this doesn't pad to 2 digits, since
 * syscfg-stored values should round-trip through syscfg_get() and a
 * leading zero would still parse fine there but reads oddly in
 * config.ini for a human editing it by hand. */
static void int_to_str(int v, char *out, size_t out_len) {
    if (v < 0) v = 0;
    if (out_len < 2) { if (out_len == 1) out[0] = '\0'; return; }
    if (v < 10) {
        out[0] = (char)('0' + v);
        out[1] = '\0';
    } else {
        out[0] = (char)('0' + (v / 10) % 10);
        out[1] = (char)('0' + v % 10);
        if (out_len >= 3) out[2] = '\0'; else out[1] = '\0';
    }
}

/* Rebuild and redraw the top status bar: "username | Mon DD YYYY  HH:MM:SS" */
void sys_shell_update_statusbar(void) {
    if (!trpfs_is_mounted()) return;
    load_username();

    rtc_time_t t;
    rtc_read(&t);

    char hh[3], mm[3], ss[3];
    u2s(t.hour, hh); u2s(t.minute, mm); u2s(t.second, ss);

    char bar[80];
    int n = 0;
    bar[n++] = ' ';
    for (const char *p = g_username; *p && n < 30; p++) bar[n++] = *p;
    bar[n++] = ' '; bar[n++] = '|'; bar[n++] = ' ';
    for (const char *p = month_name(t.month); *p; p++) bar[n++] = *p;
    bar[n++] = ' ';
    char dd[3]; u2s(t.day, dd);
    bar[n++] = dd[0]; bar[n++] = dd[1]; bar[n++] = ' ';
    /* year */
    uint16_t y = t.year;
    char ybuf[5]; ybuf[0]=(char)('0'+(y/1000)%10); ybuf[1]=(char)('0'+(y/100)%10);
    ybuf[2]=(char)('0'+(y/10)%10); ybuf[3]=(char)('0'+y%10); ybuf[4]='\0';
    for (int i=0;i<4;i++) bar[n++]=ybuf[i];
    bar[n++] = ' '; bar[n++] = ' ';
    bar[n++] = hh[0]; bar[n++] = hh[1]; bar[n++] = ':';
    bar[n++] = mm[0]; bar[n++] = mm[1]; bar[n++] = ':';
    bar[n++] = ss[0]; bar[n++] = ss[1];
    bar[n] = '\0';

    vga_draw_statusbar(bar);
}

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void print_u64(uint64_t v) {
    if (!v) { io_put_char('0'); return; }
    char t[21]; int i = 0;
    while (v) { t[i++] = '0' + (char)(v % 10); v /= 10; }
    while (i--) io_put_char(t[i]);
}

static void print_size(uint64_t b) {
    if      (b >= 1024*1024) { print_u64(b/(1024*1024)); io_put_string(" MB"); }
    else if (b >= 1024)      { print_u64(b/1024);        io_put_string(" KB"); }
    else                     { print_u64(b);             io_put_string(" B");  }
}

static void print_hex_u32(uint32_t v, int digits) {
    static const char hexd[] = "0123456789abcdef";
    char buf[9];
    for (int i = digits - 1; i >= 0; i--) { buf[i] = hexd[v & 0xF]; v >>= 4; }
    buf[digits] = '\0';
    io_put_string(buf);
}

static void resolve_path(const char *arg, char *dst, size_t n) {
    if (!arg || !arg[0]) { strncpy(dst, g_cwd, n-1); dst[n-1]='\0'; return; }
    if (arg[0] == '/') { strncpy(dst, arg, n-1); dst[n-1]='\0'; return; }
    size_t cl = strlen(g_cwd);
    strncpy(dst, g_cwd, n-1); dst[n-1]='\0';
    if (cl > 0 && dst[cl-1] != '/' && cl < n-1) dst[cl++]='/', dst[cl]='\0';
    size_t di = cl;
    for (size_t ai = 0; arg[ai] && di < n-1; ai++, di++) dst[di]=arg[ai];
    dst[di]='\0';
}

typedef struct {
    const char *needle;
    char *out;
    size_t outlen;
    int found;
    const char *dir_path;
} search_ctx_t;

static int search_dir_recursive(const char *dir_path, const char *needle, char *out, size_t outlen);

static int search_dir_cb(const char *name, uint8_t nlen, uint8_t type, void *ctx) {
    search_ctx_t *s = (search_ctx_t *)ctx;
    if (s->found) return 0;

    size_t needle_len = strlen(s->needle);
    char tmp[256];
    size_t len = strlen(s->dir_path);

    if (type == FILE_TYPE_DIR) {
        if (nlen == needle_len && strncmp(name, s->needle, needle_len) == 0) {
            if (len + 1 + needle_len >= sizeof(tmp)) return 0;
            memcpy(tmp, s->dir_path, len);
            if (len == 0 || tmp[len - 1] != '/') tmp[len++] = '/';
            memcpy(tmp + len, name, needle_len);
            tmp[len + needle_len] = '\0';

            strncpy(s->out, tmp, s->outlen - 1);
            s->out[s->outlen - 1] = '\0';
            s->found = 1;
            return 1;
        }

        if (len + 1 + nlen >= sizeof(tmp)) return 0;
        memcpy(tmp, s->dir_path, len);
        if (len == 0 || tmp[len - 1] != '/') tmp[len++] = '/';
        memcpy(tmp + len, name, nlen);
        tmp[len + nlen] = '\0';

        if (search_dir_recursive(tmp, s->needle, s->out, s->outlen)) {
            s->found = 1;
            return 1;
        }
    }

    return 0;
}

static int search_dir_recursive(const char *dir_path, const char *needle, char *out, size_t outlen) {
    inode_t st;
    if (fs_stat(dir_path, &st) != 0 || !FS_IS_DIR(st.mode)) return 0;

    search_ctx_t ctx = { needle, out, outlen, 0, dir_path };
    if (fs_readdir(dir_path, search_dir_cb, &ctx) != 0) return 0;
    return ctx.found;
}

static void normalize_path(char *path);

static void resolve_and_normalize(const char *arg, char *dst, size_t n) {
    resolve_path(arg, dst, n);
    normalize_path(dst);
}

static int resolve_cd_target(const char *arg, char *dst, size_t n) {
    char path[256];
    resolve_and_normalize(arg, path, sizeof(path));

    inode_t st;
    if (fs_stat(path, &st) == 0 && FS_IS_DIR(st.mode)) {
        strncpy(dst, path, n - 1);
        dst[n - 1] = '\0';
        return 1;
    }

    if (arg && arg[0] == '/') {
        const char *p = arg;
        while (*p == '/') p++;
        if (*p) {
            char name[64];
            size_t i = 0;
            while (p[i] && p[i] != '/' && i < sizeof(name) - 1) {
                name[i] = p[i];
                i++;
            }
            name[i] = '\0';
            if (search_dir_recursive("/", name, dst, n)) return 1;
        }
    }

    return 0;
}

/* Collapse "." and ".." segments in-place. "/a/b/../c" -> "/a/c".
 * Never escapes above root: "/.." stays "/". */
static void normalize_path(char *path) {
    char *segs[64];
    int nseg = 0;

    char tmp[256];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *tok = tmp;
    char *out = path;
    *out = '\0';

    /* Walk segments separated by '/' */
    char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *start = p;
        while (*p && *p != '/') p++;
        size_t seglen = (size_t)(p - start);
        if (*p) *p++ = '\0';

        if (seglen == 1 && start[0] == '.') {
            continue; /* "." - no-op */
        }
        if (seglen == 2 && start[0] == '.' && start[1] == '.') {
            if (nseg > 0) nseg--; /* pop last segment, can't go above root */
            continue;
        }
        if (nseg < 64) segs[nseg++] = start;
    }
    (void)tok;

    if (nseg == 0) { path[0] = '/'; path[1] = '\0'; return; }

    for (int i = 0; i < nseg; i++) {
        *out++ = '/';
        size_t l = strlen(segs[i]);
        memcpy(out, segs[i], l);
        out += l;
    }
    *out = '\0';
}

static char *split_first(char *buf) {
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == ' ') {
            buf[i] = '\0';
            char *r = buf + i + 1;
            while (*r == ' ') r++;
            return *r ? r : NULL;
        }
    }
    return NULL;
}

/* Set for the duration of a `force <command>` dispatch (see cmd_force
 * below) - checked by the few gates in this file that normally refuse
 * to run something (need_fs()'s "filesystem not mounted" check,
 * cmd_run()'s loader-format rejection) so `force` can push past them
 * on request. It does not and cannot make a genuinely broken
 * operation succeed - it just skips the OS's own gatekeeping, not the
 * underlying reason the gate existed. */
static int g_force_mode = 0;

static int need_fs(const char *cmd) {
    if (trpfs_is_mounted()) return 1;
    if (g_force_mode) {
        io_put_string(cmd);
        io_put_string(": forced ahead with no filesystem mounted - this will "
                       "likely fail or behave unpredictably\n");
        return 1;
    }
    io_put_string(cmd);
    io_put_string(": filesystem not mounted - run 'setup' first\n");
    return 0;
}

/* write a NUL-terminated string to an open fd */
static void fd_puts(fd_t fd, const char *s) {
    fs_write(fd, s, strlen(s));
}

static void cmd_help(void) {
    io_put_string("copy <src> <dst> - copy a regular file\n");
    io_put_string("write <path> <text> - write text into a file\n");
    io_put_string("cat <path> - print a file to screen\n");
    io_put_string("run <path.trp> - execute a TRP package\n");
    io_put_string("rm <path> - delete a file\n");
    io_put_string("desktop - launch the graphical desktop (Esc to return)\n");
    io_put_string("touch <path> - create an empty file\n");
    io_put_string("settings [480p|720p] - change the UI resolution\n");
    io_put_string("mkdir <path> - create a directory\n");
    io_put_string("ls [path] - list directory contents\n");
    io_put_string("cd <path> - change directory (use .. to go up)\n");
    io_put_string("pwd - print current directory\n");
    io_put_string("cls - clear the screen\n");
    io_put_string("uname - one-line system info\n");
    io_put_string("setup / oobe - run first-boot setup\n");
    io_put_string("sysver - show OS and kernel version\n");
    io_put_string("install - alias for setup\n");
    io_put_string("help - show this command list\n");
    io_put_string("advancedhelp - show package/network/build commands\n");
    io_put_string("status - show filesystem status\n");
    io_put_string("halt - halt the system\n");
    io_put_string("reboot - restart the system\n");
    io_put_string("\n");
}

static void cmd_advanced_help(void) {
    io_put_string("echo <text> - print text\n");
    io_put_string("trpbuild <folder> - build a .trp from a folder\n");
    io_put_string("trpm list - list installed packages\n");
    io_put_string("trpm install <pkg.trp> - install a TRP package\n");
    io_put_string("trpm remove <name> - remove an installed package\n");
    io_put_string("trpm repo <host[:port]> - set remote repo (HTTP)\n");
    io_put_string("ifconfig [ip nm gw [dns]] - show/set net config\n");
    io_put_string("ping <ip|hostname> - real ICMP echo request\n");
    io_put_string("free - show heap memory stats\n");
    io_put_string("hardware - list every kernel-recognized device (CPU, RAM, ports, drives, PCI)\n");
    io_put_string("mem <amount>[KB|MB|GB] | max | least - set the OS's RAM allocation\n");
    io_put_string("force <command> - run <command>, skipping its normal safety checks\n");
    io_put_string("\n");
}

static void cmd_sysver(void) {
    io_put_string(OS_NAME " v" OS_VERSION " (" KERNEL_NAME "/" BUILD_ARCH ")\n");
    io_put_string("heap . TRPFS . PS/2 kbd+mouse . PIT . TRP packages\n\n");

    io_put_string(
"             _______               \n"
"        ${c2}, ${c5}:8@@@@B@@@@@@B. ${c2},         \n"
"      _P${c4}_${c2}gggggggggggggggg;${c4}_${c2}@_       \n"
"    ,@@{${c3}g${c2}@@@@@@@@@@@@@@@@|${c3}g${c2}[@@,     \n"
"   _@@@@_${c3}@@@@@@${c2}@@@@]${c3}@@@@@W${c2}_@@@@,    \n"
"  ${c1}.${c2}9@@@W${c3}_g@P${c4}_g@${c2}@@@@]${c4}@g${c3}\"4@@${c2}\"%@@@C    \n"
"  .@@@\"${c3}@@P${c4}g@@@@${c2}@@@@]${c4}@@@@g${c3}t@[${c2}%@@@`   \n"
"  [@@${c3};@@/${c4}@@@@@@${c2}@@@@]${c4}@@@@@@${c3}\\\\@b${c2}4@@${c3}'   \n"
"  ${c2}'@@${c3}[@@${c4}@@@@@@@${c2}@@@@]${c4}@@@@@@]${c3}@@${c2}!@@'   \n"
"   @@${c3}[@]${c4}@@@@@@@${c2}@@@@]${c4}@@@@@@@${c3}@@'${c2}@,    \n"
"   '@${c3}.@@${c4}[@@@@@@${c2}@@@@]${c4}@@@@@@T${c3}@@${c2}|@     \n"
"     .${c3}4@p${c4}4@@@@@${c2}@@@@]${c4}@@@@@P${c3}g@/${c2}P      \n"
"       ${c3}^@@${c4}\"@@@@${c2}BBBB\"${c4}@@@@\"${c3}@W'        \n"
"         '@@~${c4}\"0B@@@BD\"${c3}o@D'          \n"
"             \"\"<===\"\"               \n"
    );
}

static void cmd_cls(void) { io_clear_screen(); }
static void cmd_pwd(void) { io_put_string(g_cwd); io_put_char('\n'); }

static void cmd_cd(const char *arg) {
    if (!arg || !arg[0]) { g_cwd[0]='/'; g_cwd[1]='\0'; return; }
    if (!need_fs("cd")) return;
    char path[256];
    if (!resolve_cd_target(arg, path, sizeof(path))) {
        io_put_string("cd: not found: "); io_put_string(arg); io_put_char('\n'); return;
    }
    inode_t st;
    if (fs_stat(path, &st) != 0) {
        io_put_string("cd: not found: "); io_put_string(arg); io_put_char('\n'); return;
    }
    if (!FS_IS_DIR(st.mode)) {
        io_put_string("cd: not a directory: "); io_put_string(path); io_put_char('\n'); return;
    }
    strncpy(g_cwd, path, sizeof(g_cwd)-1);
    size_t l = strlen(g_cwd);
    if (l > 1 && g_cwd[l-1] == '/') g_cwd[l-1] = '\0';
}

static int ls_cb(const char *name, uint8_t nlen, uint8_t type, void *ctx) {
    (void)ctx;
    io_put_string("  ");
    if (type == FILE_TYPE_DIR) io_put_char('[');
    for (uint8_t i = 0; i < nlen; i++) io_put_char(name[i]);
    if (type == FILE_TYPE_DIR) io_put_char(']');
    io_put_char('\n');
    return 0;
}

static void cmd_ls(const char *arg) {
    if (!need_fs("ls")) return;
    char path[256]; resolve_and_normalize(arg, path, sizeof(path));
    inode_t st;
    if (fs_stat(path, &st) != 0) {
        io_put_string("ls: not found: "); io_put_string(path); io_put_char('\n'); return;
    }
    if (!FS_IS_DIR(st.mode)) {
        io_put_string("  "); io_put_string(path);
        io_put_string("  ("); print_size(st.size); io_put_string(")\n"); return;
    }
    io_put_string(path); io_put_string(":\n");
    if (fs_readdir(path, ls_cb, NULL) != 0)
        io_put_string("ls: read error\n");
}

static void cmd_mkdir(const char *arg) {
    if (!arg||!arg[0]) { io_put_string("usage: mkdir <path>\n"); return; }
    if (!need_fs("mkdir")) return;
    char path[256]; resolve_and_normalize(arg, path, sizeof(path));
    if (fs_mkdir(path, 0755) == 0) {
        io_put_string("mkdir: created "); io_put_string(path); io_put_char('\n');
    } else {
        io_put_string("mkdir: failed (exists or bad path)\n");
    }
}

static void cmd_touch(const char *arg) {
    if (!arg||!arg[0]) { io_put_string("usage: touch <path>\n"); return; }
    if (!need_fs("touch")) return;
    char path[256]; resolve_and_normalize(arg, path, sizeof(path));
    fd_t fd = fs_open(path, O_WRONLY|O_CREAT, 0644);
    if (fd < 0) { io_put_string("touch: failed\n"); return; }
    fs_close(fd);
    io_put_string("touch: created "); io_put_string(path); io_put_char('\n');
}

static void cmd_rm(const char *arg) {
    if (!arg||!arg[0]) { io_put_string("usage: rm <path>\n"); return; }
    if (!need_fs("rm")) return;
    char path[256]; resolve_and_normalize(arg, path, sizeof(path));
    if (fs_unlink(path) == 0) {
        io_put_string("rm: removed "); io_put_string(path); io_put_char('\n');
    } else {
        io_put_string("rm: failed (not found or is a directory)\n");
    }
}

static void cmd_cat(const char *arg) {
    if (!arg||!arg[0]) { io_put_string("usage: cat <path>\n"); return; }
    if (!need_fs("cat")) return;
    char path[256]; resolve_and_normalize(arg, path, sizeof(path));
    fd_t fd = fs_open(path, O_RDONLY, 0);
    if (fd < 0) { io_put_string("cat: cannot open: "); io_put_string(path); io_put_char('\n'); return; }
    char buf[512]; ssize_t n;
    while ((n = fs_read(fd, buf, sizeof(buf)-1)) > 0) { buf[n]='\0'; io_put_string(buf); }
    io_put_char('\n');
    fs_close(fd);
}

static void cmd_write(const char *arg) {
    if (!arg||!arg[0]) { io_put_string("usage: write <path> <text>\n"); return; }
    if (!need_fs("write")) return;
    char abuf[256]; strncpy(abuf, arg, sizeof(abuf)-1); abuf[sizeof(abuf)-1]='\0';
    char *text = split_first(abuf);
    if (!text||!text[0]) { io_put_string("write: no text given\n"); return; }
    char path[256]; resolve_and_normalize(abuf, path, sizeof(path));
    fd_t fd = fs_open(path, O_WRONLY|O_CREAT, 0644);
    if (fd < 0) { io_put_string("write: cannot open: "); io_put_string(path); io_put_char('\n'); return; }
    ssize_t w = fs_write(fd, text, strlen(text));
    char nl = '\n'; fs_write(fd, &nl, 1);
    fs_close(fd);
    print_u64((uint64_t)w); io_put_string(" bytes written to "); io_put_string(path); io_put_char('\n');
}

static void cmd_echo(const char *arg) {
    if (arg && arg[0]) io_put_string(arg);
    io_put_char('\n');
}

static void cmd_copy(const char *arg) {
    if (!arg || !arg[0]) { io_put_string("usage: copy <src> <dst>\n"); return; }
    if (!need_fs("copy")) return;

    char tmp[256]; strncpy(tmp, arg, sizeof(tmp)-1); tmp[sizeof(tmp)-1]='\0';
    char *src = tmp;
    char *dst = split_first(tmp);
    if (!dst || !dst[0]) { io_put_string("usage: copy <src> <dst>\n"); return; }

    char src_path[256]; resolve_and_normalize(src, src_path, sizeof(src_path));
    char dst_path[256]; resolve_and_normalize(dst, dst_path, sizeof(dst_path));

    if (installer_copy_file(src_path, dst_path) == 0) {
        io_put_string("copy: copied "); io_put_string(src_path); io_put_string(" -> ");
        io_put_string(dst_path); io_put_char('\n');
    } else {
        io_put_string("copy: failed\n");
    }
}

static void cmd_run(const char *arg) {
    if (!arg||!arg[0]) { io_put_string("usage: run <path.trp>\n"); return; }
    char path[256]; resolve_and_normalize(arg, path, sizeof(path));
    io_put_string("run: loading "); io_put_string(path); io_put_string(" ...\n");
    process_t *proc = process_create(path, 1);
    if (!proc) { io_put_string("run: failed to create process\n"); return; }
    int rc = loader_load_executable(path, proc->pid);
    if (rc != 0 && g_force_mode) {
        /* loader_load_executable() rejects on unrecognized extension
         * or a failed format-specific loader (see loader_enhanced.c).
         * Forced, skip that gate and hand it to the TRP loader
         * directly regardless of extension - it's still that loader's
         * own internal validation from here, not a guarantee it'll
         * actually run correctly. */
        io_put_string("run: normal loader rejected it - forcing it through "
                       "the TRP loader anyway\n");
        rc = loader_load_trp(path, proc->pid);
    }
    if (rc == 0) {
        process_start(proc->pid);
    } else {
        io_put_string("run: loader rejected "); io_put_string(path); io_put_char('\n');
    }
}

static void cmd_free(void) {
    uint64_t al=0,ar=0,ac=0,fr=0;
    heap_get_stats(&al,&ar,&ac,&fr);
    io_put_string("heap  total : "); print_size(ar);               io_put_char('\n');
    io_put_string("      used  : "); print_size(al);               io_put_char('\n');
    io_put_string("      free  : "); print_size(ar>al?ar-al:0);    io_put_char('\n');
}

static void cmd_hardware(void) {
    io_put_string("=== CPU ===\n  ");
    char brand[49];
    cpu_get_brand_string(brand);
    io_put_string(brand); io_put_char('\n');

    io_put_string("\n=== RAM ===\n");
    io_put_string("  detected  : "); print_u64((uint64_t)pmm_get_total_ram_kb() / 1024); io_put_string(" MB\n");
    io_put_string("  allocated : "); print_u64((uint64_t)pmm_get_ram_limit_kb() / 1024); io_put_string(" MB  (see `mem`)\n");

    io_put_string("\n=== Ports / onboard controllers ===\n");
    io_put_string("  PS/2 keyboard   (0x60/0x64)\n");
    io_put_string("  PS/2 mouse      (0x60/0x64, aux channel)\n");
    io_put_string("  Serial COM1     (0x3F8)\n");
    if (uhci_available()) io_put_string("  USB (UHCI)      controller present\n");

    io_put_string("\n=== Drives ===\n");
    ata_detect_all();
    int any_drive = 0;
    for (int i = 0; i < 4; i++) {
        if (ata_drive_present(i)) {
            any_drive = 1;
            io_put_string("  ATA drive "); print_u64((uint64_t)i); io_put_char('\n');
        }
    }
    if (!any_drive) io_put_string("  (none detected)\n");

    io_put_string("\n=== PCI / PCIe devices ===\n");
    static pci_device_t devs[64];
    int count = pci_scan(devs, 64);
    if (count <= 0) {
        io_put_string("  (none found)\n");
    } else {
        for (int i = 0; i < count; i++) {
            io_put_string("  ");
            print_hex_u32(devs[i].bus, 2);      io_put_char(':');
            print_hex_u32(devs[i].device, 2);   io_put_char('.');
            print_hex_u32(devs[i].function, 1);
            io_put_string("  vendor="); print_hex_u32(devs[i].vendor_id, 4);
            io_put_string(" device=");  print_hex_u32(devs[i].device_id, 4);
            io_put_string(" class=");   print_hex_u32(devs[i].class_code, 2);
            io_put_char(':');           print_hex_u32(devs[i].subclass, 2);
            io_put_char('\n');
        }
        io_put_string("  ("); print_u64((uint64_t)count); io_put_string(" device(s) total)\n");
    }
}

/* Parses a bare number with an optional unit ("14", "14MB", "512KB",
 * "2GB") into an exact KB amount. Defaults to MB when no unit is
 * given, matching `mem 14 MB`'s style but also accepting `mem 14`. */
static int parse_mem_amount_kb(const char *arg, uint32_t *out_kb) {
    while (*arg == ' ') arg++;
    if (*arg < '0' || *arg > '9') return 0;
    uint64_t n = 0;
    while (*arg >= '0' && *arg <= '9') { n = n * 10 + (uint64_t)(*arg - '0'); arg++; }
    while (*arg == ' ') arg++;
    uint64_t mult = 1024; /* default unit: MB */
    if (*arg) {
        char u = (char)((*arg >= 'a' && *arg <= 'z') ? *arg - 32 : *arg);
        if      (u == 'K') mult = 1;
        else if (u == 'M') mult = 1024;
        else if (u == 'G') mult = 1024ull * 1024;
        else return 0;
    }
    uint64_t kb = n * mult;
    if (kb == 0 || kb > 0xFFFFFFFFull) return 0;
    *out_kb = (uint32_t)kb;
    return 1;
}

static void cmd_mem(const char *arg) {
    if (!arg || !arg[0]) {
        io_put_string("usage: mem <amount>[KB|MB|GB] | mem max | mem least\n");
        io_put_string("  currently allocated: ");
        print_u64((uint64_t)pmm_get_ram_limit_kb() / 1024); io_put_string(" MB of ");
        print_u64((uint64_t)pmm_get_total_ram_kb() / 1024); io_put_string(" MB detected\n");
        return;
    }

    char lower[16]; size_t li = 0;
    for (; arg[li] && arg[li] != ' ' && li < sizeof(lower)-1; li++)
        lower[li] = (char)((arg[li] >= 'A' && arg[li] <= 'Z') ? arg[li] + 32 : arg[li]);
    lower[li] = '\0';

    uint8_t mode; uint32_t custom_kb = 0;
    if (strcmp(lower, "max") == 0) {
        mode = TRBL_MEM_MODE_MAX;
    } else if (strcmp(lower, "least") == 0 || strcmp(lower, "lest") == 0) {
        mode = TRBL_MEM_MODE_LEAST;
    } else {
        if (!parse_mem_amount_kb(arg, &custom_kb)) {
            io_put_string("mem: bad amount - try `mem 512MB`, `mem 2GB`, `mem max`, or `mem least`\n");
            return;
        }
        mode = TRBL_MEM_MODE_CUSTOM;
    }

    if (pmm_apply_ram_config(mode, custom_kb) != 0) {
        io_put_string("mem: failed to apply (value out of range?)\n");
        return;
    }
    trbl_settings_set_memory(mode, custom_kb); /* persists across reboots too */

    io_put_string("mem: now allocated "); print_u64((uint64_t)pmm_get_ram_limit_kb() / 1024);
    io_put_string(" MB of "); print_u64((uint64_t)pmm_get_total_ram_kb() / 1024);
    io_put_string(" MB detected\n");
}

static void cmd_force(const char *arg) {
    if (!arg || !arg[0]) {
        io_put_string("usage: force <command> - runs <command>, skipping the "
                       "OS's own safety/validation checks for it\n");
        return;
    }
    int was_forcing = g_force_mode;
    g_force_mode = 1;
    sys_shell_dispatch(arg);
    g_force_mode = was_forcing;
}

static void cmd_desktop(void) {
    desktop_run();
}

/* ── Resolution presets, capped at 1080p per what was asked for ─────────── */
typedef struct { const char *name; uint32_t w; uint32_t h; } res_preset_t;
static const res_preset_t RES_PRESETS[] = {
    { "800x600",   800,  600  },
    { "1024x768",  1024, 768  },
    { "1280x720",  1280, 720  },
    { "1280x1024", 1280, 1024 },
    { "1366x768",  1366, 768  },
    { "1920x1080", 1920, 1080 },
};
#define RES_PRESET_COUNT (sizeof(RES_PRESETS) / sizeof(RES_PRESETS[0]))

/* Accepts either a preset name ("1280x720") or a raw "WxH" pair typed
 * directly - both land on the same graphics_set_mode() call, presets
 * just save the user from typing exact numbers for the common cases. */
static int parse_resolution(const char *arg, uint32_t *w, uint32_t *h) {
    for (unsigned i = 0; i < RES_PRESET_COUNT; i++) {
        if (strcmp(arg, RES_PRESETS[i].name) == 0) {
            *w = RES_PRESETS[i].w; *h = RES_PRESETS[i].h;
            return 1;
        }
    }
    const char *x = strchr(arg, 'x');
    if (!x) return 0;
    uint32_t width = 0, height = 0;
    for (const char *p = arg; p < x; p++) {
        if (*p < '0' || *p > '9') return 0;
        width = width * 10 + (uint32_t)(*p - '0');
    }
    for (const char *p = x + 1; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
        height = height * 10 + (uint32_t)(*p - '0');
    }
    if (width == 0 || height == 0) return 0;
    *w = width; *h = height;
    return 1;
}

/* Parses one "a.b.c.d" IPv4 octet quad starting at s, writing to
 * out[4] and advancing *end past it (leaving any trailing whitespace
 * for the caller). Returns 0 on any malformed input (out-of-range
 * octet, missing dot, empty field) without touching out[]/end. */
static int parse_ipv4(const char *s, uint8_t out[4], const char **end) {
    uint8_t tmp[4];
    const char *p = s;
    for (int octet = 0; octet < 4; octet++) {
        if (*p < '0' || *p > '9') return 0;
        uint32_t v = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (uint32_t)(*p - '0');
            p++; digits++;
            if (digits > 3 || v > 255) return 0;
        }
        tmp[octet] = (uint8_t)v;
        if (octet < 3) {
            if (*p != '.') return 0;
            p++;
        }
    }
    if (*p != '\0' && *p != ' ') return 0;
    memcpy(out, tmp, 4);
    *end = p;
    return 1;
}

static void print_settings_usage(void) {
    io_put_string("usage:\n");
    io_put_string("  settings                         show current settings\n");
    io_put_string("  settings resolution <WxH>         e.g. 1280x720 (max 1920x1080)\n");
    io_put_string("  settings network <off|dhcp>       boot-time network bring-up\n");
    io_put_string("  settings network static <ip> <mask> <gateway> <dns>\n");
    io_put_string("  settings textcolor <name|0-15>    e.g. white, lightcyan, 15\n");
    io_put_string("  settings bgcolor <name|0-15>      e.g. black, blue, 0\n");
    io_put_string("  settings statusbarcolor <name|0-15>  status/time bar text color\n");
    io_put_string("colors: black blue green cyan red magenta brown lightgrey darkgrey\n");
    io_put_string("        lightblue lightgreen lightcyan lightred lightmagenta yellow white\n");
}

static void cmd_settings(const char *arg) {
    if (!arg || !arg[0]) {
        char buf[8];
        io_put_string("resolution:       "); io_put_string(g_resolution); io_put_char('\n');

        io_put_string("text color:       ");
        syscfg_get_or("text_color", buf, sizeof(buf), "7");
        vga_color_t c; if (parse_color(buf, &c)) io_put_string(color_name(c)); else io_put_string(buf);
        io_put_char('\n');

        io_put_string("background color: ");
        syscfg_get_or("bg_color", buf, sizeof(buf), "0");
        if (parse_color(buf, &c)) io_put_string(color_name(c)); else io_put_string(buf);
        io_put_char('\n');

        io_put_string("status bar color: ");
        syscfg_get_or("statusbar_color", buf, sizeof(buf), "15");
        if (parse_color(buf, &c)) io_put_string(color_name(c)); else io_put_string(buf);
        io_put_char('\n');

        print_settings_usage();
        return;
    }

    /* Split "settings <sub> <value>" into sub/value in place. */
    /* value needs room for "static ip mask gateway dns" - four dotted-
     * quad IPv4 addresses plus spaces, up to 15*4 + "static " + 3
     * spaces = ~70 chars; 24 (fine for resolution/color values) would
     * silently truncate a static network config mid-address. */
    char sub[24]; char value[80];
    unsigned i = 0;
    while (arg[i] && arg[i] != ' ' && i < sizeof(sub) - 1) { sub[i] = arg[i]; i++; }
    sub[i] = '\0';
    while (arg[i] == ' ') i++;
    unsigned j = 0;
    while (arg[i] && j < sizeof(value) - 1) { value[j++] = arg[i++]; }
    value[j] = '\0';

    if (!value[0]) {
        print_settings_usage();
        return;
    }

    if (strcmp(sub, "resolution") == 0) {
        uint32_t w, h;
        if (!parse_resolution(value, &w, &h)) {
            io_put_string("settings: unrecognized resolution - try e.g. 1280x720\n");
            return;
        }
        if (w > 1920 || h > 1080) {
            io_put_string("settings: max supported resolution is 1920x1080\n");
            return;
        }
        if (graphics_set_mode(GRAPHICS_MODE_VESA_32BIT, w, h) != 0) {
            io_put_string("settings: mode switch failed - this display adapter may not\n");
            io_put_string("support the Bochs/QEMU VBE interface, or rejected that mode.\n");
            io_put_string("check the serial log ([VBE] lines) for details.\n");
            return;
        }
        strncpy(g_resolution, value, sizeof(g_resolution) - 1);
        g_resolution[sizeof(g_resolution) - 1] = '\0';
        syscfg_set("resolution", g_resolution);
        trpfs_sync();

        /* Also persist to the TRBL boot-time settings sector (see
         * trbl_settings.h) - syscfg.ini only affects the live
         * in-OS display API; without this, the change would be lost
         * on reboot and stage2.s would keep booting into whatever
         * resolution was last written here (or the 1024x768 default
         * on a fresh install). Not fatal if it fails - the live
         * session's resolution has already changed either way, this
         * only affects what greets you on the next boot. */
        if (trbl_settings_set_resolution((uint16_t)w, (uint16_t)h) != 0) {
            io_put_string("settings: warning - could not persist resolution to the\n");
            io_put_string("TRBL boot sector; it will apply this session but not survive reboot.\n");
        }

        sys_shell_update_statusbar();
        io_put_string("settings: resolution set to ");
        io_put_string(g_resolution);
        io_put_char('\n');
        return;
    }

    if (strcmp(sub, "network") == 0) {
        /* settings network off
         * settings network dhcp
         * settings network static <ip> <mask> <gateway> <dns>
         * Stores boot-time network config TRBL's kernel-side bring-up
         * (not yet wired to the live NIC - see trbl_settings.c) will
         * read on a future boot. Doesn't touch the live `ifconfig`
         * session at all; this is boot config, not runtime config. */
        char mode_str[16]; unsigned k = 0;
        while (value[k] && value[k] != ' ' && k < sizeof(mode_str) - 1) {
            mode_str[k] = value[k]; k++;
        }
        mode_str[k] = '\0';

        if (strcmp(mode_str, "off") == 0) {
            if (trbl_settings_set_network(TRBL_NET_OFF, 0, 0, 0, 0) != 0) {
                io_put_string("settings: failed to write TRBL settings sector\n");
                return;
            }
            io_put_string("settings: boot-time networking disabled\n");
            return;
        }
        if (strcmp(mode_str, "dhcp") == 0) {
            if (trbl_settings_set_network(TRBL_NET_DHCP, 0, 0, 0, 0) != 0) {
                io_put_string("settings: failed to write TRBL settings sector\n");
                return;
            }
            io_put_string("settings: boot-time networking set to DHCP\n");
            return;
        }
        if (strcmp(mode_str, "static") == 0) {
            const char *rest = value + k;
            while (*rest == ' ') rest++;
            uint8_t ip[4], mask[4], gw[4], dns[4];
            if (!parse_ipv4(rest, ip, &rest) ||
                !parse_ipv4(rest, mask, &rest) ||
                !parse_ipv4(rest, gw, &rest) ||
                !parse_ipv4(rest, dns, &rest)) {
                io_put_string("settings: usage - settings network static <ip> <mask> <gateway> <dns>\n");
                return;
            }
            if (trbl_settings_set_network(TRBL_NET_STATIC, ip, mask, gw, dns) != 0) {
                io_put_string("settings: failed to write TRBL settings sector\n");
                return;
            }
            io_put_string("settings: boot-time networking set to static\n");
            return;
        }
        io_put_string("settings: usage - settings network <off|dhcp|static ip mask gw dns>\n");
        return;
    }

    if (strcmp(sub, "textcolor") == 0 || strcmp(sub, "bgcolor") == 0 ||
        strcmp(sub, "statusbarcolor") == 0) {
        vga_color_t c;
        if (!parse_color(value, &c)) {
            io_put_string("settings: unrecognized color - see 'settings' for the list\n");
            return;
        }

        char cur_text[8], cur_bg[8], cur_sb[8];
        syscfg_get_or("text_color", cur_text, sizeof(cur_text), "7");
        syscfg_get_or("bg_color", cur_bg, sizeof(cur_bg), "0");
        syscfg_get_or("statusbar_color", cur_sb, sizeof(cur_sb), "15");
        vga_color_t text_c, bg_c, sb_c;
        if (!parse_color(cur_text, &text_c)) text_c = VGA_LIGHT_GREY;
        if (!parse_color(cur_bg, &bg_c)) bg_c = VGA_BLACK;
        if (!parse_color(cur_sb, &sb_c)) sb_c = VGA_WHITE;

        if (strcmp(sub, "textcolor") == 0) text_c = c;
        else if (strcmp(sub, "bgcolor") == 0) bg_c = c;
        else sb_c = c;

        vga_set_color(text_c, bg_c);
        vga_set_statusbar_color(sb_c, VGA_BLUE);

        char nb[4];
        int_to_str((int)text_c, nb, sizeof(nb));
        syscfg_set("text_color", nb);

        int_to_str((int)bg_c, nb, sizeof(nb));
        syscfg_set("bg_color", nb);

        int_to_str((int)sb_c, nb, sizeof(nb));
        syscfg_set("statusbar_color", nb);

        trpfs_sync();
        sys_shell_update_statusbar();
        io_put_string("settings: ");
        io_put_string(sub);
        io_put_string(" set to ");
        io_put_string(color_name(c));
        io_put_char('\n');
        return;
    }

    io_put_string("settings: unknown setting '");
    io_put_string(sub);
    io_put_string("'\n");
    print_settings_usage();
}

static void cmd_halt(void) {
    io_put_string("Halted.\n");
    __asm__ volatile("cli");
    while (1) __asm__ volatile("hlt");
}

static void cmd_reboot(void) {
    io_put_string("Rebooting...\n");
    __asm__ volatile("cli");
    for (volatile int i=0;i<100000;i++);
    __asm__ volatile("mov $0xFE,%%al;out %%al,$0x64":::"al");
    while (1) __asm__ volatile("hlt");
}


/* Callback that captures the first regular file name found in a directory */
typedef struct { char name[128]; int found; } first_file_ctx_t;
static int first_file_cb(const char *name, uint8_t nlen, uint8_t type, void *ctx) {
    first_file_ctx_t *c = (first_file_ctx_t *)ctx;
    if (!c->found && type == FILE_TYPE_REGULAR) {
        uint8_t copy = nlen < 127 ? nlen : 127;
        for (uint8_t i = 0; i < copy; i++) c->name[i] = name[i];
        c->name[copy] = '\0';
        c->found = 1;
    }
    return 0;
}

static void cmd_trpbuild(const char *arg) {
    if (!arg || !arg[0]) {
        io_put_string("usage: trpbuild <folder>\n");
        io_put_string("  folder must contain manifest.txt and a code/ subfolder\n");
        return;
    }
    if (!need_fs("trpbuild")) return;

    /* Resolve the source folder */
    char folder[256];
    resolve_and_normalize(arg, folder, sizeof(folder));

    /* Verify it's a directory */
    inode_t st;
    if (fs_stat(folder, &st) != 0 || !FS_IS_DIR(st.mode)) {
        io_put_string("trpbuild: not a directory: "); io_put_string(folder); io_put_char('\n');
        return;
    }

    /* Read manifest.txt */
    char manifest_path[280];
    strncpy(manifest_path, folder, sizeof(manifest_path)-20);
    size_t fl = strlen(manifest_path);
    if (fl > 0 && manifest_path[fl-1] != '/') manifest_path[fl++] = '/';
    strncpy(manifest_path + fl, "manifest.txt", sizeof(manifest_path)-fl-1);

    fd_t mfd = fs_open(manifest_path, O_RDONLY, 0);
    if (mfd < 0) {
        io_put_string("trpbuild: missing manifest.txt in "); io_put_string(folder); io_put_char('\n');
        return;
    }
    char manifest_buf[2048];
    ssize_t mlen = fs_read(mfd, manifest_buf, sizeof(manifest_buf)-1);
    fs_close(mfd);
    if (mlen <= 0) { io_put_string("trpbuild: manifest.txt is empty\n"); return; }
    manifest_buf[mlen] = '\0';

    /* Find first file inside code/ subfolder as the payload */
    char code_path[280];
    strncpy(code_path, folder, sizeof(code_path)-10);
    fl = strlen(code_path);
    if (fl > 0 && code_path[fl-1] != '/') code_path[fl++] = '/';
    strncpy(code_path + fl, "code", sizeof(code_path)-fl-1);

    first_file_ctx_t ffc = { .found = 0 };
    fs_readdir(code_path, first_file_cb, &ffc);

    uint8_t  *payload     = NULL;
    uint32_t  payload_len = 0;

    if (ffc.found) {
        char payload_path[300];
        strncpy(payload_path, code_path, sizeof(payload_path)-80);
        size_t cl = strlen(payload_path);
        if (payload_path[cl-1] != '/') payload_path[cl++] = '/';
        strncpy(payload_path + cl, ffc.name, sizeof(payload_path)-cl-1);

        inode_t pst;
        if (fs_stat(payload_path, &pst) == 0 && pst.size > 0) {
            fd_t pfd = fs_open(payload_path, O_RDONLY, 0);
            if (pfd >= 0) {
                payload = (uint8_t *)kmalloc((size_t)pst.size);
                if (payload) {
                    ssize_t pr = fs_read(pfd, payload, (size_t)pst.size);
                    payload_len = pr > 0 ? (uint32_t)pr : 0;
                }
                fs_close(pfd);
            }
        }
    }

    /* Build output path: <folder>.trp */
    char out_path[300];
    strncpy(out_path, folder, sizeof(out_path)-6);
    /* Strip trailing slash */
    size_t ol = strlen(out_path);
    if (ol > 1 && out_path[ol-1] == '/') out_path[--ol] = '\0';
    strncpy(out_path + ol, ".trp", sizeof(out_path)-ol-1);

    fd_t ofd = fs_open(out_path, O_WRONLY|O_CREAT, 0755);
    if (ofd < 0) {
        io_put_string("trpbuild: cannot create output: "); io_put_string(out_path); io_put_char('\n');
        if (payload) kfree(payload);
        return;
    }

    /* Write TRP header + manifest + payload */
    trp_file_header_t hdr;
    hdr.magic           = TRP_MAGIC;
    hdr.version         = 1;
    hdr.manifest_offset = (uint32_t)sizeof(hdr);
    hdr.manifest_len    = (uint32_t)mlen;
    hdr.payload_offset  = hdr.manifest_offset + hdr.manifest_len;
    hdr.payload_len     = payload_len;

    fs_write(ofd, &hdr,         sizeof(hdr));
    fs_write(ofd, manifest_buf, (size_t)mlen);
    if (payload && payload_len) fs_write(ofd, payload, payload_len);
    fs_close(ofd);

    if (payload) kfree(payload);

    io_put_string("trpbuild: built "); io_put_string(out_path);
    io_put_string(" (manifest="); print_u64((uint64_t)mlen); io_put_string("B");
    if (payload_len) { io_put_string(", payload="); print_u64(payload_len); io_put_string("B"); }
    io_put_string(")\n");
}

static void cmd_ifconfig(const char *arg) {
    net_device_t *dev = net_get_device();
    if (!dev) { io_put_string("ifconfig: no network device present\n"); return; }

    if (!arg || !arg[0]) {
        char macstr[18], ipstr[16], nmstr[16], gwstr[16], dnsstr[16];
        mac_to_string(dev->mac, macstr);
        ip_to_string(dev->ip, ipstr);
        ip_to_string(dev->netmask, nmstr);
        ip_to_string(dev->gateway_ip, gwstr);
        ip_to_string(dev->dns_ip, dnsstr);
        io_put_string(dev->name); io_put_string("  mac "); io_put_string(macstr); io_put_char('\n');
        io_put_string("  inet "); io_put_string(ipstr);
        io_put_string("  netmask "); io_put_string(nmstr);
        io_put_string("  gateway "); io_put_string(gwstr);
        io_put_string("  dns "); io_put_string(dnsstr); io_put_char('\n');
        return;
    }

    char buf[96]; strncpy(buf, arg, sizeof(buf) - 1); buf[sizeof(buf)-1] = '\0';
    char *rest1 = split_first(buf);
    char *rest2 = rest1 ? split_first(rest1) : NULL;
    char *rest3 = rest2 ? split_first(rest2) : NULL; /* optional dns */

    uint32_t ip, nm, gw, dns = 0;
    if (!rest1 || !rest2 || !ip_parse(buf, &ip) || !ip_parse(rest1, &nm) || !ip_parse(rest2, &gw)) {
        io_put_string("usage: ifconfig <ip> <netmask> <gateway> [dns]\n");
        return;
    }
    if (rest3 && rest3[0]) { if (!ip_parse(rest3, &dns)) { io_put_string("ifconfig: bad dns address\n"); return; } }

    dev->ip = ip; dev->netmask = nm; dev->gateway_ip = gw;
    if (rest3 && rest3[0]) dev->dns_ip = dns;
    io_put_string("ifconfig: updated\n");
}

static void cmd_ping(const char *arg) {
    net_device_t *dev = net_get_device();
    if (!dev) { io_put_string("ping: no network device present\n"); return; }
    if (!arg || !arg[0]) { io_put_string("usage: ping <ip or hostname>\n"); return; }
    if (!dev->ip) { io_put_string("ping: no IP configured — run ifconfig first\n"); return; }

    uint32_t target;
    if (!ip_parse(arg, &target)) {
        io_put_string("ping: resolving "); io_put_string(arg); io_put_string("...\n");
        if (!dns_resolve(arg, &target, 3000)) {
            io_put_string("ping: could not resolve "); io_put_string(arg); io_put_char('\n');
            return;
        }
        char ipstr[16]; ip_to_string(target, ipstr);
        io_put_string("ping: "); io_put_string(arg); io_put_string(" resolved to "); io_put_string(ipstr); io_put_char('\n');
    }

    io_put_string("ping: sending echo request...\n");
    uint16_t id = 1, seq = 1;
    if (icmp_send_echo_request(target, id, seq) != 0) {
        io_put_string("ping: send failed (ARP timeout or no route)\n");
        return;
    }

    uint32_t waited = 0;
    while (waited < 3000) {
        pit_sleep(50); waited += 50;
        if (icmp_last_reply_matches(target, id, seq)) {
            io_put_string("ping: reply received (");
            char numbuf[8]; int n=0; uint32_t v=waited; if(v==0) numbuf[n++]='0'; else { char tmp[8]; int tn=0; while(v){tmp[tn++]=(char)('0'+v%10);v/=10;} while(tn) numbuf[n++]=tmp[--tn]; } numbuf[n]='\0';
            io_put_string(numbuf); io_put_string("ms)\n");
            return;
        }
    }
    io_put_string("ping: no reply (timed out)\n");
}


static void pkg_ensure_dir(void) {
    inode_t st;
    if (fs_stat(PKG_DIR, &st) != 0) fs_mkdir(PKG_DIR, 0755);
    if (fs_stat(PKG_INDEX, &st) != 0) {
        fd_t fd = fs_open(PKG_INDEX, O_WRONLY|O_CREAT, 0644);
        if (fd >= 0) fs_close(fd);
    }
}

/* List all lines in index.txt */
typedef struct { int count; } list_ctx_t;

static void cmd_trpm_list(void) {
    if (!need_fs("trpm")) return;
    pkg_ensure_dir();

    fd_t fd = fs_open(PKG_INDEX, O_RDONLY, 0);
    if (fd < 0) { io_put_string("trpm: cannot read index\n"); return; }

    char buf[1024]; ssize_t n = fs_read(fd, buf, sizeof(buf)-1);
    fs_close(fd);

    if (n <= 0) { io_put_string("trpm: no packages installed\n"); return; }
    buf[n] = '\0';

    io_put_string("Installed packages:\n");
    /* Walk lines */
    char *p = buf;
    int count = 0;
    while (*p) {
        char *nl = p;
        while (*nl && *nl != '\n') nl++;
        if (nl > p) {
            io_put_string("  "); 
            char tmp = *nl; *nl = '\0';
            io_put_string(p);
            *nl = tmp;
            io_put_char('\n');
            count++;
        }
        if (*nl == '\n') nl++;
        p = nl;
    }
    if (!count) io_put_string("  (none)\n");
}

#define TRPM_REPO_CONFIG_PATH "/trpm_repo.txt"

static int trpm_get_repo_host(char *out, int outlen) {
    fd_t fd = fs_open(TRPM_REPO_CONFIG_PATH, O_RDONLY, 0);
    if (fd < 0) return 0;
    ssize_t n = fs_read(fd, out, (size_t)(outlen - 1));
    fs_close(fd);
    if (n <= 0) return 0;
    out[n] = '\0';
    /* strip trailing newline if present */
    for (ssize_t i = n - 1; i >= 0 && (out[i] == '\n' || out[i] == '\r'); i--) out[i] = '\0';
    return out[0] != '\0';
}

static void cmd_trpm_repo(const char *arg) {
    if (!need_fs("trpm")) return;
    if (!arg || !arg[0]) {
        char host[256];
        if (trpm_get_repo_host(host, sizeof(host))) {
            io_put_string("current repo host: "); io_put_string(host); io_put_char('\n');
        } else {
            io_put_string("no repo host configured. Set one with: trpm repo <host>\n");
            io_put_string("Note: the real GitHub repo is HTTPS-only; this OS speaks plain\n");
            io_put_string("HTTP only. Point this at a plain-HTTP proxy in front of it.\n");
        }
        return;
    }
    fd_t fd = fs_open(TRPM_REPO_CONFIG_PATH, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) { io_put_string("trpm: could not write repo config\n"); return; }
    fd_puts(fd, arg);
    fs_close(fd);
    io_put_string("trpm: repo host set to "); io_put_string(arg); io_put_char('\n');
}

static int find_hmac_header(const uint8_t *headers, uint32_t header_len, uint8_t out[32]) {
    static const char *name = "X-Trp-Hmac:";
    size_t name_len = strlen(name);
    for (uint32_t i = 0; i + name_len <= header_len; i++) {
        int match = 1;
        for (size_t j = 0; j < name_len; j++) {
            char a = (char)headers[i + j];
            char b = name[j];
            if (a >= 'a' && a <= 'z') a = (char)(a - 32);
            if (b >= 'a' && b <= 'z') b = (char)(b - 32);
            if (a != b) { match = 0; break; }
        }
        if (!match) continue;

        uint32_t p = i + (uint32_t)name_len;
        while (p < header_len && headers[p] == ' ') p++;
        if (p + 64 > header_len) return 0;

        for (int k = 0; k < 32; k++) {
            uint8_t hi = headers[p + (uint32_t)(k*2)];
            uint8_t lo = headers[p + (uint32_t)(k*2 + 1)];
            int hv, lv;
            if (hi >= '0' && hi <= '9') hv = hi - '0';
            else if (hi >= 'a' && hi <= 'f') hv = hi - 'a' + 10;
            else if (hi >= 'A' && hi <= 'F') hv = hi - 'A' + 10;
            else return 0;
            if (lo >= '0' && lo <= '9') lv = lo - '0';
            else if (lo >= 'a' && lo <= 'f') lv = lo - 'a' + 10;
            else if (lo >= 'A' && lo <= 'F') lv = lo - 'A' + 10;
            else return 0;
            out[k] = (uint8_t)((hv << 4) | lv);
        }
        return 1;
    }
    return 0;
}

static int trpm_fetch_remote(const char *host_and_port, const char *pkgname, const char *dest_path) {
    net_device_t *dev = net_get_device();
    if (!dev || !dev->ip) {
        io_put_string("trpm: no network configured — run ifconfig first\n");
        return 0;
    }

    char host[224];
    uint16_t port = 80;
    strncpy(host, host_and_port, sizeof(host) - 1); host[sizeof(host)-1] = '\0';
    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        uint32_t p = 0;
        for (const char *d = colon + 1; *d; d++) { if (*d < '0' || *d > '9') { p = 0; break; } p = p * 10 + (uint32_t)(*d - '0'); }
        if (p > 0 && p <= 65535) port = (uint16_t)p;
    }

    uint32_t ip;
    io_put_string("trpm: resolving "); io_put_string(host); io_put_string("...\n");
    if (!dns_resolve(host, &ip, 3000)) {
        io_put_string("trpm: could not resolve "); io_put_string(host); io_put_char('\n');
        return 0;
    }

    char path[160] = "/";
    strncpy(path + 1, pkgname, sizeof(path) - 6);
    strncpy(path + strlen(path), ".trp", 4);

    static uint8_t response_buf[512 * 1024];
    uint8_t *body; uint32_t body_len; int status = 0;

    io_put_string("trpm: fetching http://"); io_put_string(host); io_put_string(path); io_put_string("...\n");
    if (!http_get(ip, port, host, path, response_buf, sizeof(response_buf), &body, &body_len, &status, 5000)) {
        io_put_string("trpm: fetch failed (no response)\n");
        return 0;
    }
    if (status != 200) {
        io_put_string("trpm: server returned HTTP status ");
        char numbuf[8]; int n = 0, s = status;
        if (s == 0) numbuf[n++] = '0';
        else { char tmp[8]; int tn = 0; while (s) { tmp[tn++] = (char)('0' + s % 10); s /= 10; } while (tn) numbuf[n++] = tmp[--tn]; }
        numbuf[n] = '\0';
        io_put_string(numbuf); io_put_char('\n');
        return 0;
    }


    uint32_t header_len = (uint32_t)(body - response_buf) - 4;
    uint8_t claimed_tag[32], computed_tag[32];
    if (!find_hmac_header(response_buf, header_len, claimed_tag)) {
        io_put_string("trpm: refusing download - server did not send an X-Trp-Hmac header\n");
        return 0;
    }
    hmac_sha256((const uint8_t *)TRP_HMAC_SECRET, strlen(TRP_HMAC_SECRET), body, body_len, computed_tag);
    if (!sha256_digest_equal(claimed_tag, computed_tag)) {
        io_put_string("trpm: refusing download - HMAC mismatch (package may have been tampered with in transit, or the proxy's secret doesn't match TRP_HMAC_SECRET)\n");
        return 0;
    }

    fd_t dst = fs_open(dest_path, O_WRONLY | O_CREAT, 0644);
    if (dst < 0) { io_put_string("trpm: could not write downloaded file\n"); return 0; }
    fs_write(dst, body, body_len);
    fs_close(dst);

    io_put_string("trpm: downloaded "); io_put_string(pkgname);
    io_put_string(" ("); 
    { char numbuf[12]; int n=0; uint32_t v=body_len; if(v==0) numbuf[n++]='0'; else { char tmp[12]; int tn=0; while(v){tmp[tn++]=(char)('0'+v%10);v/=10;} while(tn) numbuf[n++]=tmp[--tn]; } numbuf[n]='\0'; io_put_string(numbuf); }
    io_put_string(" bytes)\n");
    return 1;
}

static void cmd_trpm_install(const char *arg) {
    if (!arg || !arg[0]) { io_put_string("usage: trpm install <pkg.trp | package-name>\n"); return; }
    if (!need_fs("trpm")) return;
    pkg_ensure_dir();

    char path[256]; resolve_and_normalize(arg, path, sizeof(path));
    char pkgname[128];
    int is_remote = 0;

    inode_t st;
    if (fs_stat(path, &st) != 0) {
        char host[256];
        if (!trpm_get_repo_host(host, sizeof(host))) {
            io_put_string("trpm: not found locally: "); io_put_string(arg); io_put_char('\n');
            io_put_string("trpm: no repo host configured for remote install — set one with: trpm repo <host>\n");
            return;
        }

        strncpy(pkgname, arg, sizeof(pkgname) - 1); pkgname[sizeof(pkgname)-1] = '\0';
        size_t pl = strlen(pkgname);
        if (pl > 4 && strcmp(pkgname + pl - 4, ".trp") == 0) pkgname[pl - 4] = '\0';

        strncpy(path, "/_trpm_download.trp", sizeof(path) - 1);
        if (!trpm_fetch_remote(host, pkgname, path)) return;
        is_remote = 1;
    }

  
    if (fs_stat(path, &st) != 0) {
        io_put_string("trpm: not found: "); io_put_string(path); io_put_char('\n'); return;
    }
    if (st.size < sizeof(trp_file_header_t)) {
        io_put_string("trpm: file too small to be a TRP package\n"); return;
    }

    fd_t fd = fs_open(path, O_RDONLY, 0);
    if (fd < 0) { io_put_string("trpm: cannot open package\n"); return; }
    trp_file_header_t hdr;
    fs_read(fd, &hdr, sizeof(hdr));
    fs_close(fd);

    if (hdr.magic != TRP_MAGIC) {
        io_put_string("trpm: invalid TRP magic - not a valid package\n"); return;
    }

    if (!is_remote) {
        const char *base = path;
        for (const char *p = path; *p; p++) if (*p == '/') base = p + 1;
        strncpy(pkgname, base, sizeof(pkgname)-1); pkgname[sizeof(pkgname)-1]='\0';
        size_t nl = strlen(pkgname);
        if (nl > 4 && strcmp(pkgname + nl - 4, ".trp") == 0) pkgname[nl-4] = '\0';
    }

    /* Copy .trp into /pkgs/<name>.trp */
    char dest[256];
    strncpy(dest, PKG_DIR "/", sizeof(dest)-1);
    size_t dl = strlen(dest);
    strncpy(dest + dl, pkgname, sizeof(dest)-dl-6);
    strncpy(dest + strlen(dest), ".trp", 5);

    /* Copy file contents */
    fd_t src = fs_open(path, O_RDONLY, 0);
    fd_t dst = fs_open(dest, O_WRONLY|O_CREAT, 0755);
    if (src < 0 || dst < 0) {
        io_put_string("trpm: copy failed\n");
        if (src >= 0) fs_close(src);
        if (dst >= 0) fs_close(dst);
        return;
    }
    char cbuf[512]; ssize_t nr;
    while ((nr = fs_read(src, cbuf, sizeof(cbuf))) > 0)
        fs_write(dst, cbuf, (size_t)nr);
    fs_close(src); fs_close(dst);

    /* Append name to index */
    fd_t idx = fs_open(PKG_INDEX, O_WRONLY|O_CREAT, 0644);
    if (idx >= 0) {
        fs_seek(idx, 0, 2); /* seek to end */
        fd_puts(idx, pkgname);
        fd_puts(idx, "\n");
        fs_close(idx);
    }

    io_put_string("trpm: installed "); io_put_string(pkgname);
    io_put_string(" -> "); io_put_string(dest); io_put_char('\n');
    io_put_string("      run it with: run "); io_put_string(dest); io_put_char('\n');
}

static void cmd_trpm_remove(const char *arg) {
    if (!arg || !arg[0]) { io_put_string("usage: trpm remove <name>\n"); return; }
    if (!need_fs("trpm")) return;

    /* Remove the .trp file from /pkgs/ */
    char dest[256];
    strncpy(dest, PKG_DIR "/", sizeof(dest)-1);
    size_t dl = strlen(dest);
    strncpy(dest + dl, arg, sizeof(dest)-dl-6);
    strncpy(dest + strlen(dest), ".trp", 5);

    if (fs_unlink(dest) != 0) {
        io_put_string("trpm: package not found: "); io_put_string(arg); io_put_char('\n'); return;
    }

    /* Rebuild index without this entry */
    fd_t fd = fs_open(PKG_INDEX, O_RDONLY, 0);
    char old_idx[2048]; ssize_t on = 0;
    if (fd >= 0) { on = fs_read(fd, old_idx, sizeof(old_idx)-1); fs_close(fd); }
    if (on > 0) old_idx[on] = '\0'; else old_idx[0] = '\0';

    fd_t wfd = fs_open(PKG_INDEX, O_WRONLY|O_CREAT, 0644);
    if (wfd >= 0) {
        char *p = old_idx;
        while (*p) {
            char *nl = p;
            while (*nl && *nl != '\n') nl++;
            size_t ll = (size_t)(nl - p);
            /* Write line only if it doesn't match the removed name */
            if (ll != strlen(arg) || strncmp(p, arg, ll) != 0) {
                fs_write(wfd, p, ll);
                fs_write(wfd, "\n", 1);
            }
            if (*nl == '\n') nl++;
            p = nl;
        }
        fs_close(wfd);
    }

    io_put_string("trpm: removed "); io_put_string(arg); io_put_char('\n');
}

static void cmd_trpm(const char *arg) {
    if (!arg || !arg[0]) {
        io_put_string("usage: trpm <list|install|remove> [args]\n"); return;
    }
    char abuf[256]; strncpy(abuf, arg, sizeof(abuf)-1); abuf[sizeof(abuf)-1]='\0';
    char *rest = split_first(abuf);

    if (strcmp(abuf, "list")    == 0) { cmd_trpm_list();           return; }
    if (strcmp(abuf, "install") == 0) { cmd_trpm_install(rest);    return; }
    if (strcmp(abuf, "remove")  == 0) { cmd_trpm_remove(rest);     return; }
    if (strcmp(abuf, "repo")    == 0) { cmd_trpm_repo(rest);       return; }

    io_put_string("trpm: unknown subcommand '"); io_put_string(abuf);
    io_put_string("' - use list, install, remove, repo\n");
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

void sys_shell_dispatch(const char *line) {
    if (!line) return;
    while (*line == ' ') line++;
    if (!*line) return;

    char buf[256]; strncpy(buf, line, sizeof(buf)-1); buf[sizeof(buf)-1]='\0';
    char *arg = split_first(buf);
    char *cmd = buf;

    if (strcmp(cmd,"help")     ==0) { cmd_help();                  return; }
    if (strcmp(cmd,"advancedhelp") ==0) { cmd_advanced_help();      return; }
    if (strcmp(cmd,"sysver")   ==0) { cmd_sysver();                return; }
    if (strcmp(cmd,"uname")    ==0) { io_put_string(KERNEL_NAME " " OS_NAME " " OS_VERSION " " BUILD_ARCH "\n"); return; }
    if (strcmp(cmd,"cls")      ==0) { cmd_cls();                   return; }
    if (strcmp(cmd,"pwd")      ==0) { cmd_pwd();                   return; }
    if (strcmp(cmd,"cd")       ==0) { cmd_cd(arg);                 return; }
    if (strcmp(cmd,"ls")       ==0) { cmd_ls(arg);                 return; }
    if (strcmp(cmd,"mkdir")    ==0) { cmd_mkdir(arg);              return; }
    if (strcmp(cmd,"touch")    ==0) { cmd_touch(arg);              return; }
    if (strcmp(cmd,"rm")       ==0) { cmd_rm(arg);                 return; }
    if (strcmp(cmd,"cat")      ==0) { cmd_cat(arg);                return; }
    if (strcmp(cmd,"write")    ==0) { cmd_write(arg);              return; }
    if (strcmp(cmd,"copy")     ==0) { cmd_copy(arg);               return; }
    if (strcmp(cmd,"echo")     ==0) { cmd_echo(arg);               return; }
    if (strcmp(cmd,"run")      ==0) { cmd_run(arg);                return; }
    if (strcmp(cmd,"desktop")  ==0) { cmd_desktop();               return; }
    if (strcmp(cmd,"settings") ==0) { cmd_settings(arg);           return; }
    if (strcmp(cmd,"trpbuild") ==0) { cmd_trpbuild(arg);           return; }
    if (strcmp(cmd,"trpm")     ==0) { cmd_trpm(arg);               return; }
    if (strcmp(cmd,"ifconfig") ==0) { cmd_ifconfig(arg);           return; }
    if (strcmp(cmd,"ping")     ==0) { cmd_ping(arg);               return; }
    if (strcmp(cmd,"free")     ==0) { cmd_free();                  return; }
    if (strcmp(cmd,"hardware") ==0) { cmd_hardware();               return; }
    if (strcmp(cmd,"mem")      ==0) { cmd_mem(arg);                 return; }
    if (strcmp(cmd,"force")    ==0) { cmd_force(arg);               return; }
    if (strcmp(cmd,"setup")    ==0 || strcmp(cmd,"oobe") ==0 || strcmp(cmd,"install") ==0) {
        installer_run();
        g_username_loaded = 0; /* force re-read in case account changed */
        if (trpfs_is_mounted()) vga_set_statusbar_enabled(1);
        return;
    }
    if (strcmp(cmd,"status")   ==0) { installer_print_status();    return; }
    if (strcmp(cmd,"halt")     ==0) { cmd_halt();                  return; }
    if (strcmp(cmd,"reboot")   ==0) { cmd_reboot();                return; }

    io_put_string("unknown command: '"); io_put_string(cmd); io_put_string("'  (type 'help')\n");
}