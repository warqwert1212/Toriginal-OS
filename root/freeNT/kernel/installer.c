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
#include "usb_hid.h"
#include "serial.h"
#include "ata.h"
#include "syscfg.h"
#include "net.h"
#include "heap.h"
#include "trbl_settings.h"
#include "mbr.h"
#include "process.h"
#include "vga.h"
#include "install_abi.h"

/* FIX: was 4 MiB. seed_embedded_assets_to_fs() (called from
 * installer_stage_files(), right before finalize_install() writes
 * config.ini) copies the embedded wallpaper/startmenu/cursor asset
 * blob into this same volume - and that blob alone is ~4.2 MB (see
 * build_asset_blob.py's own build-time output), already bigger than
 * the entire old 4 MiB volume before config.ini ever got a chance to
 * write. TRPFS ran out of space partway through staging, and
 * config.ini's write was just the first casualty once it did - not a
 * config.ini-specific bug. 64 MiB leaves real headroom over the ~4.2
 * MB of assets plus the install_seed payload plus inode/directory
 * overhead; the real underlying disk (a multi-GB VHD in testing) has
 * far more room than this to spare either way. */
#define INSTALLER_DISK_BYTES   (64ULL * 1024ULL * 1024ULL)  /* 64 MiB TRPFS volume */
#define INSTALLER_MAX_ERRORS   16
#define INSTALLER_ERROR_LEN    160

/* Defined in kernel.c - writes every multiboot module's bytes (see
 * parse_multiboot()'s MB2_TAG_MODULE handling and grub.cfg's module2
 * lines) to the TRPFS path its cmdline names. Called here so a fresh
 * install gets wallpapers/start menu images/cursor immediately, and
 * again from kernel_init()'s automount path on every later boot so a
 * rebuilt ISO's assets stay in sync with what's on disk. */
void seed_boot_modules_to_fs(void);

/* Defined in kernel.c - unpacks the asset archive baked directly
 * into kernel.elf (kernel/boot/asset_blob.s) to the TRPFS paths its
 * table names. Works on every boot regardless of which bootloader
 * ran (unlike seed_boot_modules_to_fs() above, which only has
 * anything to seed when GRUB delivered multiboot2 modules - TRBL,
 * this OS's own bootloader used for every boot after the first
 * install, has no module mechanism at all). Called alongside
 * seed_boot_modules_to_fs() at every site that seeds assets, so
 * assets get seeded correctly no matter which bootloader is active. */
void seed_embedded_assets_to_fs(void);

/* The "primary disk" backing TRPFS. Module-static so the pointer trpfs.c
 * holds onto stays valid for the lifetime of the OS.
 *
 * provision_disk() tries a real ATA disk first (for actual persistence
 * across reboots) and falls back to a RAM disk if no ATA hardware is
 * present (e.g. a VM with no HDD attached). g_using_disk reflects which
 * one ended up active, and is checked at boot by installer_try_automount(). */
int install_session_selected(void);
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

/* Non-blocking "any console input" poll shared by the installer's own
 * prompts and by sys_read(fd 0) for TRP programs: legacy PS/2, then USB
 * HID (VirtualBox's default keyboard), then raw COM1 (serial console /
 * automated tests). Returns 0 when nothing is pending. */
int console_getc_nb(void) {
    char c = keyboard_getc_nb();
    if (c) return (unsigned char)c;

    usb_hid_poll();
    c = usb_hid_getc_nb();
    if (c) return (unsigned char)c;

    uint8_t lsr;
    __asm__ volatile("inb %%dx, %0" : "=a"(lsr) : "d"((uint16_t)0x3FD));
    /* A machine with no COM1 at all (VirtualBox's default) reads 0xFF from
     * every unclaimed port: that has "data ready" set, so it used to look
     * like an endless stream of 0xFF keypresses. A real UART's line-status
     * register never has all 8 bits set (bit 7 = FIFO error only appears
     * with bit 0.. combos that don't read as 0xFF), so 0xFF == "no UART". */
    if (lsr != 0xFF && (lsr & 0x01)) {
        uint8_t sb;
        __asm__ volatile("inb %%dx, %0" : "=a"(sb) : "d"((uint16_t)0x3F8));
        if (sb == 0xFF || sb == 0x00) return 0;   /* line noise */
        return sb;
    }
    return 0;
}

static char installer_getc(void) {
    int c = console_getc_nb();
    if (c) return (char)c;
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
    char ip[16];
    char netmask[16];
    char gateway[16];
    char dns[16];
    int  network_configured;
} installer_account_t;

