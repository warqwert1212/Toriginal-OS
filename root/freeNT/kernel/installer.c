/* =============================================================================
 * INSTALLER.C - Toriginal OS installer
 * See installer.h for the high-level flow and scope notes.
 *
 * MOVED: this file used to live at root/installer/installer.c, a
 * directory the flat Makefile never compiled, so it was dead weight -
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
#include "ata.h"
#include "serial.h"

#define INSTALLER_DISK_BYTES   (4ULL * 1024ULL * 1024ULL)  /* 4 MiB TRPFS volume */
#define INSTALLER_MAX_ERRORS   16
#define INSTALLER_ERROR_LEN    160

/* Defined in kernel.c - writes every multiboot module's bytes (see
 * parse_multiboot()'s MB2_TAG_MODULE handling and grub.cfg's module2
 * lines) to the TRPFS path its cmdline names. Called here so a fresh
 * install gets wallpapers/start menu images/cursor immediately, and
 * again from kernel_init()'s automount path on every later boot so a
 * rebuilt ISO's assets stay in sync with what's on disk. */
void seed_boot_modules_to_fs(void);

/* The "primary disk" backing TRPFS. Module-static so the pointer trpfs.c
 * holds onto stays valid for the lifetime of the OS.
 *
 * provision_disk() tries a real ATA disk first (for actual persistence
 * across reboots) and falls back to a RAM disk if no ATA hardware is
 * present (e.g. a VM with no HDD attached). g_using_disk reflects which
 * one ended up active, and is checked at boot by installer_try_automount(). */
static trpfs_blkdev_t *g_disk = NULL;
static int g_using_real_disk = 0;

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

static char installer_getc(void) {
    char c = keyboard_getc_nb();
    if (c) return c;

    uint8_t lsr;
    __asm__ volatile("inb %%dx, %0" : "=a"(lsr) : "d"((uint16_t)0x3FD));
    if (lsr & 0x01) {
        uint8_t s;
        __asm__ volatile("inb %%dx, %0" : "=a"(s) : "d"((uint16_t)0x3F8));
        return (char)s;
    }
    __asm__ volatile("pause");
    return 0;
}

static void read_line(char *buf, int max_len, int mask) {
    int i = 0;
    for (;;) {
        char c = installer_getc();
        if (!c) continue;
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

static int ensure_parent_dir(const char *path) {
    char tmp[256];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] != '/') continue;
        tmp[i] = '\0';
        if (tmp[0] == '\0') continue;
        inode_t st;
        if (fs_stat(tmp, &st) == 0) {
            if (FS_IS_DIR(st.mode)) {
                tmp[i] = '/';
                continue;
            }
        }
        if (fs_mkdir(tmp, FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X) != 0) {
            tmp[i] = '/';
            return -1;
        }
        tmp[i] = '/';
    }
    return 0;
}

static int write_seed_file(const char *path, const char *content) {
    if (!path || !content) return -1;
    if (ensure_parent_dir(path) != 0) return -1;
    fd_t fd = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC,
                      FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
    if (fd < 0) return -1;
    size_t n = strlen(content);
    int ok = (fs_write(fd, content, n) == (ssize_t)n);
    fs_close(fd);
    return ok ? 0 : -1;
}

int installer_copy_file(const char *src, const char *dst) {
    if (!src || !dst) return -1;
    if (ensure_parent_dir(dst) != 0) return -1;

    fd_t src_fd = fs_open(src, O_RDONLY, 0);
    if (src_fd < 0) return -1;
    fd_t dst_fd = fs_open(dst, O_WRONLY | O_CREAT | O_TRUNC,
                          FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
    if (dst_fd < 0) {
        fs_close(src_fd);
        return -1;
    }

    char buf[512];
    ssize_t n;
    int ok = 1;
    while ((n = fs_read(src_fd, buf, sizeof(buf))) > 0) {
        if (fs_write(dst_fd, buf, (size_t)n) != n) {
            ok = 0;
            break;
        }
    }
    fs_close(src_fd);
    fs_close(dst_fd);
    return ok ? 0 : -1;
}

static int seed_install_payload(installer_errors_t *errs) {
    io_put_string("[4/5] Seeding install payload...\n");

    static const struct {
        const char *src;
        const char *dst;
        const char *content;
    } files[] = {
        { "/install_seed/README.txt",          "/toriginal_os/README.txt",          "Welcome to Toriginal OS.\n" },
        { "/install_seed/boot/boot.txt",       "/toriginal_os/boot/boot.txt",       "Toriginal OS boot payload\n" },
        { "/install_seed/bin/hello.sh",        "/toriginal_os/bin/hello.sh",        "#!/bin/sh\necho hello from Toriginal OS\n" },
        { "/install_seed/home/notes.txt",      "/toriginal_os/home/notes.txt",      "This file was installed to disk.\n" },
    };

    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (write_seed_file(files[i].src, files[i].content) != 0) {
            err_add(errs, "Failed to stage install payload source file");
            return -1;
        }

        io_put_string("      Copying ");
        io_put_string(files[i].src);
        io_put_string(" -> ");
        io_put_string(files[i].dst);
        io_put_string("\n");
        if (installer_copy_file(files[i].src, files[i].dst) != 0) {
            err_add(errs, "Failed to copy install payload to the disk-backed filesystem");
            return -1;
        }
    }

    return 0;
}

/* ── Step 1: provision the TRPFS volume ──────────────────────────────────── */

