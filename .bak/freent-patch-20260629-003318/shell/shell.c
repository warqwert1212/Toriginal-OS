/* =============================================================================
 * sys/shell/shell.c — Toriginal OS CLI layer
 *
 * All user-visible commands live here. The kernel's shell.c owns only the
 * raw input loop (keyboard polling, line buffer, Ctrl+C). This file owns
 * everything the user actually sees and interacts with.
 *
 * Commands:
 *   help                   list all commands
 *   sysver                 OS + kernel version
 *   cls                    clear the screen
 *   pwd                    print working directory
 *   cd <path>              change working directory
 *   ls [path]              list directory contents
 *   mkdir <path>           create a directory
 *   rm <path>              delete a file
 *   touch <path>           create an empty file
 *   write <path> <text>    write text into a file (overwrites)
 *   cat <path>             print a file to screen
 *   echo <text>            print text
 *   run <path.trp>         execute a TRP package
 *   free                   heap memory stats
 *   uname                  one-line system info
 *   install                provision + format + mount TRPFS
 *   status                 filesystem / installer status
 *   halt                   halt the system
 *   reboot                 reset via PS/2 controller
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

/* ── Version ────────────────────────────────────────────────────────────── */
#define OS_NAME     "Toriginal OS"
#define OS_VERSION  "1.0"
#define KERNEL_NAME "freeNT"
#define BUILD_ARCH  "x86-64"

/* ── Working directory ───────────────────────────────────────────────────── */
static char g_cwd[256] = "/";

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static void print_u64(uint64_t v) {
    if (v == 0) { io_put_char('0'); return; }
    char tmp[21]; int i = 0;
    while (v) { tmp[i++] = '0' + (char)(v % 10); v /= 10; }
    while (i--) io_put_char(tmp[i]);
}

static void print_size(uint64_t bytes) {
    if      (bytes >= 1024*1024) { print_u64(bytes/(1024*1024)); io_put_string(" MB"); }
    else if (bytes >= 1024)      { print_u64(bytes/1024);        io_put_string(" KB"); }
    else                         { print_u64(bytes);             io_put_string(" B");  }
}

/* Resolve path: absolute → use as-is, relative → prepend cwd. */
static void resolve_path(const char *arg, char *dst, size_t dstlen) {
    if (!arg || !arg[0]) {
        strncpy(dst, g_cwd, dstlen - 1);
        dst[dstlen - 1] = '\0';
        return;
    }
    if (arg[0] == '/') {
        strncpy(dst, arg, dstlen - 1);
        dst[dstlen - 1] = '\0';
        return;
    }
    size_t cwd_len = strlen(g_cwd);
    strncpy(dst, g_cwd, dstlen - 1);
    dst[dstlen - 1] = '\0';
    if (cwd_len > 0 && dst[cwd_len-1] != '/' && cwd_len < dstlen - 1)
        dst[cwd_len++] = '/', dst[cwd_len] = '\0';
    size_t di = cwd_len;
    for (size_t ai = 0; arg[ai] && di < dstlen - 1; ai++, di++)
        dst[di] = arg[ai];
    dst[di] = '\0';
}

/* Split a line at the first space into cmd + rest-of-line.
 * Returns pointer into buf past the first word, or NULL if nothing follows. */
static char *split_first(char *buf) {
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == ' ') {
            buf[i] = '\0';
            char *rest = buf + i + 1;
            while (*rest == ' ') rest++;
            return (*rest) ? rest : NULL;
        }
    }
    return NULL;
}

