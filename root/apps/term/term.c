/* term.c - Toriginal OS terminal app.
 *
 * This is a genuine user-space .trp program: it is built and linked
 * separately from the kernel (see app.ld's ENTRY(main_explorer_executable)
 * and ../include/trsys.h's header comment on why this is the first
 * real example of that), and talks to the kernel exclusively through
 * the `syscall` instruction via trsys.h - no kernel headers, no
 * kernel data structures, no `extern` reach into kernel globals.
 *
 * What it actually does right now: a line-editing REPL that reads
 * from stdin (fd 0 - newly wired to the keyboard driver, see
 * syscall.c's sys_read() comment) and echoes/dispatches simple
 * built-in commands by writing to stdout (fd 1). It does NOT yet run
 * arbitrary programs (that needs sys_exec() wired to actually load
 * and run another .trp while this one waits - process_exec() exists
 * in process.c but nothing in this app calls it yet) - this is
 * intentionally scoped to "a real, working terminal input/output
 * loop using the real syscall surface" as the foundation piece, not
 * a claim that command execution is done. Built-ins (pwd, echo,
 * clear, exit) are enough to prove the syscall path end-to-end
 * (getcwd, write, ioctl) without overclaiming a full shell.
 */
#include "../include/trsys.h"

#define LINE_MAX 256

static uint64_t strlen_(const char *s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

static int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void term_write(const char *s) {
    sys_write(1, s, strlen_(s));
}

/* Blocking line read built on the non-blocking stdin syscall (see
 * syscall.c's sys_read() comment on why fd 0 is non-blocking at the
 * syscall level) - yields between empty reads rather than busy-
 * spinning the CPU pegged at 100% while waiting for a keypress,
 * which matters a lot more on real hardware than it would look like
 * in a quick test (a busy-wait here would starve every other process
 * of CPU time on a cooperative-yield-friendly but still single-core
 * scheduler). Returns line length (without the trailing newline). */
static int term_getline(char *buf, int max_len) {
    int len = 0;
    for (;;) {
        char c;
        int64_t n = sys_read(0, &c, 1);
        if (n <= 0) {
            sys_yield();
            continue;
        }

        if (c == '\n' || c == '\r') {
            term_write("\n");
            buf[len] = '\0';
            return len;
        }

        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                term_write("\b \b"); /* erase the character on-screen too */
            }
            continue;
        }

        if (len < max_len - 1) {
            buf[len++] = c;
            char echo[2] = { c, '\0' };
            term_write(echo);
        }
    }
}

static void cmd_pwd(void) {
    char cwd[256];
    int64_t r = trsys_call2(SYS_GETCWD, (int64_t)(uintptr_t)cwd, sizeof(cwd));
    if (r < 0) {
        term_write("pwd: error\n");
        return;
    }
    term_write(cwd);
    term_write("\n");
}

static void cmd_echo(const char *args) {
    term_write(args);
    term_write("\n");
}

static void cmd_clear(void) {
    /* ANSI clear+home - the gterm/vga text backend this eventually
     * feeds into doesn't parse ANSI escapes today (see gfx_terminal.c
     * - it's a plain cell grid, no escape-sequence state machine), so
     * for now this is a readable no-op on-screen rather than a
     * guaranteed-working clear; kept as a real command (not silently
     * dropped) so scripts/muscle-memory using it don't hard-fail,
     * and so the moment ANSI parsing lands in gterm this starts
     * working with zero changes needed here. */
    term_write("\x1b[2J\x1b[H");
}

int main_explorer_executable(void) {
    term_write("Toriginal OS terminal - type 'help' for commands, 'exit' to quit\n");

    char line[LINE_MAX];
    for (;;) {
        term_write("$ ");
        int len = term_getline(line, LINE_MAX);
        if (len == 0) continue;

        if (streq(line, "exit")) {
            break;
        } else if (streq(line, "help")) {
            term_write("built-ins: pwd, echo <text>, clear, exit\n");
        } else if (streq(line, "pwd")) {
            cmd_pwd();
        } else if (len > 5 && line[0]=='e' && line[1]=='c' && line[2]=='h' && line[3]=='o' && line[4]==' ') {
            cmd_echo(line + 5);
        } else if (streq(line, "clear")) {
            cmd_clear();
        } else {
            term_write(line);
            term_write(": command not found\n");
        }
    }

    sys_exit(0);
    return 0;
}
