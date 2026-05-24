#include "shell.h"
#include "io.h"
#include "string.h"
#include "process.h"
#include "fs.h"

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

    if (fs_mkdir("/toriginal_os", FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X) == 0) {
        kernel_install_log("Created /toriginal_os\n");
    } else {
        kernel_install_log("/toriginal_os already exists or could not be created\n");
    }

    if (fs_mkdir("/toriginal_os/boot", FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X) == 0) {
        kernel_install_log("Created /toriginal_os/boot\n");
    } else {
        kernel_install_log("/toriginal_os/boot already exists or could not be created\n");
    }

    kernel_install_write_file("/toriginal_os/README.txt",
        "Welcome to Toriginal OS!\nThis installation is stored in the kernel's internal filesystem.\nUse 'status' to confirm install state.\n",
        "Wrote /toriginal_os/README.txt");

    kernel_install_write_file("/toriginal_os/version.txt",
        "Toriginal OS installed version 1.0\n",
        "Wrote /toriginal_os/version.txt");

    kernel_install_write_file("/toriginal_os/boot/freeNT",
        "Toriginal OS installed kernel placeholder\n",
        "Installed kernel placeholder at /toriginal_os/boot/freeNT");

    kernel_install_write_file("/toriginal_os/boot/grub.cfg",
        "set timeout=5\nset default=0\n\n# Toriginal OS installed boot config placeholder\n",
        "Wrote boot config placeholder at /toriginal_os/boot/grub.cfg");

    kernel_install_write_file("/toriginal_os/installed.flag",
        "installed\n",
        "Installation complete: /toriginal_os/installed.flag created");

    kernel_install_log("Toriginal OS has been installed into the internal HDD/SSD target.\n");
}

static void kernel_show_install_status(void) {
    inode_t st;
    if (fs_stat("/toriginal_os/installed.flag", &st) == 0) {
        io_put_string("Toriginal OS is installed.\n");
        io_put_string("Installed OS root: /toriginal_os\n");
        serial_puts("Toriginal OS is installed.\n");
        serial_puts("Installed OS root: /toriginal_os\n");
    } else {
        io_put_string("Toriginal OS is not installed yet. Use 'install OS' to install.\n");
        serial_puts("Toriginal OS is not installed yet. Use 'install OS' to install.\n");
    }
}

static void kernel_run_executable(const char *path) {
    io_put_string("Running: ");
    io_put_string(path);
    io_put_string("\n");
    serial_puts("Running: ");
    serial_puts(path);
    serial_puts("\n");

    process_t *p = process_create(path, 1);
    if (!p) {
        serial_puts("Failed to create process\n");
        return;
    }

    if (process_exec(p->pid, path, NULL) != 0) {
        serial_puts("Exec failed\n");
        return;
    }

    process_start(p->pid);
    serial_puts("Process returned to shell\n");
}

static void kernel_display_gui_placeholder(void) {
    io_put_string("\n=== Toriginal OS GUI Placeholder ===\n");
    io_put_string("Welcome to Toriginal OS GUI mode.\n");
    if (fs_stat("/toriginal_os/installed.flag", NULL) == 0) {
        io_put_string("Installed OS detected: /toriginal_os\n");
    } else {
        io_put_string("No installed OS detected yet. Use 'install OS' in shell mode.\n");
    }
    io_put_string("Type 'status' or 'help' in the shell for available commands.\n");
    serial_puts("[gui] Toriginal OS GUI placeholder displayed\n");
}

static void kernel_handle_command(char *command, int is_os_mode) {
    if (!command)
        return;

    if (strncmp(command, "shell ", 6) == 0) {
        command += 6;
    }

    if (strcmp(command, "help") == 0) {
        serial_puts("Built-ins: help, echo, install OS, status, run <path>, halt, reboot\n");
        if (is_os_mode) {
            serial_puts("  gui        - display GUI placeholder\n");
        }
    } else if (strncmp(command, "echo ", 5) == 0) {
        serial_puts(command + 5);
        serial_puts("\n");
    } else if (strcmp(command, "install OS") == 0 || strcmp(command, "install os") == 0) {
        kernel_install_toriginal_os();
    } else if (strcmp(command, "status") == 0) {
        kernel_show_install_status();
    } else if (strncmp(command, "run ", 4) == 0) {
        kernel_run_executable(command + 4);
    } else if (is_os_mode && strcmp(command, "gui") == 0) {
        kernel_display_gui_placeholder();
    } else if (strcmp(command, "halt") == 0) {
        serial_puts("System halted.\n");
        while (1) {
            asm volatile("hlt");
        }
    } else if (strcmp(command, "reboot") == 0) {
        serial_puts("Reboot requested (halting).\n");
        asm volatile("cli; hlt");
        while (1) asm volatile("hlt");
    } else {
        serial_puts("Unknown command. Type 'help'.\n");
    }
}

static void kernel_shell_loop(const char *banner, const char *prompt, int is_os_mode) {
    char buf[256];
    int idx = 0;

    serial_puts(banner);
    serial_puts(prompt);

    while (1) {
        char c = serial_getc();

        if (c == '\r' || c == '\n') {
            buf[idx] = '\0';
            serial_puts("\n");

            if (idx != 0) {
                kernel_handle_command(buf, is_os_mode);
            }

            idx = 0;
            serial_puts(prompt);
            continue;
        }

        if (c == '\b' || c == 0x7f) {
            if (idx > 0) {
                idx--;
                serial_puts("\b \b");
            }
            continue;
        }

        if (idx < (int)(sizeof(buf) - 1)) {
            buf[idx++] = c;
            serial_putc(c);
        }
    }
}

void kernel_shell(void) {
    kernel_shell_loop("\nToriginal OS Shell (serial). Type 'help' for commands.\n", "shell~$ ", 0);
}

void kernel_os_shell(void) {
    kernel_shell_loop("\nToriginal OS Shell (GUI mode). Type 'help' for commands.\n", "os~$ ", 1);
}

void kernel_install_mode(void) {
    kernel_install_toriginal_os();
}
