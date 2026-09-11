
#include "kernel.h"
#include "types.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "panic.h"
#include "fs.h"
#include "string.h"
#include "process.h"
#include "shell.h"
#include "installer.h"
#include "graphics.h"
#include "graphics_3d.h"
#include "gfx_terminal.h"
#include "cursor.h"
#include "net.h"
#include "tcp.h"
#include "rtl8139.h"
#include "acpi.h"
#include "apic.h"
#include "desktop.h"
#include "ps2.h"
#include "uhci.h"
#include "trbl_settings.h"
#include "pmm.h"
#include "png.h"
#include "graphics_2d.h"
#include "heap.h"

void interrupts_init(void);
void syscall_init(void);
void keyboard_wire_idt(void);
void mouse_wire_init(void);
void scheduler_init(void);

static void early_print(const char *s) {
    serial_write(s);
}

static void kprint(const char *s) {
    serial_write(s);
    /* Route through the graphical terminal once it's up, not the
     * legacy VGA text buffer - by the time TRBL/GRUB hand off to the
     * kernel, the display is already in a VBE linear-framebuffer
     * graphics mode (set before kernel_main ever runs), so writes to
     * the old 0xB8000 text buffer are never scanned out to the
     * screen at all. gterm_is_active() correctly reports false for
     * every kprint() call before gterm_init() runs (a handful of
     * early boot lines - banner, "[1/8] Memory init...", etc.),
     * which still fall back to vga_write() same as before; every
     * message from graphics coming online onward - the vast
     * majority of boot output, and the entire interactive shell -
     * now actually reaches the screen. */
    if (gterm_is_active()) {
        gterm_write(s);
    } else {
        vga_write(s);
    }
}

static const char g_hex_digits[] = "0123456789ABCDEF";

static void early_print_hex(uint64_t v) {
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++)
        buf[2 + i] = g_hex_digits[(v >> (60 - i*4)) & 0xF];
    buf[18] = '\0';
    serial_write(buf);
}

#define MB2_TAG_END       0u
#define MB2_TAG_CMDLINE   1u
#define MB2_TAG_MODULE     3u
#define MB2_TAG_MMAP      6u
#define MB2_TAG_FB        8u
#define MB2_TAG_ACPI_OLD 14u
#define MB2_TAG_ACPI_NEW 15u
#define MB2_MAX_ITER  64u

static uint32_t g_mb_rsdp_old_phys = 0;
static uint32_t g_mb_rsdp_new_phys = 0;

/* Loaded once during kernel_init() (step [8/8], right after disk/
 * filesystem bring-up guarantees drive 0 has been probed - see
 * trbl_settings_read()'s self-detecting ata_raw_read_sector() call
 * either way). Holds whatever the TRBL boot sector says was
 * configured (resolution TRBL's stage2 already acted on before the
 * kernel even started, plus network mode/boot flags the kernel
 * itself can act on). Not static - trbl_settings.c and any future
 * caller (e.g. an automatic DHCP bring-up) can reference it directly
 * instead of re-reading the disk every time. */
trbl_settings_t g_trbl_settings;

typedef struct { uint32_t type; uint32_t size; }
    __attribute__((packed)) mb2_tag_t;

typedef struct { uint32_t type; uint32_t size;
                 uint32_t mod_start; uint32_t mod_end;
                 char     cmdline[]; }
    __attribute__((packed)) mb2_tag_module_t;

/* GRUB's `module2 /path/to/file.png /system/gui/wallpapers/file.png`
 * passes the destination TRPFS path as the module's cmdline string -
 * see grub.cfg. boot_assets_seed() (kernel.c, called once per boot
 * from kernel_init) writes each module's raw bytes straight to that
 * path, so wallpaper.c/cursor.c never need to know modules exist at
 * all - they just find real files already sitting on TRPFS. */
#define MB2_MAX_MODULES 16
typedef struct {
    uint32_t phys_start;
    uint32_t phys_end;
    char     dest_path[128];
} mb2_module_entry_t;

