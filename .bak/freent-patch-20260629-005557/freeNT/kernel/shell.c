/* =============================================================================
 * kernel/shell.c — Raw input loop and line assembly
 *
 * This file owns ONLY the mechanical layer of the shell:
 *   - Reading characters from the PS/2 keyboard via keyboard_getc()
 *   - Echoing to VGA + serial
 *   - Assembling characters into a NUL-terminated line buffer
 *   - Handling backspace, Ctrl+C, Ctrl+L
 *   - Calling sys_shell_dispatch(line) (implemented in sys/shell/shell.c)
 *     when Enter is pressed
 *
 * It does NOT implement any commands. All commands live in sys/shell/shell.c.
 * ========================================================================= */

#include "io.h"
#include "string.h"
#include "keyboard.h"
#include "serial.h"
#include "shell.h"
#include "installer.h"

/* ── Serial + keyboard input ─────────────────────────────────────────────
 * serial_getc_nb(): non-blocking read from COM1.
 *   Returns a char if one is waiting, 0 if nothing ready.
 *   Allows QEMU -serial stdio to drive the shell without a display.
 *
 * shell_getc(): polls PS/2 keyboard first, then serial, spins until
 *   something arrives. This replaces the old blocking keyboard_getc()
 *   call so both input paths work. */
static char serial_getc_nb(void) {
    uint8_t lsr;
    /* COM1 Line Status Register (port 0x3FD): bit 0 = data ready.
     * Use dx for the port number to avoid assembler truncation warnings. */
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
        char k = keyboard_getc_nb();   /* PS/2 — non-blocking */
        if (k) return k;
        char s = serial_getc_nb();     /* serial (QEMU stdio) — non-blocking */
        if (s) return s;
        __asm__ volatile("pause");     /* avoid 100% CPU spin */
    }
}

#define INPUT_BUF 256

static void shell_loop(const char *banner, const char *prompt) {
    char buf[INPUT_BUF];
    int  idx = 0;

    io_put_string(banner);
    serial_puts(banner);
    io_put_string(prompt);
    serial_puts(prompt);

    while (1) {
        char c = shell_getc();

        /* Mirror printable chars and newlines to serial for debugging. */
        if (c == '\n' || c == '\r') {
            serial_putc('\n');
        } else if (c >= 0x20) {
            serial_putc(c);
        }

        /* Enter — dispatch the line. */
        if (c == '\n' || c == '\r') {
            buf[idx] = '\0';
            io_put_char('\n');
            if (idx > 0) {
                sys_shell_dispatch(buf);
            }
            idx = 0;
            io_put_string(prompt);
            serial_puts(prompt);
            continue;
        }

        /* Backspace / DEL */
        if (c == '\b' || c == 127) {
            if (idx > 0) {
                idx--;
                io_put_char('\b');
                io_put_char(' ');
                io_put_char('\b');
                serial_puts("\b \b");
            }
            continue;
        }

        /* Ctrl+C — cancel current line */
        if (c == 0x03) {
            io_put_string("^C\n");
            serial_puts("^C\n");
            idx = 0;
            io_put_string(prompt);
            serial_puts(prompt);
            continue;
        }

        /* Ctrl+L — clear screen */
        if (c == 0x0C) {
            io_clear_screen();
            io_put_string(prompt);
            for (int i = 0; i < idx; i++) io_put_char(buf[i]);
            continue;
        }

        /* Ignore other control characters */
        if (c < 0x20) continue;

        /* Normal character */
        if (idx < INPUT_BUF - 1) {
            buf[idx++] = c;
            io_put_char(c);
        }
        /* Buffer full — silently drop */
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
        "  ______           _             _             _    ___  ____  \n"
        " /_  __/___  _____(_)___ _(_)___  ____ _| |  / _ \\/ ___| \n"
        "  / / / __ \\/ ___/ / __ `/ / __ \\/ __ `/ / |  / / / /___ \\ \n"
        " / / / /_/ / /  / / /_/ / / / / / /_/ / /| | / /  \\___/ /  \n"
        "/_/  \\____/_/  /_/\\__, /_/_/ /_/\\__,_/_/ |_|/_/  /____/   \n"
        "                  /____/                                       \n"
        "\n"
        "  freeNT 1.0 kernel  |  Type 'help' for commands\n"
        "\n",
        "os~$ "
    );
}

void kernel_install_mode(void) {
    installer_run();
}
