/* =============================================================================
 * sys/shell/shell.c — Toriginal OS CLI layer
 *
 * This is the OS-facing command interpreter. It is NOT part of the kernel
 * proper (lives in sys/shell/, not kernel/) — the kernel's shell.c owns
 * only the raw input loop and PS/2 keyboard polling. This file owns every
 * user-visible command: what they do, how they print output, and what the
 * filesystem API calls look like.
 *
 * Compiled into the kernel image via the Makefile SYS_SRCS glob so it
 * shares the same address space as the kernel (no process boundary yet —
 * that's v2). The separation is structural / logical, not a hard ABI wall,
 * but it means all the command code can move to a real userspace process
 * later without touching kernel/shell.c at all.
 *
 * Commands implemented:
 *   sysver              — print OS version string
 *   help                — list all commands
 *   ls [path]           — list directory contents (default: /)
 *   cat <path>          — print a file to screen
 *   mkdir <path>        — create a directory
 *   run <path.trp>      — execute a TRP package
 *   pwd                 — print current working directory
 *   echo <text>         — print text
 *   free                — show heap memory stats
 *   install             — run the TRPFS installer (provision + format + mount)
 *   status              — show installer/filesystem status
 *   halt                — halt the system
 *   reboot              — reset via PS/2 controller
 * ========================================================================= */

#include "io.h"
#include "string.h"
#include "fs.h"
#include "trpfs.h"
#include "process.h"
#include "loader.h"
#include "serial.h"
#include "installer.h"
#include "memory.h"

/* ── Version string ─────────────────────────────────────────────────────── */

#define FREENT_VERSION   "freeNT 1.0"
#define TORIGINAL_VER    "Toriginal OS v1.0"
#define BUILD_ARCH       "x86-64"

/* ── Current working directory (single global for v1 — no per-process
 *    context yet; that comes with real process address spaces in v2) ───── */

static char g_cwd[256] = "/";

/* ── Small helpers ──────────────────────────────────────────────────────── */

/* Print a decimal uint64_t — no libc, so roll our own. */
static void print_u64(uint64_t v) {
    if (v == 0) { io_put_char('0'); return; }
    char tmp[21];
    int i = 0;
    while (v) { tmp[i++] = '0' + (char)(v % 10); v /= 10; }
    while (i--) io_put_char(tmp[i]);
}

/* Print v as a human-readable size (B / KB / MB). */
static void print_size(uint64_t bytes) {
    if (bytes >= 1024 * 1024) {
        print_u64(bytes / (1024 * 1024));
        io_put_string(" MB");
    } else if (bytes >= 1024) {
        print_u64(bytes / 1024);
        io_put_string(" KB");
    } else {
        print_u64(bytes);
        io_put_string(" B");
    }
}

/* Resolve a possibly-relative path against g_cwd into dst (up to dstlen). */
static void resolve_path(const char *arg, char *dst, size_t dstlen) {
    if (!arg || arg[0] == '\0') {
        /* No argument — return cwd. */
        strncpy(dst, g_cwd, dstlen - 1);
        dst[dstlen - 1] = '\0';
        return;
    }
    if (arg[0] == '/') {
        /* Absolute path — use as-is. */
        strncpy(dst, arg, dstlen - 1);
        dst[dstlen - 1] = '\0';
        return;
    }
    /* Relative path — prepend cwd. */
    size_t cwd_len = strlen(g_cwd);
    strncpy(dst, g_cwd, dstlen - 1);
    dst[dstlen - 1] = '\0';
    /* Ensure there's a trailing slash on cwd before appending. */
    if (cwd_len > 0 && dst[cwd_len - 1] != '/' && cwd_len < dstlen - 1) {
        dst[cwd_len++] = '/';
        dst[cwd_len]   = '\0';
    }
    /* Manually append arg (strncat not in kernel string lib). */
    size_t di = cwd_len;
    for (size_t ai = 0; arg[ai] && di < dstlen - 1; ai++, di++) {
        dst[di] = arg[ai];
    }
    dst[di] = '\0';
}

/* ── Command implementations ────────────────────────────────────────────── */

