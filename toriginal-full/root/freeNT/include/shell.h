/* Simple in-kernel shell API */
#ifndef _KERNEL_SHELL_H
#define _KERNEL_SHELL_H

/* kernel/shell.c — input loop (keyboard polling, line buffering) */
void kernel_shell(void);
void kernel_os_shell(void);
void kernel_install_mode(void);

/* sys/shell/shell.c — OS CLI command dispatcher (ls, cat, mkdir, etc.)
 * Called by kernel_os_shell() after assembling a complete input line. */
void sys_shell_dispatch(const char *line);

/* sys/shell/shell.c — refresh the top status bar (username + date/time).
 * Safe to call even if the bar isn't enabled yet (no-op until installed). */
void sys_shell_update_statusbar(void);

#endif /* _KERNEL_SHELL_H */