/* Check fs is mounted; print msg and return 0 if not. */
static int need_fs(const char *cmd) {
    if (trpfs_is_mounted()) return 1;
    io_put_string(cmd);
    io_put_string(": no filesystem mounted — run 'install' first\n");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Command implementations
 * ═══════════════════════════════════════════════════════════════════════════ */

static void cmd_help(void) {
    io_put_string("\n");
    io_put_string("  " OS_NAME " " OS_VERSION " — command reference\n");
    io_put_string("  ─────────────────────────────────────────────\n");
    io_put_string("  help                    this list\n");
    io_put_string("  sysver                  OS and kernel version\n");
    io_put_string("  uname                   one-line system info\n");
    io_put_string("  cls                     clear screen\n");
    io_put_string("\n");
    io_put_string("  pwd                     print working directory\n");
    io_put_string("  cd <path>               change working directory\n");
    io_put_string("  ls [path]               list directory (default: /)\n");
    io_put_string("  mkdir <path>            create directory\n");
    io_put_string("  touch <path>            create empty file\n");
    io_put_string("  rm <path>               delete file\n");
    io_put_string("\n");
    io_put_string("  cat <path>              print file to screen\n");
    io_put_string("  write <path> <text>     write text into file\n");
    io_put_string("  echo <text>             print text\n");
    io_put_string("\n");
    io_put_string("  run <path.trp>          execute TRP package\n");
    io_put_string("  free                    show heap memory stats\n");
    io_put_string("  install                 provision TRPFS filesystem\n");
    io_put_string("  status                  filesystem status\n");
    io_put_string("\n");
    io_put_string("  halt                    halt the system\n");
    io_put_string("  reboot                  restart the system\n");
    io_put_string("\n");
}

static void cmd_sysver(void) {
    io_put_string(OS_NAME " v" OS_VERSION
                  "  (" KERNEL_NAME " / " BUILD_ARCH ")\n");
    io_put_string("Kernel subsystems: free-list heap · TRPFS RAM disk"
                  " · PS/2 kbd+mouse · PIT timer\n");
    io_put_string("Toolchain: x86_64-linux-gnu-gcc (freestanding)"
                  " · GRUB Multiboot2\n");
}

static void cmd_uname(void) {
    io_put_string(KERNEL_NAME " " OS_NAME " " OS_VERSION
                  " " BUILD_ARCH "\n");
}

static void cmd_cls(void) {
    io_clear_screen();
}

static void cmd_pwd(void) {
    io_put_string(g_cwd);
    io_put_char('\n');
}

static void cmd_cd(const char *arg) {
    if (!arg || !arg[0]) {
        /* cd with no argument → go to root */
        g_cwd[0] = '/'; g_cwd[1] = '\0';
        return;
    }
    if (!need_fs("cd")) return;

    char path[256];
    resolve_path(arg, path, sizeof(path));

    /* Verify it exists and is a directory. */
    inode_t st;
    if (fs_stat(path, &st) != 0) {
        io_put_string("cd: no such directory: ");
        io_put_string(path); io_put_char('\n');
        return;
    }
    if (!(st.mode & FILE_TYPE_DIR)) {
        io_put_string("cd: not a directory: ");
        io_put_string(path); io_put_char('\n');
        return;
    }

    strncpy(g_cwd, path, sizeof(g_cwd) - 1);
    g_cwd[sizeof(g_cwd) - 1] = '\0';
    /* Strip trailing slash unless we're at root. */
    size_t len = strlen(g_cwd);
    if (len > 1 && g_cwd[len-1] == '/')
        g_cwd[len-1] = '\0';
}

/* ls callback — called once per directory entry. */
static int ls_cb(const char *name, uint8_t name_len, uint8_t type, void *ctx) {
    (void)ctx;
    io_put_string("  ");
    if (type == FILE_TYPE_DIR) io_put_char('[');
    for (uint8_t i = 0; i < name_len; i++) io_put_char(name[i]);
    if (type == FILE_TYPE_DIR) io_put_char(']');
    io_put_char('\n');
    return 0;
}

static void cmd_ls(const char *arg) {
    if (!need_fs("ls")) return;

    char path[256];
    resolve_path(arg, path, sizeof(path));

    inode_t st;
    if (fs_stat(path, &st) != 0) {
        io_put_string("ls: not found: ");
        io_put_string(path); io_put_char('\n');
        return;
    }
    if (!(st.mode & FILE_TYPE_DIR)) {
        /* Single file — print name and size. */
        io_put_string("  ");
        io_put_string(path);
        io_put_string("  ("); print_size(st.size); io_put_string(")\n");
        return;
    }

    io_put_string(path); io_put_string(":\n");
    if (fs_readdir(path, ls_cb, NULL) != 0)
        io_put_string("ls: error reading directory\n");
}

static void cmd_mkdir(const char *arg) {
    if (!arg || !arg[0]) { io_put_string("usage: mkdir <path>\n"); return; }
    if (!need_fs("mkdir")) return;

    char path[256];
    resolve_path(arg, path, sizeof(path));

    if (fs_mkdir(path, 0755) == 0) {
        io_put_string("mkdir: created "); io_put_string(path); io_put_char('\n');
    } else {
        io_put_string("mkdir: failed — already exists or bad path\n");
    }
}

static void cmd_touch(const char *arg) {
    if (!arg || !arg[0]) { io_put_string("usage: touch <path>\n"); return; }
    if (!need_fs("touch")) return;

    char path[256];
    resolve_path(arg, path, sizeof(path));

    /* O_CREAT with O_WRONLY creates an empty file; close immediately. */
    fd_t fd = fs_open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        io_put_string("touch: failed to create "); io_put_string(path); io_put_char('\n');
        return;
    }
    fs_close(fd);
    io_put_string("touch: created "); io_put_string(path); io_put_char('\n');
}