static void cmd_sysver(void) {
    io_put_string(TORIGINAL_VER " (" FREENT_VERSION " / " BUILD_ARCH ")\n");
    io_put_string("Kernel: freeNT free-list heap, TRPFS RAM disk, PS/2 kbd+mouse\n");
    io_put_string("Built with: x86_64-linux-gnu-gcc (freestanding), GRUB Multiboot2\n");
}

static void cmd_help(void) {
    io_put_string("\nToriginal OS CLI commands:\n");
    io_put_string("  sysver          show OS and kernel version\n");
    io_put_string("  ls [path]       list directory (default: /)\n");
    io_put_string("  cat <path>      print file contents\n");
    io_put_string("  mkdir <path>    create directory\n");
    io_put_string("  run <path>      execute TRP package\n");
    io_put_string("  pwd             print current directory\n");
    io_put_string("  echo <text>     print text\n");
    io_put_string("  free            show memory usage\n");
    io_put_string("  install         provision and format TRPFS filesystem\n");
    io_put_string("  status          show filesystem and installer status\n");
    io_put_string("  halt            halt the system\n");
    io_put_string("  reboot          restart the system\n");
    io_put_string("  help            show this list\n\n");
}

/* Callback used by fs_readdir for the ls command. */
static int ls_callback(const char *name, uint8_t name_len,
                        uint8_t type, void *ctx) {
    (void)ctx;
    /* Print type indicator. */
    if (type == FILE_TYPE_DIR) {
        io_put_char('[');
    }
    /* name is not guaranteed NUL-terminated — print exactly name_len chars. */
    for (uint8_t i = 0; i < name_len; i++) {
        io_put_char(name[i]);
    }
    if (type == FILE_TYPE_DIR) {
        io_put_char(']');
    }
    io_put_string("  ");
    return 0; /* continue walking */
}

static void cmd_ls(const char *arg) {
    if (!trpfs_is_mounted()) {
        io_put_string("ls: no filesystem mounted (run 'install' first)\n");
        return;
    }

    char path[256];
    resolve_path(arg, path, sizeof(path));

    /* Verify the path exists and is a directory before calling readdir. */
    inode_t st;
    if (fs_stat(path, &st) != 0) {
        io_put_string("ls: no such directory: ");
        io_put_string(path);
        io_put_char('\n');
        return;
    }
    if (!(st.mode & FILE_TYPE_DIR)) {
        /* It's a regular file — just print its name and size. */
        io_put_string(path);
        io_put_string("  (");
        print_size(st.size);
        io_put_string(")\n");
        return;
    }

    io_put_string(path);
    io_put_string(":\n  ");

    int r = fs_readdir(path, ls_callback, NULL);
    io_put_char('\n');

    if (r != 0) {
        io_put_string("ls: error reading directory\n");
    }
}

static void cmd_cat(const char *arg) {
    if (!arg || arg[0] == '\0') {
        io_put_string("usage: cat <path>\n");
        return;
    }
    if (!trpfs_is_mounted()) {
        io_put_string("cat: no filesystem mounted (run 'install' first)\n");
        return;
    }

    char path[256];
    resolve_path(arg, path, sizeof(path));

    fd_t fd = fs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        io_put_string("cat: cannot open: ");
        io_put_string(path);
        io_put_char('\n');
        return;
    }

    char buf[512];
    ssize_t n;
    while ((n = fs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        io_put_string(buf);
    }
    io_put_char('\n');
    fs_close(fd);
}

static void cmd_mkdir(const char *arg) {
    if (!arg || arg[0] == '\0') {
        io_put_string("usage: mkdir <path>\n");
        return;
    }
    if (!trpfs_is_mounted()) {
        io_put_string("mkdir: no filesystem mounted (run 'install' first)\n");
        return;
    }

    char path[256];
    resolve_path(arg, path, sizeof(path));

    int r = fs_mkdir(path, 0755);
    if (r == 0) {
        io_put_string("mkdir: created ");
        io_put_string(path);
        io_put_char('\n');
    } else {
        io_put_string("mkdir: failed to create ");
        io_put_string(path);
        io_put_string(" (already exists or I/O error)\n");
    }
}

