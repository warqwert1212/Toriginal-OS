/* =============================================================================
 * kernel/shell.c — Raw input loop only. All commands in sys/shell/shell.c.
 * ============================================================================= */

#include "io.h"
#include "string.h"
#include "keyboard.h"
#include "serial.h"
#include "shell.h"
#include "installer.h"

/* ── Serial + keyboard input ─────────────────────────────────────────────── */
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
        if (k) return k;
        char s = serial_getc_nb();
        if (s) return s;
        __asm__ volatile("pause");
    }
}

#define INPUT_BUF 256

static void shell_loop(const char *banner, const char *prompt) {
    char buf[INPUT_BUF];
    int  idx = 0;

    /* Banner and prompt go to VGA+serial once only via io_put_string */
    io_put_string(banner);
    io_put_string(prompt);

    while (1) {
        char c = shell_getc();

        if (c == '\n' || c == '\r') {
            buf[idx] = '\0';
            io_put_char('\n');
            if (idx > 0) {
                sys_shell_dispatch(buf);
            }
            idx = 0;
            io_put_string(prompt);
            continue;
        }

        if (c == '\b' || c == 127) {
            if (idx > 0) {
                idx--;
                io_put_char('\b');
                io_put_char(' ');
                io_put_char('\b');
            }
            continue;
        }

        if (c == 0x03) {  /* Ctrl+C */
            io_put_string("^C\n");
            idx = 0;
            io_put_string(prompt);
            continue;
        }

        if (c == 0x0C) {  /* Ctrl+L */
            io_clear_screen();
            io_put_string(prompt);
            for (int i = 0; i < idx; i++) io_put_char(buf[i]);
            continue;
        }

        if (c < 0x20) continue;

        if (idx < INPUT_BUF - 1) {
            buf[idx++] = c;
            io_put_char(c);
        }
    }
}

void kernel_shell(void) {
    shell_loop(
        "\nToriginal OS Shell. Type 'help' for commands.\n",
        "shell~$ "
    );
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
        "os~$ "
    );
}

void kernel_install_mode(void) {
    installer_run();
}
