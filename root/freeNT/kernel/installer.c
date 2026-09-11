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
    /* Was only checking legacy PS/2 (keyboard_getc_nb) and raw serial
     * - missing the usb_hid_poll()/usb_hid_getc_nb() pair that
     * kernel/shell.c's normal shell loop already does. Any USB
     * keyboard (VirtualBox's default emulated HID keyboard, not
     * legacy PS/2) produced no input here at all - the OOBE prompts
     * would print but every keypress went nowhere. */
    char c = keyboard_getc_nb();
    if (c) return c;

    usb_hid_poll();
    c = usb_hid_getc_nb();
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
    trpfs_blkdev_t *ata = ata_init_blkdev(INSTALLER_DISK_BYTES);
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
                        "running interactive setup.\n");
            installer_errors_t errs;
            memset(&errs, 0, sizeof(errs));
            installer_run_oobe(&errs);
            return 0;
        }
        serial_puts("[INSTALL] Existing filesystem found on disk - auto-mounted.\n");
        return 0;
    }

    serial_puts("[INSTALL] ATA disk present but no valid filesystem found - "
                "starting first-boot staging.\n");
    installer_run_unattended();

    if (trpfs_mount(g_disk) == 0) {
        serial_puts("[INSTALL] First-boot staging completed - prompting for reboot.\n");
        halt_for_reboot(
            "  Initial setup complete.\n"
            "  Please REMOVE the install disc/ISO now, then reboot.\n"
            "  Toriginal OS will finish setup (OOBE) on the next boot.\n");
        /* unreachable - halt_for_reboot() never returns */
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
