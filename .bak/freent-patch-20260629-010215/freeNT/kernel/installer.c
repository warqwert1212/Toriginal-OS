/* =============================================================================
 * INSTALLER.C — Toriginal OS installer
 * See installer.h for the high-level flow and scope notes.
 *
 * MOVED: this file used to live at root/installer/installer.c, a
 * directory the flat Makefile never compiled, so it was dead weight —
 * "install OS" in the shell used a separate, broken, inline duplicate
 * instead (see shell.c). Moved here so it's actually built, and shell.c
 * now calls installer_run() / installer_print_status() directly.
 * ========================================================================= */

#include "installer.h"
#include "trpfs.h"
#include "ntfs.h"
#include "fs.h"
#include "io.h"
#include "string.h"
#include "keyboard.h"
#include "serial.h"

#define INSTALLER_DISK_BYTES   (4ULL * 1024ULL * 1024ULL)  /* 4 MiB TRPFS volume */
#define INSTALLER_MAX_ERRORS   16
#define INSTALLER_ERROR_LEN    160

/* The "primary disk" backing TRPFS. Module-static so the pointer trpfs.c
 * holds onto stays valid for the lifetime of the OS. When a real AHCI/NVMe
 * driver exists, replace trpfs_ramdisk_init() below with a call that fills
 * in g_disk from that driver instead. */
static trpfs_blkdev_t g_disk;

typedef struct {
    char msgs[INSTALLER_MAX_ERRORS][INSTALLER_ERROR_LEN];
    int  count;
} installer_errors_t;

static void err_add(installer_errors_t *e, const char *msg) {
    if (e->count >= INSTALLER_MAX_ERRORS) return;
    int i = 0;
    while (msg[i] && i < INSTALLER_ERROR_LEN - 1) { e->msgs[e->count][i] = msg[i]; i++; }
    e->msgs[e->count][i] = '\0';
    e->count++;
}

static void err_print_all(const installer_errors_t *e) {
    if (e->count == 0) return;
    io_put_string("\n--- Installer reported the following problems: ---\n");
    for (int i = 0; i < e->count; i++) {
        io_put_string("  - ");
        io_put_string(e->msgs[i]);
        io_put_string("\n");
        serial_puts("[INSTALL] ERROR: ");
        serial_puts(e->msgs[i]);
        serial_puts("\n");
    }
    io_put_string("---------------------------------------------------\n\n");
}

/* ── Small line-editing input helper ─────────────────────────────────────── */

static void read_line(char *buf, int max_len, int mask) {
    int i = 0;
    for (;;) {
        char c = keyboard_getc();
        if (c == '\r' || c == '\n') break;
        if ((c == '\b' || c == 127) && i > 0) {
            i--;
            io_put_char('\b'); io_put_char(' '); io_put_char('\b');
            continue;
        }
        if (c < 0x20) continue;
        if (i + 1 < max_len) {
            buf[i++] = c;
            io_put_char(mask ? '*' : c);
        }
    }
    buf[i] = '\0';
    io_put_string("\n");
}

/* ── Step 1: provision the TRPFS volume ──────────────────────────────────── */

static int provision_disk(installer_errors_t *errs) {
    io_put_string("[1/5] Creating virtual disk (");
    {
        char nbuf[12]; int n = (int)(INSTALLER_DISK_BYTES / (1024 * 1024));
        int idx = 0; char tmp[12]; int ti = 0;
        if (n == 0) tmp[ti++] = '0';
        while (n > 0) { tmp[ti++] = (char)('0' + n % 10); n /= 10; }
        while (ti > 0) nbuf[idx++] = tmp[--ti];
        nbuf[idx] = '\0';
        io_put_string(nbuf);
    }
    io_put_string(" MiB RAM disk)...\n");

    if (trpfs_ramdisk_init(&g_disk, INSTALLER_DISK_BYTES) != 0) {
        err_add(errs, "Could not allocate RAM disk (out of memory)");
        return -1;
    }

    serial_puts("[INSTALL] RAM disk ready, total_blocks=");
    serial_write_dec(g_disk.total_blocks);
    serial_puts("\n");
    return 0;
}

