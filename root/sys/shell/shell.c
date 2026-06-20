/* =============================================================================
 * shell.c — Toriginal OS kernel shell (C, bare-metal)
 *
 * FIXES:
 *   - Removed #include "shell.h" (that is the C++ class header, wrong file)
 *   - All kernel API calls use explicit forward declarations
 *   - No C++ types or STL anywhere
 *   - Explicit serial_puts/serial_putc forward declarations
 * ========================================================================== */

#include "io.h"
#include "string.h"
#include "process.h"
#include "fs.h"
#include "loader.h"
#include "keybord.h"
#include "types.h"

/* Explicit forward declarations — never rely on implicit prototypes */
void serial_puts(const char *str);
void serial_putc(char c);

/* ── Internal helpers ────────────────────────────────────────────────────── */

static void shell_print(const char *msg) {
    io_put_string(msg);
    serial_puts(msg);
}

static void shell_write_file(const char *path, const char *contents,
                              const char *description) {
    fd_t fd = fs_open(path, O_CREAT | O_WRONLY | O_TRUNC,
                      FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
    if (fd >= 0) {
        fs_write(fd, contents, strlen(contents));
        fs_close(fd);
        shell_print(description);
        shell_print("\n");
    } else {
        shell_print("Failed to write ");
        shell_print(path);
        shell_print("\n");
    }
}

/* ── Interactive OS installer ────────────────────────────────────────────── */
static void do_install(void) {
    char username[64] = {0};
    char password[64] = {0};
    char timezone[64] = {0};
    char accent[32]   = {0};

    shell_print("\nToriginal OS interactive installer\n");

    if (fs_mkdir("/toriginal_os",
                 FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X) == 0)
        shell_print("Created /toriginal_os\n");
    else
        shell_print("/toriginal_os already exists\n");

    fs_mkdir("/toriginal_os/boot",
             FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X);
    fs_mkdir("/toriginal_os/bin",
             FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X);
    fs_mkdir("/toriginal_os/home",
             FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X);

    /* Read username */
    io_put_string("Enter username: ");
    for (int i = 0;;) {
        char c = keyboard_getc();
        if (c == '\r' || c == '\n') break;
        if ((c == '\b' || c == 127) && i > 0) {
            i--; io_put_char('\b'); io_put_char(' '); io_put_char('\b');
        } else if (i + 1 < (int)sizeof(username)) {
            username[i++] = c; io_put_char(c);
        }
    }
    io_put_string("\n");
    if (username[0] == '\0') memcpy(username, "user", 5);

    /* Read password */
    io_put_string("Enter password: ");
    for (int i = 0;;) {
        char c = keyboard_getc();
        if (c == '\r' || c == '\n') break;
        if ((c == '\b' || c == 127) && i > 0) {
            i--; io_put_char('\b'); io_put_char(' '); io_put_char('\b');
        } else if (i + 1 < (int)sizeof(password)) {
            password[i++] = c; io_put_char('*');
        }
    }
    io_put_string("\n");

    /* Read timezone */
    io_put_string("Timezone (e.g. UTC): ");
    for (int i = 0;;) {
        char c = keyboard_getc();
        if (c == '\r' || c == '\n') break;
        if ((c == '\b' || c == 127) && i > 0) {
            i--; io_put_char('\b'); io_put_char(' '); io_put_char('\b');
        } else if (i + 1 < (int)sizeof(timezone)) {
            timezone[i++] = c; io_put_char(c);
        }
    }
    io_put_string("\n");
    if (timezone[0] == '\0') memcpy(timezone, "UTC", 4);

    /* Read accent */
    io_put_string("Accent color (blue/green/red) [blue]: ");
    for (int i = 0;;) {
        char c = keyboard_getc();
        if (c == '\r' || c == '\n') break;
        if ((c == '\b' || c == 127) && i > 0) {
            i--; io_put_char('\b'); io_put_char(' '); io_put_char('\b');
        } else if (i + 1 < (int)sizeof(accent)) {
            accent[i++] = c; io_put_char(c);
        }
    }
    io_put_string("\n");
    if (accent[0] == '\0') memcpy(accent, "blue", 5);

    /* Write config */
    fd_t fd = fs_open("/toriginal_os/config.ini",
                      O_CREAT | O_WRONLY | O_TRUNC,
                      FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
    if (fd >= 0) {
        fs_write(fd, "username=", 9); fs_write(fd, username, strlen(username)); fs_write(fd, "\n", 1);
        fs_write(fd, "password=", 9); fs_write(fd, password, strlen(password)); fs_write(fd, "\n", 1);
        fs_write(fd, "timezone=", 9); fs_write(fd, timezone, strlen(timezone)); fs_write(fd, "\n", 1);
        fs_write(fd, "accent=",   7); fs_write(fd, accent,   strlen(accent));   fs_write(fd, "\n", 1);
        fs_close(fd);
        shell_print("Wrote /toriginal_os/config.ini\n");
    } else {
        shell_print("Failed to write /toriginal_os/config.ini\n");
    }

    shell_write_file("/toriginal_os/installed.flag", "installed\n",
                     "Installation complete: /toriginal_os/installed.flag created");
}

/* ── Status check ────────────────────────────────────────────────────────── */
static void do_status(void) {
    inode_t st;
    if (fs_stat("/toriginal_os/installed.flag", &st) == 0) {
        shell_print("Toriginal OS is installed.\n");
    } else {
        shell_print("Toriginal OS is not installed. Use 'install OS' to install.\n");
    }
}

/* ── Run an executable ───────────────────────────────────────────────────── */
static void do_run(const char *path) {
    shell_print("Running: ");
    shell_print(path);
    shell_print("\n");

    process_t *proc = process_create(path, 1);
    if (!proc) { shell_print("Execution failed: could not create process.\n"); return; }

    pid_t pid = proc->pid;
    if (loader_load_executable(path, pid) == 0) {
        process_start(pid);
    } else {
        shell_print("Execution failed: loader error.\n");
    }
}

/* ── Command dispatcher ──────────────────────────────────────────────────── */
static void handle_command(char *cmd, int is_os_mode) {
    if (!cmd) return;

    /* Strip leading "shell " prefix */
    if (strncmp(cmd, "shell ", 6) == 0) cmd += 6;

    size_t len = strlen(cmd);

    /* Direct executable run by extension */
    if (len > 4) {
        const char *ext = cmd + len - 4;
        if (strcmp(ext, ".exe") == 0 || strcmp(ext, ".trp") == 0 ||
            strcmp(ext, ".elf") == 0) {
            do_run(cmd);
            return;
        }
    }

    if (strcmp(cmd, "help") == 0) {
        io_put_string("Commands: help, echo <text>, install OS, status,\n");
        io_put_string("          run <path>, halt, reboot");
        if (is_os_mode) io_put_string(", gui");
        io_put_string("\n");

    } else if (strncmp(cmd, "echo ", 5) == 0) {
        io_put_string(cmd + 5);
        io_put_string("\n");

    } else if (strcmp(cmd, "install OS") == 0 ||
               strcmp(cmd, "install os") == 0) {
        do_install();

    } else if (strcmp(cmd, "status") == 0) {
        do_status();

    } else if (strncmp(cmd, "run ", 4) == 0) {
        do_run(cmd + 4);

    } else if (is_os_mode && strcmp(cmd, "gui") == 0) {
        shell_print("GUI mode not yet implemented.\n");

    } else if (strcmp(cmd, "halt") == 0) {
        shell_print("System halted.\n");
        for (;;) __asm__ volatile("cli; hlt");

    } else if (strcmp(cmd, "reboot") == 0) {
        shell_print("Rebooting...\n");
        __asm__ volatile("cli");
        /* Pulse PS/2 controller reset line */
        __asm__ volatile(
            "movb $0xFE, %%al\n"
            "outb %%al, $0x64\n"
            ::: "al"
        );
        for (;;) __asm__ volatile("hlt");

    } else {
        io_put_string("Unknown command: '");
        io_put_string(cmd);
        io_put_string("'  (type 'help')\n");
    }
}

/* ── Main shell loop ─────────────────────────────────────────────────────── */
static void shell_loop(const char *banner, const char *prompt, int is_os_mode) {
    char buf[256];
    int  idx = 0;

    shell_print(banner);
    io_put_string(prompt);
    serial_puts(prompt);

    for (;;) {
        char c = keyboard_getc();

        /* Serial echo for debugging */
        if (c == '\n' || c == '\r') serial_putc('\n');
        else if (c >= 0x20)         serial_putc(c);

        if (c == '\n' || c == '\r') {
            buf[idx] = '\0';
            io_put_char('\n');
            if (idx > 0) handle_command(buf, is_os_mode);
            idx = 0;
            io_put_string(prompt);
            serial_puts(prompt);
            continue;
        }

        if (c == '\b' || c == 127) {
            if (idx > 0) {
                idx--;
                io_put_char('\b'); io_put_char(' '); io_put_char('\b');
                serial_puts("\b \b");
            }
            continue;
        }

        if (c == 0x03) {  /* Ctrl+C */
            io_put_string("^C\n"); serial_puts("^C\n");
            idx = 0;
            io_put_string(prompt); serial_puts(prompt);
            continue;
        }

        if (c == 0x0C) {  /* Ctrl+L — clear */
            io_clear_screen();
            io_put_string(prompt);
            for (int i = 0; i < idx; i++) io_put_char(buf[i]);
            continue;
        }

        if (c < 0x20) continue;

        if (idx < (int)(sizeof(buf) - 1)) {
            buf[idx++] = c;
            io_put_char(c);
        }
    }
}

/* ── Public entry points ─────────────────────────────────────────────────── */
void kernel_shell(void) {
    shell_loop("\nToriginal OS Shell. Type 'help' for commands.\n",
               "shell~$ ", 0);
}

void kernel_os_shell(void) {
    shell_loop("\nToriginal OS. Type 'help' for commands.\n",
               "os~$ ", 1);
}

void kernel_install_mode(void) {
    do_install();
}