static int provision_disk(installer_errors_t *errs) {
    io_put_string("[1/5] Locating storage device...\n");

    /* Real persistence requires a real ATA/IDE disk. */
    trpfs_blkdev_t *ata = ata_init_blkdev(INSTALLER_DISK_BYTES);
    if (ata) {
        g_disk = ata;
        g_using_real_disk = 1;
        io_put_string("      Found ATA disk - Toriginal OS will persist across reboots.\n");
    } else {
        io_put_string("      No ATA disk detected.\n");
        io_put_string("      Install requires a real ATA/IDE disk to persist data.\n");
        io_put_string("      Attach a virtual HDD and run install again.\n");
        err_add(errs, "No ATA disk detected - persistence requires a real disk");
        return -1;
    }

    serial_puts("[INSTALL] Disk ready, total_blocks=");
    serial_write_dec(g_disk->total_blocks);
    serial_puts("  (ATA, persistent)\n");
    return 0;
}

/* ── Step 2: scan for an existing NTFS (Windows) install ─────────────────── */

static void scan_existing_os(installer_errors_t *errs) {
    (void)errs;
    io_put_string("[2/5] Scanning target for an existing operating system...\n");

    ntfs_volume_t vol;
    if (ntfs_detect(g_disk, &vol) != 0) {
        io_put_string("      Could not read the target - skipping scan.\n");
        return;
    }

    if (!vol.detected) {
        io_put_string("      No existing NTFS (Windows) installation found.\n");
        return;
    }

    ntfs_read_volume_label(g_disk, &vol);

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
            for (;;) { /* halt - nothing further to do */
                __asm__ volatile("hlt");
            }
        }
    }
}

/* ── Step 3: format + mount TRPFS, build directory tree ──────────────────── */

static int build_filesystem(installer_errors_t *errs) {
    io_put_string("[3/5] Formatting TRPFS volume \"TORIGINALOS\"...\n");

    if (trpfs_format(g_disk, "TORIGINALOS") != 0) {
        err_add(errs, "trpfs_format() failed");
        return -1;
    }
    if (trpfs_mount(g_disk) != 0) {
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
} installer_account_t;

static void collect_account_info(installer_account_t *acct, int unattended) {
    if (unattended) {
        memcpy(acct->username, "user", 5);
        memcpy(acct->password, "password", 9);
        memcpy(acct->timezone, "UTC", 4);
        return;
    }

    io_put_string("[4/5] OOBE setup\n");

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
        write_kv(fd, "resolution", "720p");
        write_kv(fd, "storage", "ata");
        /* NOTE: storing a plaintext password is a placeholder for v1 - a
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

static void installer_run_internal(int unattended) {
    installer_errors_t errs;
    memset(&errs, 0, sizeof(errs));

    if (!unattended) {
        io_put_string("\n==================================================\n");
        io_put_string("        TORIGINAL OS SETUP / OOBE (TRPFS v1)\n");
        io_put_string("==================================================\n\n");
    }

    if (provision_disk(&errs) != 0) {
        err_print_all(&errs);
        return;
    }

    if (!unattended) {
        scan_existing_os(&errs); /* may halt internally if the user declines */
    }

    if (build_filesystem(&errs) != 0) {
        err_print_all(&errs);
        return;
    }

    if (seed_install_payload(&errs) != 0) {
        err_print_all(&errs);
        return;
    }

    /* Wallpapers/start menu images/cursor land on disk right now, as
     * part of this install, instead of requiring a reboot before
     * they'd show up via kernel_init()'s automount path. */
    seed_boot_modules_to_fs();

    installer_account_t acct;
    memset(&acct, 0, sizeof(acct));
    collect_account_info(&acct, unattended);

    finalize_install(&acct, &errs);

    err_print_all(&errs);

    if (errs.count == 0) {
        if (unattended) {
            io_put_string("First-boot installation complete.\n");
            serial_puts("[INSTALL] First-boot installation completed successfully.\n");
        } else {
            io_put_string("Setup complete. Type 'status' to verify.\n");
            serial_puts("[INSTALL] Setup completed successfully.\n");
        }
    } else {
        io_put_string("Installation finished with warnings/errors (see above).\n");
        serial_puts("[INSTALL] Completed with errors.\n");
    }
}

void installer_run(void) {
    installer_run_internal(0);
}

void installer_run_unattended(void) {
    installer_run_internal(1);
}

/* ── Boot-time auto-mount ─────────────────────────────────────────────────
 * Called once from kernel_init(), before the shell starts. If a real ATA
 * disk is present AND it already has a valid TRPFS superblock (i.e. the
 * user ran 'install' in a previous boot), mount it directly - no
 * reformatting, no account setup, just bring the existing filesystem
 * online so all prior files/folders are exactly as they were left. */
int installer_try_automount(void) {
    trpfs_blkdev_t *ata = ata_init_blkdev(INSTALLER_DISK_BYTES);
    if (!ata) {
        serial_puts("[INSTALL] No ATA disk detected - install requires a "
                     "real ATA/IDE disk for persistence.\n");
        return -1;
    }

    g_disk = ata;
    g_using_real_disk = 1;

    if (trpfs_mount(g_disk) == 0) {
        serial_puts("[INSTALL] Existing filesystem found on disk - auto-mounted.\n");
        return 0;
    }

    serial_puts("[INSTALL] ATA disk present but no valid filesystem found - "
                "starting first-boot installation.\n");
    installer_run_unattended();

    if (trpfs_mount(g_disk) == 0) {
        serial_puts("[INSTALL] First-boot installation completed and filesystem "
                    "is now mounted.\n");
        return 0;
    }

    serial_puts("[INSTALL] First-boot installation did not leave a valid filesystem "
                "mounted.\n");
    return -1;
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