/* ── Step 2: scan for an existing NTFS (Windows) install ─────────────────── */

static void scan_existing_os(installer_errors_t *errs) {
    (void)errs;
    io_put_string("[2/5] Scanning target for an existing operating system...\n");

    ntfs_volume_t vol;
    if (ntfs_detect(&g_disk, &vol) != 0) {
        io_put_string("      Could not read the target — skipping scan.\n");
        return;
    }

    if (!vol.detected) {
        io_put_string("      No existing NTFS (Windows) installation found.\n");
        return;
    }

    ntfs_read_volume_label(&g_disk, &vol);

    io_put_string("      *** Existing NTFS volume detected");
    if (vol.volume_label[0]) {
        io_put_string(" (label: ");
        io_put_string(vol.volume_label);
        io_put_string(")");
    }
    io_put_string(" ***\n");
    io_put_string("      Toriginal OS v1.1 does not write to NTFS volumes, so\n");
    io_put_string("      this data is not at risk from this installer directly,\n");
    io_put_string("      but continuing will format THIS virtual disk for\n");
    io_put_string("      Toriginal OS. For a real dual-boot, install Toriginal\n");
    io_put_string("      OS to a *separate* physical disk/partition.\n");
    io_put_string("\nContinue and format this disk for Toriginal OS? (Y/N): ");

    for (;;) {
        char c = keyboard_getc();
        if (c == 'y' || c == 'Y') { io_put_string("Y\n"); return; }
        if (c == 'n' || c == 'N') {
            io_put_string("N\n");
            io_put_string("Installation cancelled by user.\n");
            serial_puts("[INSTALL] Cancelled: NTFS volume present, user declined.\n");
            /* Caller checks this via the return path below */
            for (;;) { /* halt — nothing further to do */
                __asm__ volatile("hlt");
            }
        }
    }
}

/* ── Step 3: format + mount TRPFS, build directory tree ──────────────────── */

static int build_filesystem(installer_errors_t *errs) {
    io_put_string("[3/5] Formatting TRPFS volume \"TORIGINALOS\"...\n");

    if (trpfs_format(&g_disk, "TORIGINALOS") != 0) {
        err_add(errs, "trpfs_format() failed");
        return -1;
    }
    if (trpfs_mount(&g_disk) != 0) {
        err_add(errs, "trpfs_mount() failed after format");
        return -1;
    }

    static const char *dirs[] = {
        "/toriginal_os",
        "/toriginal_os/boot",
        "/toriginal_os/bin",
        "/toriginal_os/sys",
        "/toriginal_os/home",
    };

    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if (fs_mkdir(dirs[i],
                      FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X) != 0) {
            char msg[INSTALLER_ERROR_LEN];
            int n = 0;
            const char *p = "Failed to create directory: ";
            while (*p && n < INSTALLER_ERROR_LEN - 1) msg[n++] = *p++;
            p = dirs[i];
            while (*p && n < INSTALLER_ERROR_LEN - 1) msg[n++] = *p++;
            msg[n] = '\0';
            err_add(errs, msg);
        }
    }

    io_put_string("      Filesystem ready.\n");
    return 0;
}

/* ── Step 4: account setup ───────────────────────────────────────────────── */

typedef struct {
    char username[32];
    char password[32];
    char timezone[16];
    char accent[8];
} installer_account_t;

static void collect_account_info(installer_account_t *acct) {
    io_put_string("[4/5] Account setup\n");

    io_put_string("Username: ");
    read_line(acct->username, sizeof(acct->username), 0);
    if (acct->username[0] == '\0') {
        memcpy(acct->username, "user", 5);
    }

    io_put_string("Password: ");
    read_line(acct->password, sizeof(acct->password), 1);

    io_put_string("Timezone (e.g. UTC, UTC-5): ");
    read_line(acct->timezone, sizeof(acct->timezone), 0);
    if (acct->timezone[0] == '\0') {
        memcpy(acct->timezone, "UTC", 4);
    }

    io_put_string("Accent colour (blue/green/red) [blue]: ");
    read_line(acct->accent, sizeof(acct->accent), 0);
    if (acct->accent[0] == '\0') {
        memcpy(acct->accent, "blue", 5);
    }
}

