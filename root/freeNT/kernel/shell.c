/* =============================================================================
 * kernel/shell.c — Raw input loop with real line editing.
 *
 * Supports: visual backspace/delete, left/right arrow cursor movement
 * within the current line, up/down arrow command history, home/end.
 * Commands are dispatched to sys/shell/shell.c via sys_shell_dispatch().
 *
 * IMPORTANT: this file never uses '\r' to reposition the cursor. Not every
 * output backend treats '\r' as "return to column 0" (VGA didn't, until it
 * was patched — but relying on that is fragile). Instead we track exactly
 * how many characters have been printed since the prompt (`screen_pos`)
 * and reposition purely with '\b' (backspace), which every backend already
 * has to support correctly for normal backspacing to work.
 * ============================================================================= */

#include "io.h"
#include "string.h"
#include "keyboard.h"
#include "serial.h"
#include "shell.h"
#include "installer.h"
#include "vga.h"
#include "pit.h"

/* Implemented in sys/shell/shell.c — returns the current working directory
 * so the prompt can reflect it (os~$ at root, os/folder~$ when cd'd in). */
extern const char *sys_shell_get_cwd(void);

#define INPUT_BUF   256
#define HISTORY_LEN 16

static char history[HISTORY_LEN][INPUT_BUF];
static int  history_count = 0;
static int  history_next  = 0;
static int  cursor_visible = 1;
static uint64_t cursor_last_toggle = 0;

/* ── Serial fallback input (for headless QEMU testing) ──────────────────── */
static char serial_getc_nb(void) {
    uint8_t lsr;
    __asm__ volatile("inb %%dx, %0" : "=a"(lsr) : "d"((uint16_t)0x3FD));
    if (lsr & 0x01) {
        uint8_t c;
        __asm__ volatile("inb %%dx, %0" : "=a"(c) : "d"((uint16_t)0x3F8));
        return (char)c;
    }
    return 0;
}

static char shell_getc(void) {
    while (1) {
        char k = keyboard_getc_nb();
        if (k) {
            cursor_visible = 1;
            cursor_last_toggle = pit_get_milliseconds();
            return k;
        }
        char s = serial_getc_nb();
        if (s) {
            cursor_visible = 1;
            cursor_last_toggle = pit_get_milliseconds();
            return s;
        }
        uint64_t now = pit_get_milliseconds();
        if (now - cursor_last_toggle >= 400) {
            cursor_visible = !cursor_visible;
            cursor_last_toggle = now;
        }
        __asm__ volatile("pause");
    }
}

/* Build the prompt string from the current working directory:
 *   "/"          -> "os~$ "
 *   "/folder"    -> "os/folder~$ "  */
static void build_prompt(char *out, size_t outlen) {
    const char *cwd = sys_shell_get_cwd();
    if (!cwd || cwd[0] == '\0' || (cwd[0] == '/' && cwd[1] == '\0')) {
        strncpy(out, "os~$ ", outlen - 1);
        out[outlen - 1] = '\0';
        return;
    }
    size_t n = 0;
    out[n++] = 'o'; out[n++] = 's';
    for (const char *p = cwd; *p && n < outlen - 5; p++) out[n++] = *p;
    out[n++] = '~'; out[n++] = '$'; out[n++] = ' '; out[n] = '\0';
}

/* Redraw the input line in place using only backspace + reprint — no '\r'.
 * `screen_pos` = where the terminal cursor currently is, measured in
 * characters printed since the prompt (from the previous state).
 * Returns the new screen_pos, which is always == cursor after this call. */
static void show_cursor(void) {
    io_put_char('_');
    io_put_char('\b');
}

static int redraw_line(const char *buf, int len, int cursor, int screen_pos) {
    for (int i = 0; i < screen_pos; i++) io_put_char('\b');
    for (int i = 0; i < len; i++) io_put_char(buf[i]);
    io_put_char(' '); /* wipe any leftover char from a longer previous line */
    int back = (len + 1) - cursor;
    for (int i = 0; i < back; i++) io_put_char('\b');
    if (cursor_visible) {
        io_put_char('_');
        io_put_char('\b');
    }
    return cursor;
}

