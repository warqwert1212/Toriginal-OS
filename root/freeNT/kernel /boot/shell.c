#include "shell.h"
#include "io.h"
#include "string.h"
#include "process.h"
#include "fs.h"
#include "loader.h"
#include "keyboard.h"      // <<< ADDED: keyboard driver

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void kernel_install_log(const char *message) {
    io_put_string(message);
    serial_puts(message);
}

static void kernel_install_write_file(const char *path, const char *contents, const char *description) {
    fd_t fd = fs_open(path, O_CREAT | O_WRONLY | O_TRUNC, FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
    if (fd >= 0) {
        fs_write(fd, contents, strlen(contents));
        fs_close(fd);
        kernel_install_log(description);
        kernel_install_log("\n");
    } else {
        kernel_install_log("Failed to write ");
        kernel_install_log(path);
        kernel_install_log("\n");
    }
}

static void kernel_install_toriginal_os(void) {
    kernel_install_log("\nToriginal OS installer starting...\n");
    kernel_install_log("Writing OS image to internal install tree...\n");

    if (fs_mkdir("/toriginal_os", FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X) == 0)
        kernel_install_log("Created /toriginal_os\n");
    else
        kernel_install_log("/toriginal_os already exists\n");

    if (fs_mkdir("/toriginal_os/boot", FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X) == 0)
        kernel_install_log("Created /toriginal_os/boot\n");
    else
        kernel_install_log("/toriginal_os/boot already exists\n");

    kernel_install_write_file("/toriginal_os/README.txt",
        "Welcome to Toriginal OS!\nThis installation is stored in the kernel's internal filesystem.\n",
        "Wrote /toriginal_os/README.txt");

    kernel_install_write_file("/toriginal_os/version.txt",
        "Toriginal OS version 1.0\n",
        "Wrote /toriginal_os/version.txt");

    kernel_install_write_file("/toriginal_os/boot/freeNT",
        "Toriginal OS kernel placeholder\n",
        "Installed kernel placeholder at /toriginal_os/boot/freeNT");

    kernel_install_write_file("/toriginal_os/boot/grub.cfg",
        "set timeout=5\nset default=0\n",
        "Wrote boot config at /toriginal_os/boot/grub.cfg");

    kernel_install_write_file("/toriginal_os/installed.flag",
        "installed\n",
        "Installation complete: /toriginal_os/installed.flag created");

    kernel_install_log("Toriginal OS has been installed.\n");
}

static void kernel_show_install_status(void) {
    inode_t st;
    if (fs_stat("/toriginal_os/installed.flag", &st) == 0) {
        io_put_string("Toriginal OS is installed.\n");
        serial_puts("Toriginal OS is installed.\n");
    } else {
        io_put_string("Toriginal OS is not installed yet. Use 'install OS' to install.\n");
        serial_puts("Not installed yet.\n");
    }
}

static void kernel_run_executable(const char *path) {
    io_put_string("Running: ");
    io_put_string(path);
    io_put_string("\n");
    serial_puts("Running: ");
    serial_puts(path);
    serial_puts("\n");

    process_t *new_proc = process_create(path, 1);
    if (!new_proc) {
        io_put_string("Execution failed: Could not create process.\n");
        return;
    }

    pid_t new_pid = new_proc->pid;

    if (loader_load_executable(path, new_pid) == 0) {
        process_start(new_pid);
    } else {
        io_put_string("Execution failed: Format handler error.\n");
    }
}

static void kernel_display_gui_placeholder(void) {
    io_put_string("\n=== Toriginal OS GUI Placeholder ===\n");
    io_put_string("GUI mode coming in version 2.0.\n");
    io_put_string("Type 'help' for available shell commands.\n");
    serial_puts("[gui] placeholder displayed\n");
}

// ---------------------------------------------------------------------------
// Command handler
// ---------------------------------------------------------------------------

static void kernel_handle_command(char *command, int is_os_mode) {
    if (!command)
        return;

    // Strip leading "shell " prefix if present
    if (strncmp(command, "shell ", 6) == 0)
        command += 6;

    size_t cmd_len = strlen(command);

    // Check for executable extensions — run directly
    if (cmd_len > 4 &&
       (strcmp(command + cmd_len - 4, ".exe") == 0 ||
        strcmp(command + cmd_len - 4, ".trp") == 0 ||
        strcmp(command + cmd_len - 4, ".txt") == 0)) {
        kernel_run_executable(command);
        return;
    }

    if (strcmp(command, "help") == 0) {
        io_put_string("Commands: help, echo <text>, install OS, status, run <path>,\n");
        io_put_string("          halt, reboot");
        if (is_os_mode) io_put_string(", gui");
        io_put_string("\n");

    } else if (strncmp(command, "echo ", 5) == 0) {
        io_put_string(command + 5);
        io_put_string("\n");

    } else if (strcmp(command, "install OS") == 0 ||
               strcmp(command, "install os") == 0) {
        kernel_install_toriginal_os();

    } else if (strcmp(command, "status") == 0) {
        kernel_show_install_status();

    } else if (strncmp(command, "run ", 4) == 0) {
        kernel_run_executable(command + 4);

    } else if (is_os_mode && strcmp(command, "gui") == 0) {
        kernel_display_gui_placeholder();

    } else if (strcmp(command, "halt") == 0) {
        io_put_string("System halted.\n");
        serial_puts("System halted.\n");
        while (1) { asm volatile("hlt"); }

    } else if (strcmp(command, "reboot") == 0) {
        io_put_string("Rebooting...\n");
        serial_puts("Reboot requested.\n");
        // Pulse the keyboard controller reset line (works on real hardware + VMs)
        asm volatile("cli");
        // Wait for input buffer empty
        for (volatile int i = 0; i < 100000; i++);
        // Pulse reset via PS/2 controller output port
        __asm__ volatile(
            "mov $0xFE, %%al\n"
            "out %%al, $0x64\n"
            ::: "al"
        );
        // If that didn't work, triple fault
        while (1) { asm volatile("hlt"); }

    } else {
        io_put_string("Unknown command: '");
        io_put_string(command);
        io_put_string("'  (type 'help')\n");
    }
}

// ---------------------------------------------------------------------------
// Shell VGA echo helper — prints a char to screen
// ---------------------------------------------------------------------------

static void shell_echo_char(char c) {
    if (c == '\b' || c == 127) {
        // Print backspace-space-backspace to erase character on VGA
        io_put_char('\b');
        io_put_char(' ');
        io_put_char('\b');
    } else {
        io_put_char(c);
    }
}

// ---------------------------------------------------------------------------
// Main shell loop
// Uses keyboard_getc() for physical keyboard input
// Serial output still works in parallel for QEMU debugging
// ---------------------------------------------------------------------------

static void kernel_shell_loop(const char *banner, const char *prompt, int is_os_mode) {
    char buf[256];
    int idx = 0;

    // Print banner to both VGA and serial
    io_put_string(banner);
    serial_puts(banner);

    io_put_string(prompt);
    serial_puts(prompt);

    while (1) {
        // <<< CHANGED: was serial_getc(), now keyboard_getc()
        // keyboard_getc() blocks with HLT until a key is pressed.
        // It reads from the PS/2 keyboard on real hardware and all VMs.
        char c = keyboard_getc();

        // Also echo to serial for debugging in QEMU
        if (c == '\n' || c == '\r') {
            serial_putc('\n');
        } else if (c >= 0x20) {
            serial_putc(c);
        }

        // --- Enter: execute command ---
        if (c == '\n' || c == '\r') {
            buf[idx] = '\0';
            io_put_char('\n');

            if (idx > 0) {
                kernel_handle_command(buf, is_os_mode);
            }

            idx = 0;
            io_put_string(prompt);
            serial_puts(prompt);
            continue;
        }

        // --- Backspace ---
        if (c == '\b' || c == 127) {
            if (idx > 0) {
                idx--;
                shell_echo_char('\b');
                serial_puts("\b \b");
            }
            continue;
        }

        // --- Ctrl+C: cancel current line ---
        if (c == 0x03) {
            io_put_string("^C\n");
            serial_puts("^C\n");
            idx = 0;
            io_put_string(prompt);
            serial_puts(prompt);
            continue;
        }

        // --- Ctrl+L: clear screen ---
        if (c == 0x0C) {
            io_clear_screen();
            io_put_string(prompt);
            // Redraw whatever was typed so far
            for (int i = 0; i < idx; i++)
                io_put_char(buf[i]);
            continue;
        }

        // --- Ignore other control characters ---
        if (c < 0x20)
            continue;

        // --- Normal character ---
        if (idx < (int)(sizeof(buf) - 1)) {
            buf[idx++] = c;
            shell_echo_char(c);
        }
        // Buffer full — silently drop
    }
}

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

void kernel_shell(void) {
    kernel_shell_loop(
        "\nToriginal OS Shell. Type 'help' for commands.\n",
        "shell~$ ",
        0
    );
}

void kernel_os_shell(void) {
    kernel_shell_loop(
        "\nToriginal OS. Type 'help' for commands.\n",
        "os~$ ",
        1
    );
}

void kernel_install_mode(void) {
    kernel_install_toriginal_os();
}