static mb2_module_entry_t g_mb_modules[MB2_MAX_MODULES];
static int                g_mb_module_count = 0;

typedef struct { uint32_t type; uint32_t size;
                 uint32_t entry_size; uint32_t entry_version; }
    __attribute__((packed)) mb2_tag_mmap_t;

typedef struct { uint64_t base_addr; uint64_t length;
                 uint32_t type; uint32_t reserved; }
    __attribute__((packed)) mb2_mmap_entry_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  reserved;

} __attribute__((packed)) mb2_tag_fb_t;

static int      g_mb_fb_found  = 0;
static uint64_t g_mb_fb_addr   = 0;
static uint32_t g_mb_fb_pitch  = 0;
static uint32_t g_mb_fb_width  = 0;
static uint32_t g_mb_fb_height = 0;
static uint8_t  g_mb_fb_bpp    = 0;
static uint8_t  g_mb_fb_type   = 0;

int      mb_fb_found(void)  { return g_mb_fb_found; }
uint64_t mb_fb_addr(void)   { return g_mb_fb_addr; }
uint32_t mb_fb_pitch(void)  { return g_mb_fb_pitch; }
uint32_t mb_fb_width(void)  { return g_mb_fb_width; }
uint32_t mb_fb_height(void) { return g_mb_fb_height; }
uint8_t  mb_fb_bpp(void)    { return g_mb_fb_bpp; }

/* Set from the multiboot2 command line tag (GRUB's "toriginal_OS
 * (GUI)" menu entry passes "gui" as a kernel argument) - lets
 * kernel_main() launch straight into the graphical desktop instead of
 * the text shell, without needing a separate kernel binary or any
 * persisted "first boot" state on disk. */
static int g_boot_gui_requested = 0;
int kernel_boot_gui_requested(void) { return g_boot_gui_requested; }

