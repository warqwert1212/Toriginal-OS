#include "io.h"
#include "string.h"
#include "keyboard.h"
#include "serial.h"
#include "shell.h"
#include "installer.h"
#include "vga.h"
#include "pit.h"
#include "gfx_terminal.h"
#include "usb_hid.h"

extern const char *sys_shell_get_cwd(void);

#define INPUT_BUF   256
#define HISTORY_LEN 16

static char history[HISTORY_LEN][INPUT_BUF];
static int  history_count = 0;
static int  history_next  = 0;
static int  cursor_visible = 1;
static uint64_t cursor_last_toggle = 0;

#define STATUSBAR_REFRESH_MS 1000
static uint64_t statusbar_last_update = 0;

static char serial_getc_nb(void) {
    uint8_t lsr;
    __asm__ volatile("inb %%dx, %0" : "=a"(lsr) : "d"((uint16_t)0x3FD));
    if (lsr & 0x01) {
        uint8_t c;
        __asm__ volatile("inb %%dx, %0" : "=a"(c) : "d"((uint16_t)0x3F8));
        if (c == '\n' || c == '\r' || c == '\b' || c == 127 || c == 0x03 || c == 0x0C) return (char)c;
        if (c >= 0x20 && c < 0x7F) return (char)c;
        return 0;
    }
    return 0;
}

static char shell_getc(void) {
    while (1) {

        uint64_t sb_now = pit_get_milliseconds();
        if (sb_now - statusbar_last_update >= STATUSBAR_REFRESH_MS) {
            sys_shell_update_statusbar();
            statusbar_last_update = sb_now;
        }

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
        usb_hid_poll();
        char u = usb_hid_getc_nb();
        if (u) {
            cursor_visible = 1;
            cursor_last_toggle = pit_get_milliseconds();
            return u;
        }
        uint64_t now = pit_get_milliseconds();
        if (now - cursor_last_toggle >= 400) {
            cursor_visible = !cursor_visible;
            cursor_last_toggle = now;
        }

        gterm_poll_tick();

        __asm__ volatile("pause");
    }
}

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

static void show_cursor(void) {
    io_put_char('_');
    io_put_char('\b');
}

static int redraw_line(const char *buf, int len, int cursor, int screen_pos) {
    for (int i = 0; i < screen_pos; i++) io_put_char('\b');
    for (int i = 0; i < len; i++) io_put_char(buf[i]);
    io_put_char(' ');
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
    int  screen_pos = 0;
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

        if (c == '\b' || c == 127) {
            if (cursor > 0) {
                for (int i = cursor - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; cursor--;
                screen_pos = redraw_line(buf, len, cursor, screen_pos);
            }
            continue;
        }

        if ((unsigned char)c == KEY_DEL) {
            if (cursor < len) {
                for (int i = cursor; i < len - 1; i++) buf[i] = buf[i + 1];
                len--;
                screen_pos = redraw_line(buf, len, cursor, screen_pos);
            }
            continue;
        }

        if ((unsigned char)c == KEY_LEFT) {
            if (cursor > 0) { cursor--; io_put_char('\b'); screen_pos--; }
            continue;
        }
        if ((unsigned char)c == KEY_RIGHT) {
            if (cursor < len) { io_put_char(buf[cursor]); cursor++; screen_pos++; }
            continue;
        }

        if ((unsigned char)c == KEY_HOME) {
            while (cursor > 0) { cursor--; io_put_char('\b'); screen_pos--; }
            continue;
        }
        if ((unsigned char)c == KEY_END) {
            while (cursor < len) { io_put_char(buf[cursor]); cursor++; screen_pos++; }
            continue;
        }

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

        if (c == 0x03) {
            io_put_string("^C\n");
            len = 0; cursor = 0; screen_pos = 0; hist_browse = -1;
            io_put_string(prompt);
            continue;
        }

        if (c == 0x0C) {
            io_clear_screen();
            io_put_string(prompt);
            for (int i = 0; i < len; i++) io_put_char(buf[i]);
            cursor = len;
            screen_pos = len;
            continue;
        }

        if ((unsigned char)c < 0x20) continue;

        if (len < INPUT_BUF - 1) {
            for (int i = len; i > cursor; i--) buf[i] = buf[i - 1];
            buf[cursor] = c;
            len++;
            hist_browse = -1;

            if (cursor == len - 1) {

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
        "  ______              _       _                                           \n"
        " /_  __/___  _____   (_)___ _(_) __ ____  __       ____   _____             \n"
        "  / / / __ \\/ ___/ / / __ `/ / / // __ \\/ /      / __ \\/ ___/            \n"
        " / / / /_/ / /     / /_/ / / /|/ // /_/ / /__    / /_/ / \\__ \\            \n"
        "/_/  \\___/_/      \\_, /_/_/ |_/ \\___/_/__/   \\____/ /____/           \n"
        "                  /____/                                                   \n"
        "\n"
        "Toriginal OS v1.2 |  freeNT kernel 1.5  |  type 'help' for a list of commands\n"
        "\n",
        1
    );
}
//why the fuck the ascii art look fine here,but in the os it looks so fucked up! i need to fix it, maybe the font is not monospaced or 
// the terminal width is not enough.
void kernel_install_mode(void) {
    installer_run();
}