
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
    vga_write(s);
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
void seed_boot_modules_to_fs(void) {
    for (int i = 0; i < g_mb_module_count; i++) {
        mb2_module_entry_t *m = &g_mb_modules[i];
        if (m->phys_end <= m->phys_start) continue;

        /* mkdir -p the destination's parent - modules can target any
         * path, not just /system/gui/wallpapers/, so this can't
         * assume a fixed directory the way wallpaper.c's own
         * ensure_dir() does for its config file. */
        char tmp[128];
        size_t len = strlen(m->dest_path);
        if (len == 0 || len >= sizeof(tmp)) continue;
        memcpy(tmp, m->dest_path, len + 1);
        for (size_t j = 1; j < len; j++) {
            if (tmp[j] != '/') continue;
            tmp[j] = '\0';
            inode_t st;
            if (fs_stat(tmp, &st) != 0) {
                fs_mkdir(tmp, FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X);
            }
            tmp[j] = '/';
        }

        fd_t fd = fs_open(m->dest_path, O_WRONLY | O_CREAT | O_TRUNC,
                          FILE_PERM_OWNER_R | FILE_PERM_OWNER_W);
        if (fd < 0) {
            early_print("[MOD] Failed to open destination for boot module: ");
            early_print(m->dest_path);
            early_print("\n");
            continue;
        }

        const uint8_t *src = (const uint8_t *)(uintptr_t)m->phys_start;
        size_t total = (size_t)(m->phys_end - m->phys_start);
        ssize_t written = fs_write(fd, src, total);
        fs_close(fd);

        if (written != (ssize_t)total) {
            early_print("[MOD] Short write seeding boot module: ");
            early_print(m->dest_path);
            early_print("\n");
        } else {
            early_print("[MOD] Seeded ");
            early_print(m->dest_path);
            early_print("\n");
        }
    }
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

        if (cursor_assets_init() == 0) {
            kprint("[7/8] Cursor assets loaded from /sys/gui/assets/\n");
        } else {
            kprint("[7/8] Cursor assets not found - using fallback cursor shape\n");
        }
    } else {
        kprint("[7/8] No persistent disk found - run 'install' to set one up\n");
        kprint("[7/8] Cursor assets unavailable until a disk is installed - using fallback cursor shape\n");
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
    else
        kprint("[8/8] Network OK (run 'ifconfig' to configure, then 'ping'/'trpm install')\n");

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
