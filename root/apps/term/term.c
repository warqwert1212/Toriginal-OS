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