static void parse_multiboot(uint32_t mb_info_phys) {
    if (!mb_info_phys) { early_print("[MEM] No multiboot info pointer\n"); return; }

    uint8_t *ptr = (uint8_t *)(uintptr_t)mb_info_phys + 8;
    uint32_t total_size = *(uint32_t *)(uintptr_t)mb_info_phys;

    if (total_size < 8 || total_size > 4 * 1024 * 1024) {
        early_print("[MEM] Multiboot info size invalid - skipping mmap\n");
        return;
    }

    uint8_t *struct_end = (uint8_t *)(uintptr_t)mb_info_phys + total_size;

    for (uint32_t iter = 0; iter < MB2_MAX_ITER; iter++) {
        if (ptr + sizeof(mb2_tag_t) > struct_end) break;

        mb2_tag_t *tag = (mb2_tag_t *)ptr;
        if (tag->type == MB2_TAG_END) break;
        if (tag->size < 8) break;

        if (tag->type == MB2_TAG_CMDLINE) {
            const char *s = (const char *)(ptr + 8);
            const uint8_t *limit = ptr + tag->size;
            /* Small bounded substring search for "gui" - the whole
             * tag is already validated to be within struct_end, and
             * we never read past `limit`, so a malformed/missing NUL
             * terminator can't run us off the end of the multiboot
             * info structure. */
            for (const char *p = s; (const uint8_t *)p + 3 <= limit; p++) {
                if (p[0]=='g' && p[1]=='u' && p[2]=='i') {
                    g_boot_gui_requested = 1;
                    break;
                }
            }
            early_print("[BOOT] Command line: ");
            early_print(s);
            early_print("\n");
        } else if (tag->type == MB2_TAG_MMAP) {
            mb2_tag_mmap_t *mt = (mb2_tag_mmap_t *)ptr;
            uint32_t es = mt->entry_size;

            if (es < 24 || es > 256 || (es & 7)) {
                early_print("[MEM] Bad mmap entry_size - skipping\n");
            } else {
                uint8_t *ep  = ptr + sizeof(mb2_tag_mmap_t);
                uint8_t *end = ptr + tag->size;
                early_print("[MEM] Memory map:\n");
                while (ep + es <= end) {
                    mb2_mmap_entry_t *e = (mb2_mmap_entry_t *)ep;
                    if (e->type == 1) {
                        early_print("[MEM]   avail  base=");
                        early_print_hex(e->base_addr);
                        early_print("  len=");
                        early_print_hex(e->length);
                        early_print("\n");
                    }
                    ep += es;
                }
            }
        } else if (tag->type == MB2_TAG_FB) {
            if (tag->size >= sizeof(mb2_tag_fb_t)) {
                mb2_tag_fb_t *fb = (mb2_tag_fb_t *)ptr;

                if (fb->framebuffer_type == 1 && fb->framebuffer_bpp >= 24) {
                    g_mb_fb_found  = 1;
                    g_mb_fb_addr   = fb->framebuffer_addr;
                    g_mb_fb_pitch  = fb->framebuffer_pitch;
                    g_mb_fb_width  = fb->framebuffer_width;
                    g_mb_fb_height = fb->framebuffer_height;
                    g_mb_fb_bpp    = fb->framebuffer_bpp;
                    g_mb_fb_type   = fb->framebuffer_type;

                    early_print("[FB] Framebuffer found: ");
                    early_print_hex(fb->framebuffer_addr);
                    early_print(" w=");
                    early_print_hex(fb->framebuffer_width);
                    early_print(" h=");
                    early_print_hex(fb->framebuffer_height);
                    early_print(" bpp=");
                    early_print_hex(fb->framebuffer_bpp);
                    early_print("\n");
                } else {
                    early_print("[FB] Framebuffer tag present but not usable RGB mode - skipping\n");
                }
            }
        } else if (tag->type == MB2_TAG_MODULE) {
            if (tag->size >= sizeof(mb2_tag_module_t) && g_mb_module_count < MB2_MAX_MODULES) {
                mb2_tag_module_t *mod = (mb2_tag_module_t *)ptr;
                const char *cmd = mod->cmdline;
                const uint8_t *limit = ptr + tag->size;

                mb2_module_entry_t *e = &g_mb_modules[g_mb_module_count];
                e->phys_start = mod->mod_start;
                e->phys_end   = mod->mod_end;

                size_t n = 0;
                while (n < sizeof(e->dest_path) - 1 &&
                       (const uint8_t *)(cmd + n) < limit && cmd[n] != '\0') {
                    e->dest_path[n] = cmd[n];
                    n++;
                }
                e->dest_path[n] = '\0';

                if (e->dest_path[0] == '/') {
                    g_mb_module_count++;
                    early_print("[MOD] Boot module -> ");
                    early_print(e->dest_path);
                    early_print("\n");
                }
            }
        } else if (tag->type == MB2_TAG_ACPI_OLD) {

            g_mb_rsdp_old_phys = (uint32_t)(uintptr_t)(ptr + 8);
            early_print("[ACPI] RSDP v1 tag found\n");
        } else if (tag->type == MB2_TAG_ACPI_NEW) {
            g_mb_rsdp_new_phys = (uint32_t)(uintptr_t)(ptr + 8);
            early_print("[ACPI] RSDP v2 tag found\n");
        }

        uint32_t next = (tag->size + 7u) & ~7u;
        if (next == 0) break;
        ptr += next;
    }
}

static volatile int kernel_initialized = 0;

/* Writes every parsed multiboot module's raw bytes to the TRPFS path
 * given by its cmdline (see parse_multiboot()'s MB2_TAG_MODULE case
 * and grub.cfg's `module2 <iso-file> <dest-path>` lines). This is the
 * ONLY place actual asset bytes (wallpapers, cursor images, whatever
 * else ships this way later) get onto the filesystem - nothing is
 * baked into the kernel binary as a C array. Re-runs every boot
 * (idempotent O_TRUNC overwrite) so the disk always matches whatever
 * the booted ISO actually shipped. Must run after fs_init() +
 * installer_try_automount() have given us a mounted, writable TRPFS. */