static void cmd_run(const char *arg) {
    if (!arg || arg[0] == '\0') {
        io_put_string("usage: run <path.trp>\n");
        return;
    }
    char path[256];
    resolve_path(arg, path, sizeof(path));

    io_put_string("run: loading ");
    io_put_string(path);
    io_put_string(" ...\n");

    process_t *proc = process_create(path, 1);
    if (!proc) {
        io_put_string("run: failed to create process\n");
        return;
    }
    if (loader_load_executable(path, proc->pid) == 0) {
        process_start(proc->pid);
    } else {
        io_put_string("run: loader rejected ");
        io_put_string(path);
        io_put_string(" (bad format or checksum)\n");
    }
}

static void cmd_pwd(void) {
    io_put_string(g_cwd);
    io_put_char('\n');
}

static void cmd_echo(const char *arg) {
    if (arg && arg[0]) {
        io_put_string(arg);
    }
    io_put_char('\n');
}

static void cmd_free(void) {
    uint64_t allocated = 0, arena = 0, allocs = 0, frees = 0;
    heap_get_stats(&allocated, &arena, &allocs, &frees);

    io_put_string("Heap memory:\n");
    io_put_string("  arena total : "); print_size(arena);      io_put_char('\n');
    io_put_string("  in use      : "); print_size(allocated);  io_put_char('\n');
    io_put_string("  free        : "); print_size(arena > allocated ? arena - allocated : 0); io_put_char('\n');
    io_put_string("  alloc calls : "); print_u64(allocs); io_put_char('\n');
    io_put_string("  free  calls : "); print_u64(frees);  io_put_char('\n');
}

static void cmd_halt(void) {
    io_put_string("System halted. Safe to power off.\n");
    serial_puts("[HALT] system halted by user\n");
    asm volatile("cli");
    while (1) { asm volatile("hlt"); }
}

static void cmd_reboot(void) {
    io_put_string("Rebooting...\n");
    serial_puts("[REBOOT] user requested reboot\n");
    asm volatile("cli");
    /* Drain PS/2 input buffer, then pulse the reset line. */
    for (volatile int i = 0; i < 100000; i++);
    asm volatile(
        "mov $0xFE, %%al\n"
        "out %%al, $0x64\n"
        ::: "al"
    );
    /* Triple-fault fallback if the pulse didn't reset. */
    while (1) { asm volatile("hlt"); }
}

/* ── Public dispatch entry point ────────────────────────────────────────── *
 * Called by kernel/shell.c after it has read and NUL-terminated a line.
 * Tokenises the first word as the command, passes the rest as `arg`.      */
void sys_shell_dispatch(const char *line) {
    if (!line || line[0] == '\0') return;

    /* Skip leading spaces. */
    while (*line == ' ') line++;
    if (*line == '\0') return;

    /* Copy into a mutable buffer for tokenisation. */
    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Split: find first space after the command word. */
    char *cmd = buf;
    char *arg = NULL;
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == ' ') {
            buf[i] = '\0';
            arg = buf + i + 1;
            /* Skip extra spaces between cmd and arg. */
            while (*arg == ' ') arg++;
            if (*arg == '\0') arg = NULL;
            break;
        }
    }

    /* Route to command handler. */
    if (strcmp(cmd, "help")    == 0) { cmd_help();           return; }
    if (strcmp(cmd, "sysver")  == 0) { cmd_sysver();         return; }
    if (strcmp(cmd, "ls")      == 0) { cmd_ls(arg);          return; }
    if (strcmp(cmd, "cat")     == 0) { cmd_cat(arg);         return; }
    if (strcmp(cmd, "mkdir")   == 0) { cmd_mkdir(arg);       return; }
    if (strcmp(cmd, "run")     == 0) { cmd_run(arg);         return; }
    if (strcmp(cmd, "pwd")     == 0) { cmd_pwd();            return; }
    if (strcmp(cmd, "echo")    == 0) { cmd_echo(arg);        return; }
    if (strcmp(cmd, "free")    == 0) { cmd_free();           return; }
    if (strcmp(cmd, "install") == 0) { installer_run();      return; }
    if (strcmp(cmd, "status")  == 0) { installer_print_status(); return; }
    if (strcmp(cmd, "halt")    == 0) { cmd_halt();           return; }
    if (strcmp(cmd, "reboot")  == 0) { cmd_reboot();         return; }

    /* Unknown command. */
    io_put_string("unknown command: '");
    io_put_string(cmd);
    io_put_string("'  (type 'help')\n");
}