static void cmd_rm(const char *arg) {
    if (!arg || !arg[0]) { io_put_string("usage: rm <path>\n"); return; }
    if (!need_fs("rm")) return;

    char path[256];
    resolve_path(arg, path, sizeof(path));

    if (fs_unlink(path) == 0) {
        io_put_string("rm: removed "); io_put_string(path); io_put_char('\n');
    } else {
        io_put_string("rm: failed — not found or is a directory\n");
    }
}

static void cmd_cat(const char *arg) {
    if (!arg || !arg[0]) { io_put_string("usage: cat <path>\n"); return; }
    if (!need_fs("cat")) return;

    char path[256];
    resolve_path(arg, path, sizeof(path));

    fd_t fd = fs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        io_put_string("cat: cannot open: "); io_put_string(path); io_put_char('\n');
        return;
    }
    char buf[512]; ssize_t n;
    while ((n = fs_read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        io_put_string(buf);
    }
    io_put_char('\n');
    fs_close(fd);
}

/* write <path> <text>  — first arg is path, rest of line is content. */
static void cmd_write(const char *arg) {
    if (!arg || !arg[0]) {
        io_put_string("usage: write <path> <text>\n");
        return;
    }
    if (!need_fs("write")) return;

    /* Split arg at first space: left = path, right = text. */
    char abuf[256];
    strncpy(abuf, arg, sizeof(abuf) - 1);
    abuf[sizeof(abuf) - 1] = '\0';
    char *text = split_first(abuf);
    /* abuf now holds just the path, text points to the rest. */

    if (!text || !text[0]) {
        io_put_string("write: no text given — usage: write <path> <text>\n");
        return;
    }

    char path[256];
    resolve_path(abuf, path, sizeof(path));

    fd_t fd = fs_open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        io_put_string("write: cannot open: "); io_put_string(path); io_put_char('\n');
        return;
    }
    size_t len = strlen(text);
    ssize_t written = fs_write(fd, text, len);
    /* Write a newline after the text. */
    char nl = '\n';
    fs_write(fd, &nl, 1);
    fs_close(fd);

    if (written < 0) {
        io_put_string("write: I/O error\n");
    } else {
        io_put_string("write: "); print_u64((uint64_t)written);
        io_put_string(" bytes written to "); io_put_string(path); io_put_char('\n');
    }
}

static void cmd_echo(const char *arg) {
    if (arg && arg[0]) io_put_string(arg);
    io_put_char('\n');
}