static void shell_loop(const char *banner, int show_prompt_dynamic) {
    char buf[INPUT_BUF];
    int  len        = 0;
    int  cursor     = 0;
    int  screen_pos = 0;  /* cursor's current on-screen offset from prompt */
    int  hist_browse = -1;
    char prompt[280];

    if (show_prompt_dynamic) build_prompt(prompt, sizeof(prompt));
    else strncpy(prompt, "shell~$ ", sizeof(prompt) - 1);

    sys_shell_update_statusbar();
    io_put_string(banner);
    io_put_string(prompt);
    show_cursor();

    while (1) {
        char c = shell_getc();

        /* ── Enter ──────────────────────────────────────────────────── */
        if (c == '\n' || c == '\r') {
            buf[len] = '\0';
            io_put_char('\n');
            if (len > 0) {
                if (history_count == 0 ||
                    strcmp(history[(history_next - 1 + HISTORY_LEN) % HISTORY_LEN], buf) != 0) {
                    strncpy(history[history_next], buf, INPUT_BUF - 1);
                    history[history_next][INPUT_BUF - 1] = '\0';
                    history_next = (history_next + 1) % HISTORY_LEN;
                    if (history_count < HISTORY_LEN) history_count++;
                }
                sys_shell_dispatch(buf);
            }
            len = 0; cursor = 0; screen_pos = 0; hist_browse = -1;
            if (show_prompt_dynamic) build_prompt(prompt, sizeof(prompt));
            sys_shell_update_statusbar();
            io_put_string(prompt);
            show_cursor();
            continue;
        }

        /* ── Backspace ──────────────────────────────────────────────── */
        if (c == '\b' || c == 127) {
            if (cursor > 0) {
                for (int i = cursor - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; cursor--;
                screen_pos = redraw_line(buf, len, cursor, screen_pos);
            }
            continue;
        }

        /* ── Delete key ────────────────────────────────────────────── */
        if ((unsigned char)c == KEY_DEL) {
            if (cursor < len) {
                for (int i = cursor; i < len - 1; i++) buf[i] = buf[i + 1];
                len--;
                screen_pos = redraw_line(buf, len, cursor, screen_pos);
            }
            continue;
        }

        /* ── Left / Right arrow ───────────────────────────────────── */
        if ((unsigned char)c == KEY_LEFT) {
            if (cursor > 0) { cursor--; io_put_char('\b'); screen_pos--; }
            continue;
        }
        if ((unsigned char)c == KEY_RIGHT) {
            if (cursor < len) { io_put_char(buf[cursor]); cursor++; screen_pos++; }
            continue;
        }

        /* ── Home / End ────────────────────────────────────────────── */
        if ((unsigned char)c == KEY_HOME) {
            while (cursor > 0) { cursor--; io_put_char('\b'); screen_pos--; }
            continue;
        }
        if ((unsigned char)c == KEY_END) {
            while (cursor < len) { io_put_char(buf[cursor]); cursor++; screen_pos++; }
            continue;
        }

        /* ── Up / Down — command history ──────────────────────────── */
        if ((unsigned char)c == KEY_UP) {
            if (history_count == 0) continue;
            if (hist_browse == -1) hist_browse = history_count - 1;
            else if (hist_browse > 0) hist_browse--;
            int slot = (history_next - history_count + hist_browse + HISTORY_LEN) % HISTORY_LEN;
            strncpy(buf, history[slot], INPUT_BUF - 1);
            buf[INPUT_BUF - 1] = '\0';
            len = (int)strlen(buf);
            cursor = len;
            screen_pos = redraw_line(buf, len, cursor, screen_pos);
            continue;
        }
        if ((unsigned char)c == KEY_DOWN) {
            if (hist_browse == -1) continue;
            if (hist_browse < history_count - 1) {
                hist_browse++;
                int slot = (history_next - history_count + hist_browse + HISTORY_LEN) % HISTORY_LEN;
                strncpy(buf, history[slot], INPUT_BUF - 1);
                buf[INPUT_BUF - 1] = '\0';
                len = (int)strlen(buf);
            } else {
                hist_browse = -1;
                len = 0; buf[0] = '\0';
            }
            cursor = len;
            screen_pos = redraw_line(buf, len, cursor, screen_pos);
            continue;
        }

        /* ── Ctrl+C ────────────────────────────────────────────────── */
        if (c == 0x03) {
            io_put_string("^C\n");
            len = 0; cursor = 0; screen_pos = 0; hist_browse = -1;
            io_put_string(prompt);
            continue;
        }

        /* ── Ctrl+L — clear screen ─────────────────────────────────── */
        if (c == 0x0C) {
            io_clear_screen();
            io_put_string(prompt);
            for (int i = 0; i < len; i++) io_put_char(buf[i]);
            cursor = len;
            screen_pos = len;
            continue;
        }

        /* Ignore other control characters */
        if ((unsigned char)c < 0x20) continue;

        /* ── Printable character — insert at cursor ───────────────── */
        if (len < INPUT_BUF - 1) {
            for (int i = len; i > cursor; i--) buf[i] = buf[i - 1];
            buf[cursor] = c;
            len++;
            hist_browse = -1;

            if (cursor == len - 1) {
                /* Appending at end — just print it, no full redraw needed */
                io_put_char(c);
                cursor++;
                screen_pos++;
            } else {
                cursor++;
                screen_pos = redraw_line(buf, len, cursor, screen_pos);
            }
        }
    }
}

void kernel_shell(void) {
    shell_loop("\nToriginal OS Shell. Type 'help' for commands.\n", 0);
}

void kernel_os_shell(void) {
    shell_loop(
        "\n\n"
        "  ______           _             _   ___  ____  \n"
        " /_  __/___  _____(_)___ _(_)  / | / /  |/  /  \n"
        "  / / / __ \\/ ___/ / __ `/ /  |/ // /|_/ /   \n"
        " / / / /_/ / /  / / /_/ / / /|  // /  / /      \n"
        "/_/  \\____/_/  /_/\\__, /_/_/ |_//_/  /_/      \n"
        "                  /____/                          \n"
        "\n"
        "  Toriginal OS v1.0  |  freeNT kernel  |  type 'help'\n"
        "\n",
        1
    );
}

void kernel_install_mode(void) {
    installer_run();
}