/* ── Step 5: write config + flag, create home directory ──────────────────── */

static void write_kv(fd_t fd, const char *key, const char *value) {
    fs_write(fd, key, strlen(key));
    fs_write(fd, "=", 1);
    fs_write(fd, value, strlen(value));
    fs_write(fd, "\n", 1);
}

static int finalize_install(const installer_account_t *acct, installer_errors_t *errs) {
    io_put_string("[5/5] Writing configuration...\n");

    /* /toriginal_os/home/<username> */
    char home_path[64] = "/toriginal_os/home/";
    size_t base_len = strlen(home_path);
    size_t ulen = strlen(acct->username);
    if (ulen > sizeof(home_path) - base_len - 1) ulen = sizeof(home_path) - base_len - 1;
    memcpy(home_path + base_len, acct->username, ulen);
    home_path[base_len + ulen] = '\0';

    if (fs_mkdir(home_path, FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X) != 0) {
        err_add(errs, "Failed to create user home directory");
    }

    fd_t fd = fs_open("/toriginal_os/config.ini",
                       O_CREAT | O_WRONLY | O_TRUNC,
                       FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
    if (fd < 0) {
        err_add(errs, "Failed to create /toriginal_os/config.ini");
    } else {
        write_kv(fd, "username", acct->username);
        write_kv(fd, "timezone", acct->timezone);
        write_kv(fd, "accent",   acct->accent);
        /* NOTE: storing a plaintext password is a placeholder for v1 — a
         * real build should hash this before writing it to disk. */
        write_kv(fd, "password", acct->password);
        fs_close(fd);
    }

    fd = fs_open("/toriginal_os/installed.flag",
                  O_CREAT | O_WRONLY | O_TRUNC,
                  FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
    if (fd < 0) {
        err_add(errs, "Failed to create /toriginal_os/installed.flag");
        return -1;
    }
    fs_write(fd, "installed\n", 10);
    fs_close(fd);

    trpfs_sync();
    return 0;
}

/* ── Public entry points ─────────────────────────────────────────────────── */

void installer_run(void) {
    installer_errors_t errs;
    memset(&errs, 0, sizeof(errs));

    io_put_string("\n==================================================\n");
    io_put_string("        TORIGINAL OS INSTALLER (TRPFS v1)\n");
    io_put_string("==================================================\n\n");

    if (provision_disk(&errs) != 0) {
        err_print_all(&errs);
        return;
    }

    scan_existing_os(&errs); /* may halt internally if the user declines */

    if (build_filesystem(&errs) != 0) {
        err_print_all(&errs);
        return;
    }

    installer_account_t acct;
    memset(&acct, 0, sizeof(acct));
    collect_account_info(&acct);

    finalize_install(&acct, &errs);

    err_print_all(&errs);

    if (errs.count == 0) {
        io_put_string("Installation complete. Type 'status' to verify.\n");
        serial_puts("[INSTALL] Completed successfully.\n");
    } else {
        io_put_string("Installation finished with warnings/errors (see above).\n");
        serial_puts("[INSTALL] Completed with errors.\n");
    }
}

void installer_print_status(void) {
    if (!trpfs_is_mounted()) {
        io_put_string("No TRPFS volume is mounted. Run 'install OS' first.\n");
        return;
    }

    inode_t st;
    if (fs_stat("/toriginal_os/installed.flag", &st) == 0) {
        io_put_string("Toriginal OS is installed.\n");
        serial_puts("[INSTALL] status: installed.\n");
    } else {
        io_put_string("Toriginal OS is not installed yet. Use 'install OS' to install.\n");
        serial_puts("[INSTALL] status: not installed.\n");
    }
}