static void cmd_run(const char *arg) {
    if (!arg || !arg[0]) { io_put_string("usage: run <path.trp>\n"); return; }

    char path[256];
    resolve_path(arg, path, sizeof(path));

    io_put_string("run: loading "); io_put_string(path); io_put_string(" ...\n");

    process_t *proc = process_create(path, 1);
    if (!proc) { io_put_string("run: failed to create process\n"); return; }

    if (loader_load_executable(path, proc->pid) == 0) {
        process_start(proc->pid);
    } else {
        io_put_string("run: loader rejected ");
        io_put_string(path);
        io_put_string(" (bad format or checksum)\n");
    }
}

static void cmd_free(void) {
    uint64_t allocated = 0, arena = 0, allocs = 0, frees = 0;
    heap_get_stats(&allocated, &arena, &allocs, &frees);
    uint64_t free_bytes = arena > allocated ? arena - allocated : 0;

    io_put_string("Memory (kernel heap):\n");
    io_put_string("  total  : "); print_size(arena);      io_put_char('\n');
    io_put_string("  used   : "); print_size(allocated);  io_put_char('\n');
    io_put_string("  free   : "); print_size(free_bytes); io_put_char('\n');
    io_put_string("  allocs : "); print_u64(allocs);      io_put_char('\n');
    io_put_string("  frees  : "); print_u64(frees);       io_put_char('\n');
}

static void cmd_halt(void) {
    io_put_string("System halted. Safe to power off.\n");
    serial_puts("[HALT] halted by user\n");
    asm volatile("cli");
    while (1) asm volatile("hlt");
}

static void cmd_reboot(void) {
    io_put_string("Rebooting...\n");
    serial_puts("[REBOOT] user requested reboot\n");
    asm volatile("cli");
    for (volatile int i = 0; i < 100000; i++);
    /* Pulse PS/2 reset line */
    asm volatile("mov $0xFE, %%al; out %%al, $0x64" ::: "al");
    while (1) asm volatile("hlt");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public dispatch — called by kernel/shell.c on every completed input line
 * ═══════════════════════════════════════════════════════════════════════════ */
void sys_shell_dispatch(const char *line) {
    if (!line) return;
    while (*line == ' ') line++;
    if (!*line) return;

    /* Copy into mutable buffer and split. */
    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *arg = split_first(buf);
    char *cmd = buf;

    if (strcmp(cmd, "help")    == 0) { cmd_help();              return; }
    if (strcmp(cmd, "sysver")  == 0) { cmd_sysver();            return; }
    if (strcmp(cmd, "uname")   == 0) { cmd_uname();             return; }
    if (strcmp(cmd, "cls")     == 0) { cmd_cls();               return; }
    if (strcmp(cmd, "pwd")     == 0) { cmd_pwd();               return; }
    if (strcmp(cmd, "cd")      == 0) { cmd_cd(arg);             return; }
    if (strcmp(cmd, "ls")      == 0) { cmd_ls(arg);             return; }
    if (strcmp(cmd, "mkdir")   == 0) { cmd_mkdir(arg);          return; }
    if (strcmp(cmd, "touch")   == 0) { cmd_touch(arg);          return; }
    if (strcmp(cmd, "rm")      == 0) { cmd_rm(arg);             return; }
    if (strcmp(cmd, "cat")     == 0) { cmd_cat(arg);            return; }
    if (strcmp(cmd, "write")   == 0) { cmd_write(arg);          return; }
    if (strcmp(cmd, "echo")    == 0) { cmd_echo(arg);           return; }
    if (strcmp(cmd, "run")     == 0) { cmd_run(arg);            return; }
    if (strcmp(cmd, "free")    == 0) { cmd_free();              return; }
    if (strcmp(cmd, "install") == 0) { installer_run();         return; }
    if (strcmp(cmd, "status")  == 0) { installer_print_status(); return; }
    if (strcmp(cmd, "halt")    == 0) { cmd_halt();              return; }
    if (strcmp(cmd, "reboot")  == 0) { cmd_reboot();            return; }

    io_put_string("unknown command: '");
    io_put_string(cmd);
    io_put_string("'  (type 'help')\n");
}