static void collect_account_info(installer_account_t *acct, int unattended) {
    if (unattended) {
        memcpy(acct->username, "user", 5);
        memcpy(acct->password, "password", 9);
        memcpy(acct->timezone, "UTC", 4);
        acct->ip[0] = acct->netmask[0] = acct->gateway[0] = acct->dns[0] = '\0';
        acct->network_configured = 0;
        return;
    }

    io_put_string("[4/6] OOBE setup\n");

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

/* ── Step 5: internet setup ───────────────────────────────────────────────── */

static void collect_network_info(installer_account_t *acct, int unattended) {
    if (unattended) return;

    io_put_string("[5/6] Internet setup\n");
    io_put_string("Configure a network connection now? (Y/N): ");

    char yn[4];
    read_line(yn, sizeof(yn), 0);
    if (!(yn[0] == 'y' || yn[0] == 'Y')) {
        io_put_string("Skipped - run 'ifconfig <ip> <netmask> <gateway> [dns]' later.\n");
        acct->network_configured = 0;
        return;
    }

    io_put_string("IP address: ");
    read_line(acct->ip, sizeof(acct->ip), 0);
    io_put_string("Netmask: ");
    read_line(acct->netmask, sizeof(acct->netmask), 0);
    io_put_string("Gateway: ");
    read_line(acct->gateway, sizeof(acct->gateway), 0);
    io_put_string("DNS (blank to skip): ");
    read_line(acct->dns, sizeof(acct->dns), 0);

    acct->network_configured = (acct->ip[0] && acct->netmask[0] && acct->gateway[0]);
    if (!acct->network_configured) {
        io_put_string("Incomplete network info - skipped. Use 'ifconfig' later.\n");
    }
}

/* ── Step 5: write config + flag, create home directory ──────────────────── */

static int finalize_install(const installer_account_t *acct, installer_errors_t *errs) {
    io_put_string("[6/6] Writing configuration...\n");

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

    int ok = 1;
    ok &= (syscfg_set("username", acct->username) == 0);
    ok &= (syscfg_set("timezone", acct->timezone) == 0);

    /* Display settings: only stamp a default if the key isn't already
     * present. finalize_install() runs twice per install (the silent
     * staging pass, then the interactive OOBE pass right after) - if
     * this unconditionally wrote defaults both times, a 'settings'
     * change made between those two boots would get clobbered. */
    char probe[16];
    if (!syscfg_get("resolution", probe, sizeof(probe)))
        ok &= (syscfg_set("resolution", "1024x768") == 0);
    if (!syscfg_get("text_color", probe, sizeof(probe)))
        ok &= (syscfg_set("text_color", "7") == 0);   /* VGA_LIGHT_GREY */
    if (!syscfg_get("bg_color", probe, sizeof(probe)))
        ok &= (syscfg_set("bg_color", "0") == 0);      /* VGA_BLACK */
    if (!syscfg_get("statusbar_color", probe, sizeof(probe)))
        ok &= (syscfg_set("statusbar_color", "7") == 0);

    ok &= (syscfg_set("storage", "ata") == 0);
    /* NOTE: storing a plaintext password is a placeholder for v1 - a
     * real build should hash this before writing it to disk. */
    ok &= (syscfg_set("password", acct->password) == 0);

    if (acct->network_configured) {
        ok &= (syscfg_set("net_ip", acct->ip) == 0);
        ok &= (syscfg_set("net_netmask", acct->netmask) == 0);
        ok &= (syscfg_set("net_gateway", acct->gateway) == 0);
        if (acct->dns[0]) ok &= (syscfg_set("net_dns", acct->dns) == 0);

        net_device_t *dev = net_get_device();
        uint32_t ip, nm, gw, dns = 0;
        if (dev && ip_parse(acct->ip, &ip) && ip_parse(acct->netmask, &nm) &&
            ip_parse(acct->gateway, &gw)) {
            dev->ip = ip; dev->netmask = nm; dev->gateway_ip = gw;
            if (acct->dns[0] && ip_parse(acct->dns, &dns)) dev->dns_ip = dns;
            io_put_string("      Network configured and applied.\n");
        }
    }

    /* This account/network pass is the interactive OOBE - once it's run,
     * the "please reboot to finish setup" flag from the silent staging
     * pass no longer applies. */
    ok &= (syscfg_set("oobe_pending", "0") == 0);

    if (!ok) err_add(errs, "Failed to write one or more settings to config.ini");

    fd_t fd = fs_open("/toriginal_os/installed.flag",
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

/* Embedded by kernel/boot/bootloader_blobs.s (built from
 * kernel/boot/loader/stage1.s / stage2.s+stage2_pm.s - see that
 * directory's build_loader.sh) - the exact bytes this OS's own
 * from-scratch bootloader is made of. write_bootloader_to_disk()
 * below is the only place that ever reads them. */
extern const uint8_t g_stage1_blob[];
extern const uint64_t g_stage1_blob_size;
extern const uint8_t g_stage2_blob[];
extern const uint64_t g_stage2_blob_size;

#ifndef PASS1_BUILD
/* The exact PASS 1 kernel.elf file (see Makefile/build.sh's TWO-PASS
 * BUILD sections), embedded whole by bootloader_blobs.s. This is the
 * real ELF file bytes - header, program headers, everything - not a
 * reconstruction from the running kernel's own loaded memory image
 * (which can't work: GRUB loads each PT_LOAD segment at its p_vaddr,
 * discarding the original file's byte layout - the file's own ELF/
 * program headers, at file offset 0, are never loaded into memory at
 * all, since the first PT_LOAD segment starts at file offset 0x1000,
 * not 0. There is no way to recover a valid ELF file from what's
 * sitting in RAM at runtime; it has to be embedded as real file bytes
 * at build time instead, which is exactly what the two-pass build
 * does.) stage2's own ELF loader (elf_load_segments in stage2_pm.s)
 * parses this exact blob's program headers at boot.
 *
 * Guarded by PASS1_BUILD (defined only when compiling PASS 1 - see
 * Makefile/build.sh) because these symbols are provided by
 * bootloader_blobs.o, which PASS 1 deliberately does NOT link (it IS
 * the thing that eventually gets embedded - it can't embed itself).
 * PASS 1 gets a no-op stub instead (below), since nothing in PASS 1
 * ever actually needs to run the real installer to completion; only
 * the finished PASS 2 kernel that ships to the user does. */
extern const uint8_t g_kernel_elf_blob[];
extern const uint64_t g_kernel_elf_blob_size;

/* Must match stage2.s's KERNEL_ENTRY_LINK_ADDR exactly - that value is
 * hardcoded into the already-assembled stage2 blob (baking the disk
 * LBA layout in is one thing, since installer.c controls where things
 * land; but the *entry address* is baked into stage2's machine code
 * itself, so if kernel64.ld's link address ever changes, stage2.s's
 * KERNEL_ENTRY_LINK_ADDR must be updated and rebuilt to match, or an
 * installed disk boots into the wrong address and crashes instantly.
 * This check catches that mismatch at install time - a build-system
 * inconsistency, not a bug a user does anything to cause - rather
 * than let it fail silently on the user's very next reboot. */
#define EXPECTED_KERNEL_ENTRY_ADDR 0x201000

/* Must match stage2.s's KERNEL_MAX_SECTORS exactly - the ceiling on
 * how much room stage2 will read from disk for the kernel copy.
 * FIX: was 1024 (512 KiB) - too small once the kernel started
 * embedding its own boot assets; see ata.h's
 * ATA_BOOT_RESERVED_SECTORS comment for the full story. */
#define KERNEL_MAX_SECTORS_FOR_BOOTLOADER 65536

/* Writes TRBL - Toriginal OS Runtime Boot Loader (see kernel/boot/
 * loader/ for the full design writeup) into the reserved boot region
 * at the start of the disk, replacing GRUB/the install CD as what
 * actually boots an installed system:
 *   sector 0          - stage1 (512-byte MBR)
 *   sectors 1-64      - stage2 (real-mode loader: A20, TRBL settings
 *                       read, VBE, kernel load, protected-mode ELF
 *                       parse, multiboot2 handoff)
 *   sectors 65-65600  - a spare copy of the exact kernel.elf currently
 *                       running, which is what stage2 loads and jumps
 *                       into on every subsequent boot (32 MiB budget -
 *                       see ata.h's ATA_BOOT_RESERVED_SECTORS comment)
 *   sector  70000/70001 - TRBL settings sector + backup (see
 *                       trbl_settings.h) - display/network/boot-flag
 *                       config TRBL's stage2 reads at every boot
 *
 * stage1.bin and stage2.bin are now written byte-identical on every
 * install - unlike the old design, nothing is patched into them at
 * install time. The only thing this function customizes per-install
 * is the TRBL settings sector, written via trbl_settings_write()
 * (the exact same function the running shell's `settings` command
 * calls later - see trbl_settings.c), seeded from the current
 * syscfg.ini resolution so a fresh install boots at the resolution
 * the user already configured. After install, the settings sector is
 * free-standing: the shell can rewrite it any time without ever
 * touching stage1/stage2 again. */
static int write_bootloader_to_disk(installer_errors_t *errs) {
    io_put_string("      Writing bootloader to disk...\n");

    uint64_t kernel_size = g_kernel_elf_blob_size;
    if (kernel_size < 64) {
        err_add(errs, "Embedded kernel image too small to contain a valid ELF header");
        return -1;
    }
    /* Elf64_Ehdr.e_entry is at byte offset 24, 8 bytes, little-endian -
     * only the low 32 bits are read (see stage2_pm.s's same reasoning:
     * every address this kernel ever links at fits well under 4 GiB). */
    uint32_t actual_entry;
    memcpy(&actual_entry, g_kernel_elf_blob + 24, 4);
    if (actual_entry != EXPECTED_KERNEL_ENTRY_ADDR) {
        err_add(errs, "Kernel entry point does not match what stage2.s expects - "
                       "rebuild the bootloader (build_loader.sh) after any "
                       "kernel64.ld link-address change before installing");
        serial_puts("[INSTALL] FATAL: kernel entry / stage2 KERNEL_ENTRY_LINK_ADDR mismatch.\n");
        return -1;
    }

    uint64_t stage2_sector_budget = 64ULL * 512ULL;
    if (g_stage2_blob_size > stage2_sector_budget) {
        err_add(errs, "stage2 image exceeds its 64-sector budget - rebuild "
                       "the loader after growing STAGE2_SECTORS/KERNEL_LOAD_LBA");
        return -1;
    }
    uint64_t kernel_sector_budget = (uint64_t)KERNEL_MAX_SECTORS_FOR_BOOTLOADER * 512ULL;
    if (kernel_size > kernel_sector_budget) {
        err_add(errs, "kernel.elf is too large for the reserved boot region - "
                       "grow KERNEL_MAX_SECTORS in stage2.s and rebuild");
        return -1;
    }

    uint8_t sector_buf[512];
    memset(sector_buf, 0, sizeof(sector_buf));
    memcpy(sector_buf, g_stage1_blob, (size_t)g_stage1_blob_size);

    /* Keep the partition table the partition manager wrote: stage1's own
     * table is just a placeholder entry (see stage1.s). Only a table with
     * at least one real partition at/after the boot-region boundary
     * counts - an old install's whole-disk placeholder (start LBA 0) does
     * not. The system partition is flagged active so BIOS boots it. */
    {
        uint8_t cur[512];
        if (ata_raw_read_sector(0, cur) == 0 && cur[510] == 0x55 && cur[511] == 0xAA) {
            mbr_t *cm = (mbr_t *)cur;
            int have_real = 0;
            for (int i = 0; i < 4; i++)
                if (cm->partitions[i].sector_count &&
                    cm->partitions[i].start_lba >= INSTALL_PART_ALIGN_LBA) have_real = 1;
            if (have_real) {
                memcpy(sector_buf + 446, cur + 446, 64);
                mbr_t *nm = (mbr_t *)sector_buf;
                int active = install_session_selected();
                for (int i = 0; i < 4; i++) {
                    nm->partitions[i].boot_indicator = (active == i) ? 0x80 : 0x00;
                }
                if (active < 0) {
                    for (int i = 0; i < 4; i++)
                        if (nm->partitions[i].partition_type == 0x83 &&
                            nm->partitions[i].sector_count) { nm->partitions[i].boot_indicator = 0x80; break; }
                }
                sector_buf[510] = 0x55; sector_buf[511] = 0xAA;
            }
        }
    }
    if (ata_raw_write_sector(0, sector_buf) != 0) {
        err_add(errs, "Failed to write stage1 to disk sector 0");
        return -1;
    }

    /* stage2 is now written byte-identical, unpatched - no per-install
     * customization happens to it anymore. */
    uint8_t *stage2_copy = (uint8_t *)kmalloc((size_t)stage2_sector_budget);
    if (!stage2_copy) {
        err_add(errs, "Out of memory building the stage2 disk image");
        return -1;
    }
    memset(stage2_copy, 0, (size_t)stage2_sector_budget);
    memcpy(stage2_copy, g_stage2_blob, (size_t)g_stage2_blob_size);

    uint64_t stage2_sectors = stage2_sector_budget / 512ULL;
    for (uint64_t i = 0; i < stage2_sectors; i++) {
        if (ata_raw_write_sector((uint32_t)(1 + i), stage2_copy + i * 512) != 0) {
            err_add(errs, "Failed to write stage2 to disk");
            kfree(stage2_copy);
            return -1;
        }
    }
    kfree(stage2_copy);

    /* Seed the TRBL settings sector from whatever resolution syscfg
     * already has on record (e.g. set pre-install, or a default),
     * via the exact same trbl_settings_write() the shell's `settings`
     * command uses post-install - no separate code path to drift out
     * of sync with it. */
    char res_str[16];
    syscfg_get_or("resolution", res_str, sizeof(res_str), "1024x768");
    uint16_t width = 1024, height = 768;
    const char *xpos = strchr(res_str, 'x');
    if (xpos) {
        uint32_t w = 0, h = 0;
        for (const char *p = res_str; p < xpos; p++) {
            if (*p < '0' || *p > '9') { w = 0; break; }
            w = w * 10 + (uint32_t)(*p - '0');
        }
        for (const char *p = xpos + 1; *p; p++) {
            if (*p < '0' || *p > '9') { h = 0; break; }
            h = h * 10 + (uint32_t)(*p - '0');
        }
        if (w > 0 && w <= 1920 && h > 0 && h <= 1080) {
            width = (uint16_t)w;
            height = (uint16_t)h;
        }
    }
    if (trbl_settings_set_resolution(width, height) != 0) {
        err_add(errs, "Failed to write TRBL settings sector - boot-time "
                       "resolution will fall back to the 1024x768 default");
        /* Not fatal - stage2.s degrades gracefully to its compiled-in
         * default when the settings sector is missing/invalid. */
    }

    /* First boot always starts at MAX: use every byte of whatever RAM
     * this machine actually has, re-detected fresh on every future
     * boot too - not a fixed number frozen at install time, so moving
     * the disk to different hardware later still uses all of it. A
     * user can narrow this later with the shell's `mem` command. */
    if (trbl_settings_set_memory(TRBL_MEM_MODE_MAX, 0) != 0) {
        err_add(errs, "Failed to write TRBL settings sector - boot-time "
                       "RAM allocation will fall back to the MAX default");
    }

    uint64_t kernel_sectors_needed = (kernel_size + 511ULL) / 512ULL;
    for (uint64_t i = 0; i < kernel_sectors_needed; i++) {
        uint64_t remaining = kernel_size - i * 512ULL;
        if (remaining >= 512ULL) {
            if (ata_raw_write_sector((uint32_t)(65 + i), g_kernel_elf_blob + i * 512) != 0) {
                err_add(errs, "Failed to write kernel image to disk");
                return -1;
            }
        } else {
            memset(sector_buf, 0, sizeof(sector_buf));
            memcpy(sector_buf, g_kernel_elf_blob + i * 512, (size_t)remaining);
            if (ata_raw_write_sector((uint32_t)(65 + i), sector_buf) != 0) {
                err_add(errs, "Failed to write final kernel sector to disk");
                return -1;
            }
        }
    }

    io_put_string("      Bootloader installed (");
    {
        char nbuf[12];
        int n = 0;
        uint64_t v = kernel_sectors_needed;
        if (v == 0) { nbuf[n++] = '0'; }
        char tmp[12]; int t = 0;
        while (v > 0) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
        while (t > 0) { nbuf[n++] = tmp[--t]; }
        nbuf[n] = '\0';
        io_put_string(nbuf);
    }
    io_put_string(" kernel sectors written).\n");

    return 0;
}
#else /* PASS1_BUILD */
/* PASS 1 stub: no blob to write yet (bootloader_blobs.o, which
 * provides g_kernel_elf_blob etc., is only linked into PASS 2 - see
 * the #ifndef PASS1_BUILD comment above). PASS 1 never actually needs
 * to install anything for real; its only job is to exist as a
 * byte-identical, ordinary kernel build for PASS 2 to embed. This
 * stub exists purely so installer_stage_files() below still compiles
 * and links in a PASS 1 build. */
static int write_bootloader_to_disk(installer_errors_t *errs) {
    (void)errs;
    serial_puts("[INSTALL] write_bootloader_to_disk() is a no-op stub in "
                "this PASS1_BUILD - this should never run outside the "
                "build system's own bootstrap step.\n");
    return -1;
}
#endif /* PASS1_BUILD */

/* Silent first-ever-boot pass: formats TRPFS, copies every file/asset
 * the OS ships with (wallpapers, start menu art, cursors, seed payload)
 * onto the disk, and stamps default settings - all with no keyboard
 * prompts, because the install disc is still in the drive at this
 * point and this pass runs unattended from kernel_init(). It does NOT
 * collect a username/password/network config: those need the disc
 * out (see the comment on installer_try_automount() for why), so it
 * leaves oobe_pending=1 for the next boot to pick up. */
static int installer_stage_files(installer_errors_t *errs) {
    io_put_string("\n==================================================\n");
    io_put_string("   TORIGINAL OS — FIRST BOOT: STAGING INSTALL\n");
    io_put_string("==================================================\n\n");

    if (provision_disk(errs) != 0) return -1;
    if (build_filesystem(errs) != 0) return -1;
    if (seed_install_payload(errs) != 0) return -1;

    seed_boot_modules_to_fs();
    seed_embedded_assets_to_fs();

    /* Placeholder account so config.ini / installed.flag exist and
     * installer_print_status()/settings reads work right away - the
     * real values get overwritten by the interactive OOBE next boot. */
    installer_account_t acct;
    memset(&acct, 0, sizeof(acct));
    memcpy(acct.username, "user", 5);
    memcpy(acct.timezone, "UTC", 4);
    finalize_install(&acct, errs);
    /* finalize_install() clears oobe_pending as its last step (it's also
     * used by the real OOBE path below) - re-set it here since this is
     * the staging pass, not the finished setup. */
    syscfg_set("oobe_pending", "1");

    /* Make the disk bootable on its own, now that config.ini (and
     * therefore the resolution stage2 needs to be patched with) exists. */
    write_bootloader_to_disk(errs);

    trpfs_sync();

    return (errs->count == 0) ? 0 : -1;
}

/* Interactive OOBE-only pass: runs on the boot *after* staging, once the
 * install disc has been removed and the disk-backed filesystem is the
 * only thing GRUB is booting from. Filesystem already exists (staged
 * above) - this only collects the real account + network config and
 * overwrites the placeholder values. */
static void installer_run_oobe(installer_errors_t *errs) {
    io_put_string("\n==================================================\n");
    io_put_string("        TORIGINAL OS SETUP / OOBE (TRPFS v1)\n");
    io_put_string("==================================================\n\n");

    installer_account_t acct;
    memset(&acct, 0, sizeof(acct));
    collect_account_info(&acct, 0);
    collect_network_info(&acct, 0);
    finalize_install(&acct, errs);

    err_print_all(errs);
    if (errs->count == 0) {
        io_put_string("Setup complete. Welcome to Toriginal OS.\n");
        serial_puts("[INSTALL] OOBE completed successfully.\n");
    } else {
        io_put_string("Setup finished with warnings/errors (see above).\n");
        serial_puts("[INSTALL] OOBE completed with errors.\n");
    }
}

/* Fully interactive, single-boot install+OOBE in one pass - used by the
 * manual 'install'/'setup'/'oobe' shell commands (run.c calls this
 * directly, disc-removal concerns don't apply since the user is already
 * sitting at a live shell driving it by hand). */
void installer_run(void) {
    installer_errors_t errs;
    memset(&errs, 0, sizeof(errs));

    io_put_string("\n==================================================\n");
    io_put_string("        TORIGINAL OS SETUP / OOBE (TRPFS v1)\n");
    io_put_string("==================================================\n\n");

    if (provision_disk(&errs) != 0) { err_print_all(&errs); return; }
    scan_existing_os(&errs); /* may halt internally if the user declines */
    if (build_filesystem(&errs) != 0) { err_print_all(&errs); return; }
    if (seed_install_payload(&errs) != 0) { err_print_all(&errs); return; }
    seed_boot_modules_to_fs();
    seed_embedded_assets_to_fs();

    installer_account_t acct;
    memset(&acct, 0, sizeof(acct));
    collect_account_info(&acct, 0);
    collect_network_info(&acct, 0);
    finalize_install(&acct, &errs);

    err_print_all(&errs);
    if (errs.count == 0) {
        io_put_string("Setup complete. Type 'status' to verify.\n");
        serial_puts("[INSTALL] Setup completed successfully.\n");
    } else {
        io_put_string("Installation finished with warnings/errors (see above).\n");
        serial_puts("[INSTALL] Completed with errors.\n");
    }
}

void installer_run_unattended(void) {
    installer_errors_t errs;
    memset(&errs, 0, sizeof(errs));
    installer_stage_files(&errs);
    err_print_all(&errs);
}

/* Halts with a message telling the user to eject the install disc and
 * reboot. Used once, right after the silent staging pass, so the next
 * boot (disc out, booting the on-disk install) lands on the real
 * interactive OOBE instead of re-running the unattended stager. This
 * never returns - same pattern as scan_existing_os()'s decline path. */
static void halt_for_reboot(const char *msg) {
    io_put_string("\n==================================================\n");
    io_put_string(msg);
    io_put_string("==================================================\n\n");
    serial_puts("[INSTALL] Halting for reboot: ");
    serial_puts(msg);
    __asm__ volatile("cli");
    for (;;) { __asm__ volatile("hlt"); }
}


/* ══════════════════════════════════════════════════════════════════════════
 *  TRP-driven install + OOBE
 *
 *  Boot 1 (live CD):   partitionmanager.trp  ->  kernel installs onto the
 *                      partition it picked  ->  "remove disc, restart".
 *  Boot 2 (from disk): teroobe.trp (username/password/terminal look/
 *                      network/...)  ->  kernel applies the settings  ->
 *                      "restart".
 *  Boot 3:             normal boot.
 *
 *  Both programs are ordinary ring-3 .trp files (tools/trp/), delivered
 *  to the live session as GRUB modules and copied onto the installed
 *  disk under /system/trp/ - nothing about them is baked into the kernel.
 *
 *  The kernel's main thread can't be resumed after a user process runs
 *  (the scheduler only saves contexts of processes), so each stage is
 *  driven by the previous program's exit: process_set_exit_hook().
 * ══════════════════════════════════════════════════════════════════════════ */

#define TRP_PARTMAN_PATH "/system/trp/partitionmanager.trp"
#define TRP_OOBE_PATH    "/system/trp/teroobe.trp"
#define SESS_LIVE_RAM_BYTES (16ULL * 1024ULL * 1024ULL)
#define SYSTEM_PART_MIN_SECTORS (128u * 2048u)      /* 128 MiB */
/* TRPFS keeps one allocation bit per 4 KiB block in the kernel heap
 * (48 MiB of virtual space), so cap the usable volume at 64 GiB
 * (a 2 MiB bitmap). A bigger partition is allowed; only its first 64 GiB
 * is used. The same cap is used when formatting and when mounting. */
#define SYSTEM_VOLUME_MAX_BYTES (64ULL * 1024ULL * 1024ULL * 1024ULL)
static uint64_t system_volume_bytes(uint32_t sectors) {
    uint64_t b = (uint64_t)sectors * 512ULL;
    return b > SYSTEM_VOLUME_MAX_BYTES ? SYSTEM_VOLUME_MAX_BYTES : b;
}

int seed_boot_modules_matching(const char *prefix);

typedef struct {
    int      active;          /* install-session flag (partition manager)   */
    int      target_slot;     /* MBR slot chosen for Toriginal OS, or -1    */
    uint64_t total_sectors;
    mbr_t    mbr;             /* last table committed via SYS_PART_WRITE    */
    int      mbr_valid;
    char     oobe[2048];
    uint32_t oobe_len;
    int      oobe_pending_apply;
    int      oobe_mode_session;   /* teroobe launched by the installer hook */
} sess_t;

static sess_t g_sess = { .active = 0, .target_slot = -1 };
static trpfs_blkdev_t g_live_dev;

int install_session_active(void)   { return g_sess.active; }
int install_session_selected(void) { return g_sess.target_slot; }

/* Validate a candidate partition table. Returns 0 if acceptable. */
static int part_validate(const mbr_t *m, uint64_t total, const char **why) {
    for (int i = 0; i < 4; i++) {
        const partition_entry_t *a = &m->partitions[i];
        if (a->sector_count == 0) continue;
        if (a->partition_type != 0x83 && a->partition_type != 0x07 &&
            a->partition_type != 0x0C) { *why = "unsupported partition type"; return -1; }
        if (a->start_lba < INSTALL_PART_ALIGN_LBA) { *why = "overlaps the boot region"; return -1; }
        if ((uint64_t)a->start_lba + a->sector_count > total) { *why = "extends past end of disk"; return -1; }
        for (int j = i + 1; j < 4; j++) {
            const partition_entry_t *b = &m->partitions[j];
            if (b->sector_count == 0) continue;
            uint64_t ae = (uint64_t)a->start_lba + a->sector_count;
            uint64_t be = (uint64_t)b->start_lba + b->sector_count;
            if (a->start_lba < be && b->start_lba < ae) { *why = "partitions overlap"; return -1; }
        }
    }
    return 0;
}

static void sess_load_disk_size(void) {
    uint64_t sec = 0;
    if (ata_drive_info(0, &sec, NULL, 0) == 0) g_sess.total_sectors = sec;
}

/* SYS_PART_WRITE backend. Keeps the existing bootstrap code in bytes
 * 0..445 (zeros on a blank disk - the bootloader install fills them in
 * later), replaces only the 4 table entries, forces the 0x55AA signature. */
int install_part_commit(const uint8_t *user_mbr) {
    if (!g_sess.active) return -1;
    if (!g_sess.total_sectors) sess_load_disk_size();
    if (!g_sess.total_sectors) return -1;

    mbr_t cand;
    memcpy(&cand, user_mbr, sizeof(cand));
    const char *why = "";
    if (part_validate(&cand, g_sess.total_sectors, &why) != 0) {
        serial_puts("[INSTALL] Rejected partition table: "); serial_puts(why); serial_puts("\n");
        return -1;
    }
    uint8_t sec[512];
    if (ata_raw_read_sector(0, sec) != 0) memset(sec, 0, sizeof(sec));
    mbr_t *out = (mbr_t *)sec;
    for (int i = 0; i < 4; i++) {
        out->partitions[i] = cand.partitions[i];
        out->partitions[i].boot_indicator = 0;
        out->partitions[i].start_chs[0] = 0xFE; out->partitions[i].start_chs[1] = 0xFF; out->partitions[i].start_chs[2] = 0xFF;
        out->partitions[i].end_chs[0]   = 0xFE; out->partitions[i].end_chs[1]   = 0xFF; out->partitions[i].end_chs[2]   = 0xFF;
    }
    out->boot_signature = 0xAA55;
    if (ata_raw_write_sector(0, sec) != 0) return -1;

    memcpy(&g_sess.mbr, out, sizeof(mbr_t));
    g_sess.mbr_valid = 1;
    if (g_sess.target_slot >= 0 && g_sess.mbr.partitions[g_sess.target_slot].sector_count == 0)
        g_sess.target_slot = -1;
    serial_puts("[INSTALL] Partition table written.\n");
    return 0;
}

int install_session_select(int slot) {
    if (!g_sess.active || !g_sess.mbr_valid) return -1;
    if (slot < 0 || slot > 3) return -1;
    const partition_entry_t *p = &g_sess.mbr.partitions[slot];
    if (p->sector_count < SYSTEM_PART_MIN_SECTORS) return -1;
    if (p->partition_type != 0x83) return -1;
    g_sess.target_slot = slot;
    return 0;
}

/* ── key=value OOBE submission ──────────────────────────────────────────── */

static int kv_next(const char **cur, const char *end, char *k, int kn, char *v, int vn) {
    const char *p = *cur;
    while (p < end) {
        const char *ls = p;
        while (p < end && *p != '\n') p++;
        const char *le = p;
        if (p < end) p++;
        const char *eq = ls;
        while (eq < le && *eq != '=') eq++;
        if (eq == le || eq == ls) continue;
        int kl = (int)(eq - ls), vl = (int)(le - eq - 1);
        if (kl >= kn) kl = kn - 1;
        if (vl >= vn) vl = vn - 1;
        if (vl < 0) vl = 0;
        memcpy(k, ls, (size_t)kl); k[kl] = '\0';
        memcpy(v, eq + 1, (size_t)vl); v[vl] = '\0';
        while (vl > 0 && (v[vl - 1] == '\r' || v[vl - 1] == ' ')) v[--vl] = '\0';
        *cur = p;
        return 1;
    }
    *cur = p;
    return 0;
}

static int parse_u(const char *s, uint32_t *out) {
    if (!*s) return 0;
    uint32_t v = 0;
    while (*s) { if (*s < '0' || *s > '9') return 0; v = v * 10 + (uint32_t)(*s - '0'); s++; }
    *out = v; return 1;
}

static int parse_ip4(const char *s, uint8_t out[4]) {
    for (int i = 0; i < 4; i++) {
        uint32_t v = 0; int digits = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (uint32_t)(*s - '0'); s++; digits++; if (v > 255) return 0; }
        if (!digits) return 0;
        out[i] = (uint8_t)v;
        if (i < 3) { if (*s != '.') return 0; s++; }
    }
    return *s == '\0';
}

static void ip4_str(const uint8_t ip[4], char *out) {
    int o = 0;
    for (int q = 0; q < 4; q++) {
        uint8_t x = ip[q];
        if (x >= 100) out[o++] = (char)('0' + x / 100);
        if (x >= 10)  out[o++] = (char)('0' + (x / 10) % 10);
        out[o++] = (char)('0' + x % 10);
        if (q < 3) out[o++] = '.';
    }
    out[o] = '\0';
}

static int name_ok(const char *s) {
    int n = 0;
    for (; *s; s++, n++) {
        char c = *s;
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-')) return 0;
    }
    return n >= 1 && n <= 31;
}

/* Apply a submitted OOBE key=value blob: account + terminal look +
 * network + boot behaviour. Persists to config.ini (syscfg) and the TRBL
 * settings sector so everything takes effect from the next boot on. */
static int oobe_apply(const char *buf, uint32_t len) {
    installer_account_t acct;
    memset(&acct, 0, sizeof(acct));
    memcpy(acct.username, "user", 5);
    memcpy(acct.timezone, "UTC", 4);

    uint8_t net_ip[4] = {0}, net_mask[4] = {0}, net_gw[4] = {0}, net_dns[4] = {0};
    int net_mode = -1, have_ip = 0, have_mask = 0, have_gw = 0, have_dns = 0;
    int res_w = 0, res_h = 0;
    int mem_mode = -1; uint32_t ram_kb = 0;
    int flag_verbose = -1, flag_safe = -1;
    char text_c[8] = "", bg_c[8] = "", sb_c[8] = "", hostname[32] = "";

    const char *cur = buf, *end = buf + len;
    char k[32], v[96];
    while (kv_next(&cur, end, k, sizeof(k), v, sizeof(v))) {
        uint32_t n;
        if (!strcmp(k, "username"))      { if (name_ok(v)) { strncpy(acct.username, v, 31); acct.username[31] = 0; } }
        else if (!strcmp(k, "password")) { strncpy(acct.password, v, 31); acct.password[31] = 0; }
        else if (!strcmp(k, "timezone")) { strncpy(acct.timezone, v, 15); acct.timezone[15] = 0; }
        else if (!strcmp(k, "hostname")) { if (name_ok(v)) { strncpy(hostname, v, 31); hostname[31] = 0; } }
        else if (!strcmp(k, "text_color") && parse_u(v, &n) && n < 16) strncpy(text_c, v, 7);
        else if (!strcmp(k, "bg_color") && parse_u(v, &n) && n < 16)   strncpy(bg_c, v, 7);
        else if (!strcmp(k, "statusbar_color") && parse_u(v, &n) && n < 16) strncpy(sb_c, v, 7);
        else if (!strcmp(k, "resolution")) {
            uint32_t w = 0, h = 0; const char *x = strchr(v, 'x');
            if (x) { char a[8]; int al = (int)(x - v); if (al > 0 && al < 8) { memcpy(a, v, (size_t)al); a[al] = 0;
                     if (parse_u(a, &w) && parse_u(x + 1, &h) && w >= 640 && w <= 1920 && h >= 480 && h <= 1080) { res_w = (int)w; res_h = (int)h; } } }
        }
        else if (!strcmp(k, "net_mode")) {
            if (!strcmp(v, "off")) net_mode = TRBL_NET_OFF;
            else if (!strcmp(v, "dhcp")) net_mode = TRBL_NET_DHCP;
            else if (!strcmp(v, "static")) net_mode = TRBL_NET_STATIC;
        }
        else if (!strcmp(k, "net_ip"))   have_ip   = parse_ip4(v, net_ip);
        else if (!strcmp(k, "net_mask")) have_mask = parse_ip4(v, net_mask);
        else if (!strcmp(k, "net_gw"))   have_gw   = parse_ip4(v, net_gw);
        else if (!strcmp(k, "net_dns"))  have_dns  = parse_ip4(v, net_dns);
        else if (!strcmp(k, "mem_mode")) {
            if (!strcmp(v, "max")) mem_mode = TRBL_MEM_MODE_MAX;
            else if (!strcmp(v, "custom")) mem_mode = TRBL_MEM_MODE_CUSTOM;
            else if (!strcmp(v, "least")) mem_mode = TRBL_MEM_MODE_LEAST;
        }
        else if (!strcmp(k, "ram_kb") && parse_u(v, &n)) ram_kb = n;
        else if (!strcmp(k, "boot_verbose") && parse_u(v, &n)) flag_verbose = n ? 1 : 0;
        else if (!strcmp(k, "boot_safe") && parse_u(v, &n)) flag_safe = n ? 1 : 0;
    }

    installer_errors_t errs; memset(&errs, 0, sizeof(errs));
    int static_ok = (net_mode == TRBL_NET_STATIC && have_ip && have_mask && have_gw);
    if (static_ok) {
        ip4_str(net_ip, acct.ip);
        ip4_str(net_mask, acct.netmask);
        ip4_str(net_gw, acct.gateway);
        if (have_dns) ip4_str(net_dns, acct.dns);
        acct.network_configured = 1;
    }

    finalize_install(&acct, &errs);

    if (text_c[0]) syscfg_set("text_color", text_c);
    if (bg_c[0])   syscfg_set("bg_color", bg_c);
    if (sb_c[0])   syscfg_set("statusbar_color", sb_c);
    if (hostname[0]) syscfg_set("hostname", hostname);
    if (net_mode == TRBL_NET_OFF)  syscfg_set("net_mode", "off");
    if (net_mode == TRBL_NET_DHCP) syscfg_set("net_mode", "dhcp");
    if (net_mode == TRBL_NET_STATIC) syscfg_set("net_mode", "static");
    if (res_w) {
        char rs[16]; int o = 0; char t[8]; int tl;
        uint32_t vals[2] = { (uint32_t)res_w, (uint32_t)res_h };
        for (int q = 0; q < 2; q++) {
            tl = 0; uint32_t x = vals[q]; char r[8]; int rl = 0;
            while (x) { r[rl++] = (char)('0' + x % 10); x /= 10; }
            while (rl) t[tl++] = r[--rl];
            for (int z = 0; z < tl; z++) rs[o++] = t[z];
            if (q == 0) rs[o++] = 'x';
        }
        rs[o] = 0;
        syscfg_set("resolution", rs);
        trbl_settings_set_resolution((uint16_t)res_w, (uint16_t)res_h);
    }
    if (net_mode == TRBL_NET_OFF)  trbl_settings_set_network(TRBL_NET_OFF, 0, 0, 0, 0);
    if (net_mode == TRBL_NET_DHCP) trbl_settings_set_network(TRBL_NET_DHCP, 0, 0, 0, 0);
    if (static_ok) trbl_settings_set_network(TRBL_NET_STATIC, net_ip, net_mask, net_gw, have_dns ? net_dns : net_ip);
    if (mem_mode >= 0) trbl_settings_set_memory((uint8_t)mem_mode, mem_mode == TRBL_MEM_MODE_CUSTOM ? ram_kb : 0);
    if (flag_verbose >= 0 || flag_safe >= 0) {
        trbl_settings_t ts;
        if (trbl_settings_read(&ts) == 0) {
            if (flag_verbose >= 0) { if (flag_verbose) ts.boot_flags |= TRBL_FLAG_VERBOSE; else ts.boot_flags &= ~TRBL_FLAG_VERBOSE; }
            if (flag_safe >= 0)    { if (flag_safe) ts.boot_flags |= TRBL_FLAG_SAFE_MODE; else ts.boot_flags &= ~TRBL_FLAG_SAFE_MODE; }
            trbl_settings_write(&ts);
        }
    }
    if (errs.count != 0) {
        /* The account could not be created: leave oobe_pending=1 so the
         * setup runs again instead of booting an OS nobody can log in to. */
        serial_puts("[OOBE] Applying settings failed - setup will run again.\n");
        trpfs_sync();
        return -1;
    }
    syscfg_set("oobe_pending", "0");
    trpfs_sync();
    serial_puts("[OOBE] Settings applied.\n");
    return 0;
}

int install_session_oobe_submit(const char *text, uint32_t len) {
    if (len == 0 || len >= sizeof(g_sess.oobe)) return -1;
    if (!g_sess.oobe_mode_session) return -1;
    memcpy(g_sess.oobe, text, len);
    g_sess.oobe[len] = '\0';
    g_sess.oobe_len = len;
    /* Only the installer-launched setup (boot 2) may submit: any other
     * program calling this would be able to create accounts / change
     * boot settings with no authentication. */
    if (!g_sess.oobe_mode_session) return -1;
    g_sess.oobe_pending_apply = 1;
    return 0;
}

/* ── stage plumbing ─────────────────────────────────────────────────────── */

static void sess_park(int from_syscall) __attribute__((noreturn));
static void sess_park(int from_syscall) {
    /* 1 = reached via the SYS_EXIT stub (undo its swapgs), 2 = reached from
     * the previous program's exit by whichever path it took. */
    if (from_syscall == 2) from_syscall = process_exit_context();
    /* Entered from a syscall (the previous program's exit): the syscall
     * stub already did swapgs, so undo that before letting the timer
     * scheduler iretq into the next ring-3 program. */
    if (from_syscall) __asm__ volatile("swapgs");
    __asm__ volatile("sti");
    for (;;) __asm__ volatile("hlt");
}

static void sess_restart_prompt(const char *msg) __attribute__((noreturn));
static void sess_restart_prompt(const char *msg) {
    __asm__ volatile("sti");
    io_put_string("\n==================================================\n");
    io_put_string(msg);
    io_put_string("  Press ENTER to restart.\n");
    io_put_string("==================================================\n");
    serial_puts("[INSTALL] Waiting for ENTER to restart.\n");
    for (;;) {
        int c = console_getc_nb();
        if (c == '\n' || c == '\r') break;
        __asm__ volatile("hlt");
    }
    io_put_string("Restarting...\n");
    __asm__ volatile("cli");
    for (volatile int i = 0; i < 200000; i++) { }
    __asm__ volatile("outb %%al, $0x64" :: "a"((uint8_t)0xFE));
    for (;;) __asm__ volatile("hlt");
}

void scheduler_init(void);
void syscall_init(void);

static int sess_launch(const char *path, void (*hook)(int)) {
    /* The installer runs inside kernel_init() step [7/8], BEFORE the
     * process/scheduler/syscall subsystems are brought up (they come
     * right after the filesystem step). A ring-3 program needs all
     * three - without EFER.SCE set its very first `syscall` raised #UD -
     * so bring them up now; the session never returns to kernel_init. */
    static int subsystems_up = 0;
    if (!subsystems_up) {
        process_init();
        scheduler_init();
        syscall_init();
        subsystems_up = 1;
    }
    process_t *p = process_create(path, 1);
    if (!p) return -1;
    process_set_exit_hook(hook);
    if (process_exec(p->pid, path, NULL) != 0) { process_set_exit_hook(NULL); return -1; }
    return 0;
}

/* Boot 2 stage 2: teroobe.trp exited. */
static void sess_oobe_done(int status) {
    (void)status;
    if (g_sess.oobe_pending_apply) {
        g_sess.oobe_pending_apply = 0;
        if (oobe_apply(g_sess.oobe, g_sess.oobe_len) == 0)
            sess_restart_prompt("  Setup complete. Toriginal OS is ready.\n");
        io_put_string("\nCould not create the account - starting setup again...\n");
    }
    /* The program quit without submitting anything: run it again - the
     * account has to exist before the OS can be used. */
    io_put_string("\nSetup was not completed - starting it again...\n");
    if (sess_launch(TRP_OOBE_PATH, sess_oobe_done) == 0) sess_park(2);
    sess_restart_prompt("  Could not restart setup.\n");
}

/* Boot 2: disk is mounted and flagged oobe_pending. */
static int sess_run_oobe(void) {
    static inode_t st;
    if (fs_stat(TRP_OOBE_PATH, &st) != 0) return -1;
    g_sess.oobe_mode_session = 1;
    io_clear_screen();
    if (sess_launch(TRP_OOBE_PATH, sess_oobe_done) != 0) return -1;
    sess_park(0);
}

/* Boot 1 stage 2: partitionmanager.trp exited -> install. */
static void sess_partman_done(int status);

static void sess_do_install(void) {
    installer_errors_t errs; memset(&errs, 0, sizeof(errs));
    const partition_entry_t *p = &g_sess.mbr.partitions[g_sess.target_slot];

    io_clear_screen();
    io_put_string("==================================================\n");
    io_put_string("   TORIGINAL OS - INSTALLING\n");
    io_put_string("==================================================\n\n");

    uint64_t bytes = system_volume_bytes(p->sector_count);
    io_put_string("[1/4] Preparing the system partition...\n");
    trpfs_blkdev_t *dev = ata_init_blkdev_partition(0, p->start_lba, bytes);
    if (!dev) { io_put_string("      No ATA disk.\n"); sess_restart_prompt("  Install failed: no disk.\n"); }
    g_disk = dev; g_using_real_disk = 1;

    io_put_string("[2/4] Formatting TRPFS volume \"TORIGINALOS\" (can take a minute on big disks)...\n");
    if (build_filesystem(&errs) != 0) {
        err_print_all(&errs); sess_restart_prompt("  Install failed while formatting.\n");
    }
    io_put_string("[3/4] Copying system files...\n");
    seed_install_payload(&errs);
    seed_boot_modules_to_fs();
    seed_embedded_assets_to_fs();

    installer_account_t acct; memset(&acct, 0, sizeof(acct));
    memcpy(acct.username, "user", 5); memcpy(acct.timezone, "UTC", 4);
    finalize_install(&acct, &errs);
    syscfg_set("oobe_pending", "1");

    io_put_string("[4/4] Installing the bootloader...\n");
    write_bootloader_to_disk(&errs);
    trpfs_sync();

    err_print_all(&errs);
    if (errs.count) sess_restart_prompt("  Install finished with errors (see above).\n");
    sess_restart_prompt("  Installation complete.\n"
                        "  Remove the install disc first. On the next start\n"
                        "  Toriginal OS asks for your username and password.\n");
}

static void sess_partman_done(int status) {
    if (status == 1) {
        g_sess.active = 0;
        sess_restart_prompt("  Installation cancelled. Nothing was installed.\n");
    }
    if (status != 0 || g_sess.target_slot < 0 || !g_sess.mbr_valid) {
        io_put_string("\nNo install target was chosen - starting the partition manager again...\n");
        if (sess_launch(TRP_PARTMAN_PATH, sess_partman_done) == 0) sess_park(2);
        sess_restart_prompt("  Could not restart the partition manager.\n");
    }
    g_sess.active = 0;       /* no more raw partition writes from here on */
    sess_do_install();
    for (;;) __asm__ volatile("hlt");
}

/* Boot 1 entry: blank disk + live CD. Returns -1 (so the caller falls
 * back to the old unattended stager) if the TRP programs aren't there. */
static int sess_begin_install(void) {
    sess_load_disk_size();
    if (!g_sess.total_sectors) return -1;

    if (trpfs_ramdisk_init(&g_live_dev, SESS_LIVE_RAM_BYTES) != 0) return -1;
    if (trpfs_format(&g_live_dev, "LIVE") != 0 || trpfs_mount(&g_live_dev) != 0) return -1;
    if (seed_boot_modules_matching("/system/trp/") < 2) {
        serial_puts("[INSTALL] install-time TRP programs missing from the disc - using the unattended installer.\n");
        return -1;
    }
    static inode_t st;
    if (fs_stat(TRP_PARTMAN_PATH, &st) != 0) return -1;

    g_sess.active = 1; g_sess.target_slot = -1; g_sess.mbr_valid = 0;
    {   /* Start from whatever table is on the disk (if any). */
        uint8_t sec[512];
        if (ata_raw_read_sector(0, sec) == 0 && sec[510] == 0x55 && sec[511] == 0xAA) {
            memcpy(&g_sess.mbr, sec, 512);
            const char *why;
            if (part_validate(&g_sess.mbr, g_sess.total_sectors, &why) == 0) g_sess.mbr_valid = 1;
        }
    }
    io_clear_screen();
    serial_puts("[INSTALL] Live installer: launching partitionmanager.trp\n");
    if (sess_launch(TRP_PARTMAN_PATH, sess_partman_done) != 0) { g_sess.active = 0; return -1; }
    sess_park(0);
}

/* Find the system partition from the on-disk MBR: active, type 0x83,
 * starting at/after the boot region. Fills slot/start/sectors. */
static int find_system_partition(uint32_t *start, uint32_t *sectors) {
    uint8_t sec[512];
    if (ata_raw_read_sector(0, sec) != 0) return -1;
    if (sec[510] != 0x55 || sec[511] != 0xAA) return -1;
    const mbr_t *m = (const mbr_t *)sec;
    for (int i = 0; i < 4; i++) {
        const partition_entry_t *p = &m->partitions[i];
        if (p->partition_type == 0x83 && p->boot_indicator == 0x80 &&
            p->start_lba >= INSTALL_PART_ALIGN_LBA && p->sector_count >= 64) {
            *start = p->start_lba; *sectors = p->sector_count; return 0;
        }
    }
    return -1;
}

/* ── Boot-time auto-mount ─────────────────────────────────────────────────
 * Called once from kernel_init(), before the shell starts.
 *
 * Three cases:
 *   1. No valid TRPFS volume yet -> this is the very first boot (still
 *      running off the install disc). Silently stage the whole OS onto
 *      the ATA disk (format, copy every file/asset, default settings),
 *      then halt and tell the user to remove the disc and reboot. No
 *      account prompts here - see installer_stage_files()'s comment.
 *   2. Valid volume, but oobe_pending=1 -> this is the second boot, now
 *      running off the disk-backed install with the disc out. Run the
 *      real interactive OOBE (account + network) and continue into the
 *      shell once it's done.
 *   3. Valid volume, oobe_pending=0 (or unset, for installs made before
 *      this flow existed) -> ordinary boot, just mount and go. */
int installer_try_automount(void) {
    trpfs_blkdev_t *ata = NULL;
    uint32_t pstart = 0, psectors = 0;
    if (find_system_partition(&pstart, &psectors) == 0) {
        ata = ata_init_blkdev_partition(0, pstart, system_volume_bytes(psectors));
        serial_puts("[INSTALL] System partition found in the MBR.\n");
    }
    if (!ata) ata = ata_init_blkdev(INSTALLER_DISK_BYTES);   /* pre-partition-manager installs */
    if (!ata) {
        serial_puts("[INSTALL] No ATA disk detected - install requires a "
                     "real ATA/IDE disk for persistence.\n");
        return -1;
    }

    g_disk = ata;
    g_using_real_disk = 1;

    if (trpfs_mount(g_disk) == 0) {
        char pending[8];
        syscfg_get_or("oobe_pending", pending, sizeof(pending), "0");
        if (strcmp(pending, "1") == 0) {
            serial_puts("[INSTALL] Existing filesystem found, OOBE pending - "
                        "running teroobe.trp.\n");
            if (sess_run_oobe() == 0) return 0;     /* never returns on success */
            serial_puts("[INSTALL] teroobe.trp missing - falling back to the built-in OOBE.\n");
            installer_errors_t errs;
            memset(&errs, 0, sizeof(errs));
            installer_run_oobe(&errs);
            return 0;
        }
        serial_puts("[INSTALL] Existing filesystem found on disk - auto-mounted.\n");
        return 0;
    }

    serial_puts("[INSTALL] ATA disk present but no valid filesystem found - "
                "starting the live installer.\n");

    /* Boot 1: partition manager TRP, then install (never returns). */
    sess_begin_install();

    /* TRP programs unavailable: old unattended path. */
    serial_puts("[INSTALL] Falling back to the unattended installer.\n");
    g_disk = ata_init_blkdev(INSTALLER_DISK_BYTES);
    installer_run_unattended();

    if (trpfs_mount(g_disk) == 0) {
        serial_puts("[INSTALL] First-boot staging completed - prompting for reboot.\n");
        halt_for_reboot(
            "  Initial setup complete.\n"
            "  Please REMOVE the install disc/ISO now, then reboot.\n"
            "  Toriginal OS will finish setup (OOBE) on the next boot.\n");
    }

    serial_puts("[INSTALL] First-boot staging did not leave a valid filesystem "
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
