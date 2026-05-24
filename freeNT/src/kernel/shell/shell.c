#include "shell.h"
#include "io.h"
#include "string.h"
#include "process.h"
#include "fs.h"

/* Very small kernel shell over serial. Blocks and echoes input. */
void kernel_shell(void) {
    char buf[256];
    int idx = 0;

    serial_puts("\nKernel shell (serial). Type 'help' for commands.\n");
    serial_puts("ksh> ");

    while (1) {
        char c = serial_getc();

        if (c == '\r' || c == '\n') {
            buf[idx] = '\0';
            serial_puts("\n");

            if (idx == 0) {
                serial_puts("ksh> ");
                continue;
            }

            if (strcmp(buf, "help") == 0) {
                serial_puts("Built-ins: help, echo, halt, reboot\n");
                    serial_puts("  run <path>  - execute file (TRP/EXE/TXT)\n");
            } else if (strncmp(buf, "echo ", 5) == 0) {
                serial_puts(buf + 5);
                serial_puts("\n");
                } else if (strncmp(buf, "run ", 4) == 0) {
                    const char *path = buf + 4;
                    serial_puts("Running: "); serial_puts(path); serial_puts("\n");

                    process_t *p = process_create(path, 1);
                    if (!p) {
                        serial_puts("Failed to create process\n");
                    } else {
                        if (process_exec(p->pid, path, NULL) != 0) {
                            serial_puts("Exec failed\n");
                        } else {
                            /* Start the loaded process (will jump to entry) */
                            process_start(p->pid);
                            /* If it returns, continue shell */
                            serial_puts("Process returned to shell\n");
                        }
                    }
            } else if (strcmp(buf, "halt") == 0) {
                serial_puts("System halted.\n");
                while (1) {
                    asm volatile("hlt");
                }
            } else if (strcmp(buf, "reboot") == 0) {
                serial_puts("Reboot requested (halting).\n");
                asm volatile("cli; hlt");
                while (1) asm volatile("hlt");
            } else {
                serial_puts("Unknown command. Type 'help'.\n");
            }

            idx = 0;
            serial_puts("ksh> ");
        } else if (c == '\b' || c == 0x7f) {
            if (idx > 0) {
                idx--;
                serial_puts("\b \b");
            }
        } else {
            if (idx < (int)(sizeof(buf) - 1)) {
                buf[idx++] = c;
                serial_putc(c);
            }
        }
    }
}