/* Non-static: also called directly by installer.c's installer_run_internal()
 * right after a fresh install builds the filesystem, so boot-module
 * assets (wallpapers, start menu images, cursor, ...) land as part of
 * the install payload immediately - not only on the NEXT boot's
 * automount path below. See installer.c's call site for why: without
 * this, a fresh `install` would leave the desktop asset-less until a
 * reboot, which is a real, avoidable rough edge. */
/* Shared by both seed_boot_modules_to_fs() (GRUB module2-delivered
 * assets - only ever populated when GRUB booted the install disc)
 * and seed_embedded_assets_to_fs() below (assets baked directly into
 * kernel.elf, available on every boot regardless of which
 * bootloader ran - see that function's own comment for why this
 * second path exists at all). mkdir -p's dest_path's parent, then
 * writes total bytes from src to it. */
static void write_bytes_to_fs_path(const char *dest_path, const uint8_t *src, size_t total) {
    char tmp[128];
    size_t len = strlen(dest_path);
    if (len == 0 || len >= sizeof(tmp)) return;
    memcpy(tmp, dest_path, len + 1);
    for (size_t j = 1; j < len; j++) {
        if (tmp[j] != '/') continue;
        tmp[j] = '\0';
        inode_t st;
        if (fs_stat(tmp, &st) != 0) {
            fs_mkdir(tmp, FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X);
        }
        tmp[j] = '/';
    }

    fd_t fd = fs_open(dest_path, O_WRONLY | O_CREAT | O_TRUNC,
                      FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
    if (fd < 0) {
        early_print("[ASSET] Failed to open destination: ");
        early_print(dest_path);
        early_print("\n");
        return;
    }

    ssize_t written = fs_write(fd, src, total);
    fs_close(fd);

    if (written != (ssize_t)total) {
        early_print("[ASSET] Short write seeding: ");
        early_print(dest_path);
        early_print("\n");
    } else {
        early_print("[ASSET] Seeded ");
        early_print(dest_path);
        early_print("\n");
    }
}

void seed_boot_modules_to_fs(void) {
    for (int i = 0; i < g_mb_module_count; i++) {
        mb2_module_entry_t *m = &g_mb_modules[i];
        if (m->phys_end <= m->phys_start) continue;
        write_bytes_to_fs_path(m->dest_path, (const uint8_t *)(uintptr_t)m->phys_start,
                                (size_t)(m->phys_end - m->phys_start));
    }
}

/* ── Embedded asset seeding (kernel/boot/asset_blob.s) ─────────────────────
 * seed_boot_modules_to_fs() above only has anything to seed when GRUB
 * booted the install disc - GRUB's grub.cfg has one `module2` line
 * per wallpaper/start-menu-image/cursor file (see the Makefile's
 * dynamic ASSET_*_NAMES discovery), and GRUB turns each into a
 * multiboot2 MB2_TAG_MODULE the kernel receives. TRBL - this OS's own
 * bootloader, which replaces GRUB for every boot after the first
 * install - has no concept of "modules" at all; it only ever loads
 * the kernel ELF itself. So on any TRBL-only boot (which is every
 * boot after the disk is installed), g_mb_module_count is always 0
 * and seed_boot_modules_to_fs() silently does nothing - assets never
 * get (re-)seeded, no matter how many times the automount path below
 * calls it.
 *
 * This is the real fix: the exact same asset files, discovered the
 * same dynamic way (see build_asset_blob.py, which walks the
 * wallpapers/startmenu/cursor folders exactly like the Makefile's
 * ASSET_*_NAMES does), get packed into one flat archive and baked
 * directly into kernel.elf via .incbin (kernel/boot/asset_blob.s) -
 * the same technique bootloader_blobs.s already uses for TRBL's own
 * stage1/stage2. Since the assets now live inside the kernel binary
 * itself, seeding them works identically whether GRUB or TRBL booted
 * this kernel - no module delivery mechanism required at all. */
typedef struct {
    char     path[128];
    uint32_t data_offset;
    uint32_t data_len;
} __attribute__((packed)) asset_blob_entry_t;

extern const uint8_t g_asset_blob[];
extern const uint64_t g_asset_blob_size;

void seed_embedded_assets_to_fs(void) {
    if (g_asset_blob_size < 8) return;
    if (g_asset_blob[0] != 'T' || g_asset_blob[1] != 'R' ||
        g_asset_blob[2] != 'A' || g_asset_blob[3] != 'S') {
        early_print("[ASSET] Embedded asset blob has bad magic - skipping\n");
        return;
    }
    uint32_t count;
    memcpy(&count, g_asset_blob + 4, 4);

    uint64_t table_bytes = (uint64_t)count * sizeof(asset_blob_entry_t);
    if (8 + table_bytes > g_asset_blob_size) {
        early_print("[ASSET] Embedded asset blob table exceeds blob size - skipping\n");
        return;
    }

    const asset_blob_entry_t *table = (const asset_blob_entry_t *)(g_asset_blob + 8);
    for (uint32_t i = 0; i < count; i++) {
        const asset_blob_entry_t *e = &table[i];
        if ((uint64_t)e->data_offset + e->data_len > g_asset_blob_size) {
            early_print("[ASSET] Embedded asset entry out of range, skipping one file\n");
            continue;
        }
        /* path[] has no guaranteed null terminator if a source
         * filename's full destination path ever hit exactly 128
         * bytes - force one so strlen() in write_bytes_to_fs_path()
         * can't run past the entry into the next one. */
        char path[129];
        memcpy(path, e->path, 128);
        path[128] = '\0';
        write_bytes_to_fs_path(path, g_asset_blob + e->data_offset, e->data_len);
    }
}

/* ── Boot logo (kernel/boot/trbl_logo_blob.s) ───────────────────────────────
 * Decodes the embedded TRBL logo PNG and blits it to the top-left
 * corner of the screen, once graphics is confirmed online - a real
 * end-to-end exercise of the PNG decoder, surface allocation, and
 * alpha-compositing blit, using actual boot-time data, before
 * anything else ever draws to the framebuffer. Not fatal if it
 * fails for any reason (corrupt embed, decode error, OOM) - a
 * missing boot logo is cosmetic, never worth halting boot over. */
extern const uint8_t g_trbl_logo_png[];
extern const uint64_t g_trbl_logo_png_size;

static void show_boot_logo(void) {
    png_image_t decoded = {0};
    png_result_t r = png_decode(g_trbl_logo_png, (size_t)g_trbl_logo_png_size, &decoded);
    if (r != PNG_OK || !decoded.pixels) {
        early_print("[LOGO] Boot logo decode failed - skipping\n");
        return;
    }

    gfx2d_surface_t surf = gfx2d_surface_create(decoded.width, decoded.height);
    if (!surf.pixels) {
        early_print("[LOGO] Boot logo surface allocation failed - skipping\n");
        png_free(&decoded);
        return;
    }

    uint8_t ch = decoded.channels;
    for (uint32_t y = 0; y < decoded.height; y++) {
        const uint8_t *row = decoded.pixels + (size_t)y * decoded.width * ch;
        for (uint32_t x = 0; x < decoded.width; x++) {
            const uint8_t *px = row + (size_t)x * ch;
            color_t c = (ch == 4)
                ? graphics_argb(px[3], px[0], px[1], px[2])
                : graphics_argb(255,   px[0], px[1], px[2]);
            gfx2d_surface_set_pixel(&surf, x, y, c);
        }
    }

    /* Small margin off the true corner so it doesn't sit flush
     * against the screen edge. */
    gfx2d_blit_alpha(&surf, 8, 8);

    gfx2d_surface_destroy(&surf);
    png_free(&decoded);
    early_print("[LOGO] Boot logo drawn - graphics pipeline test OK\n");
}

static void kernel_init(uint32_t mb_info_phys) {
    if (kernel_initialized) return;

    kprint("\n");
    kprint("  ================================================\n");
    kprint("       Toriginal OS  -  freeNT v1.1\n");
    kprint("       Made by warqwert\n");
    kprint("  ================================================\n\n");

    parse_multiboot(mb_info_phys);
    kprint("\n");

    kprint("[1/8] Memory init...\n");
    memory_init(mb_info_phys);
    kprint("[1/8] Memory OK\n");

    kprint("[2/8] Graphics init...\n");
    if (graphics_init() == 0) {

        kprint("[2/8] Framebuffer graphics online (");
        vga_write_dec(g_framebuffer.width);
        vga_write("x");
        vga_write_dec(g_framebuffer.height);
        vga_write(", ");
        vga_write_dec(g_framebuffer.depth);
        vga_write("bpp)\n");
        serial_write("[GFX] Framebuffer graphics online\n");

        if (gfx3d_init() == 0) {
            kprint("[2/8] 3D rasterizer online (depth buffer allocated)\n");
        } else {
            kprint("[2/8] 3D rasterizer unavailable (depth buffer alloc failed)\n");
        }

        if (gterm_init() == 0) {

            kprint("[2/8] Graphical terminal online (64x24 grid, 16x32 px/cell)\n");
        } else {
            kprint("[2/8] Graphical terminal unavailable - staying on VGA text mode\n");
        }

        show_boot_logo();
    } else {

        kprint("[2/8] FATAL: no usable linear framebuffer from bootloader\n");
        kprint("[2/8] 3D rasterizer unavailable (no framebuffer)\n");
        serial_write("[GFX] FATAL: framebuffer required, none usable - halting\n");
        for (;;) __asm__ volatile("hlt");
    }

    kprint("[3/8] IDT init...\n");
    idt_init();
    kprint("[3/8] IDT OK\n");

    acpi_init(g_mb_rsdp_old_phys, g_mb_rsdp_new_phys);
    apic_init();
    if (apic_available()) {
        kprint("[3/8] I/O APIC available - using APIC IRQ routing\n");
    } else {
        kprint("[3/8] I/O APIC unavailable - using legacy PIC (unchanged behavior)\n");
    }

    kprint("[4/8] PIT init (100 Hz)...\n");
    pit_init(100);
    if (apic_available()) {
        apic_route_irq(0, 0x20);
        kprint("[4/8] PIT IRQ0 routed via I/O APIC\n");
    }
    kprint("[4/8] PIT OK\n");

    kprint("[5/8] PS/2 controller init...\n");
    ps2_controller_init();
    kprint("[5/8] PS/2 controller OK\n");

    kprint("[5/8] Keyboard init...\n");
    keyboard_wire_idt();
    kprint("[5/8] Keyboard OK\n");

    kprint("[6/8] Mouse init...\n");
    mouse_wire_init();
    kprint("[6/8] Mouse OK\n");

    kprint("[7/8] Filesystem + process init...\n");
    fs_init();
    if (installer_try_automount() == 0) {
        kprint("[7/8] Persistent filesystem mounted (data restored from disk)\n");
        vga_set_statusbar_enabled(1);

        seed_boot_modules_to_fs();
        seed_embedded_assets_to_fs();

        if (cursor_assets_init() == 0) {
            kprint("[7/8] Cursor assets loaded from /sys/gui/assets/\n");
        } else {
            kprint("[7/8] Cursor assets not found - using fallback cursor shape\n");
        }
    } else {
        kprint("[7/8] No persistent disk found - run 'install' to set one up\n");
        kprint("[7/8] Cursor assets unavailable until a disk is installed - using fallback cursor shape\n");
    }

    /* Apply any saved RAM allocation choice (the shell's `mem`
     * command, or the MAX default a fresh install seeds - see
     * installer.c's write_bootloader_to_disk()). Can't do this back in
     * memory_init()/pmm_init() - that runs in step [1/8], before ATA
     * exists, so trbl_settings_read()'s ata_raw_read_sector() call
     * would have nothing to read from yet. Until this line runs the
     * OS is simply uncapped (MAX), which is a safe default either way. */
    if (trbl_settings_read(&g_trbl_settings)) {
        if (pmm_apply_ram_config(g_trbl_settings.mem_mode, g_trbl_settings.ram_limit_kb) == 0) {
            kprint("[7/8] RAM allocation applied from saved settings ("); 
            vga_write_dec(pmm_get_ram_limit_kb() / 1024);
            kprint(" MB of "); vga_write_dec(pmm_get_total_ram_kb() / 1024); kprint(" MB detected)\n");
        }
    }

    process_init();
    scheduler_init();
    syscall_init();
    kprint("[7/8] Subsystems OK\n");

    kprint("[8/8] Network init...\n");
    net_init();
    tcp_init();
    if (!rtl8139_probe_and_init())
        kprint("[8/8] No supported NIC found — networking unavailable\n");
    else {
        kprint("[8/8] Network OK (run 'ifconfig' to configure, then 'ping'/'trpm install')\n");

        /* TRBL boot-time network config (see include/trbl_settings.h,
         * set via `settings network` in the shell) is read here and
         * logged, but not yet wired to an automatic DHCP/static
         * bring-up call - net.c doesn't have a DHCP client yet, and
         * ifconfig's static-assign path expects to be driven
         * interactively. Wiring "TRBL_NET_DHCP -> call the DHCP
         * client automatically at boot" is real follow-up work, not
         * done here - this only makes the saved intent visible so
         * it's obvious what's configured and what still needs a
         * manual `ifconfig`/`dhclient`-equivalent call to act on it. */
        if (trbl_settings_read(&g_trbl_settings)) {
            switch (g_trbl_settings.net_mode) {
                case TRBL_NET_DHCP:
                    kprint("[8/8] TRBL settings request DHCP at boot - not yet "
                           "automatic, run 'ifconfig' to bring the link up\n");
                    break;
                case TRBL_NET_STATIC:
                    kprint("[8/8] TRBL settings request a static boot IP - not yet "
                           "automatic, run 'ifconfig' to apply it\n");
                    break;
                case TRBL_NET_OFF:
                default:
                    break;
            }
        }
    }

    kprint("\n[BOOT] freeNT ready. Type 'help' for commands.\n\n");
    kernel_initialized = 1;
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    serial_init();
    vga_init();
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();

    if (multiboot_magic != 0x36D76289) {
        vga_write("FATAL: Not booted via Multiboot2!\n");
        serial_write("FATAL: bad multiboot magic\n");
        for (;;) __asm__ volatile("hlt");
    }

    serial_write("[boot] Multiboot2 OK\n");

    kernel_init(multiboot_info);

    __asm__ volatile("sti");

    serial_write("[boot] Interrupts enabled\n");

    uhci_init();

    serial_write("[boot] *** KERNEL FULLY BOOTED - freeNT v1.1 READY ***\n");

    if (kernel_boot_gui_requested()) {
        serial_write("[boot] GUI boot requested via GRUB - launching desktop\n");
        kprint("[BOOT] Starting graphical desktop (Esc returns to the shell)...\n");
        desktop_run();
    }

    serial_write("[boot] Shell starting - type commands below\n");
    serial_write("os~$ ");

    kernel_os_shell();

    for (;;) __asm__ volatile("hlt");
}

void kernel_panic(const char *reason) {
    __asm__ volatile("cli");
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_clear();
    vga_write("\n  !!! KERNEL PANIC !!!\n\n  ");
    vga_write(reason);
    vga_write("\n\n  System halted.\n");
    serial_write("\nKERNEL PANIC: ");
    serial_write(reason);
    serial_write("\n");
    for (;;) __asm__ volatile("hlt");
}
