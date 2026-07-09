/* =============================================================================
 * kernel.c - Toriginal OS freeNT v1.0 kernel entry point
 * ============================================================================= */

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
#include "process.h"
#include "shell.h"
#include "installer.h"
#include "graphics.h"
#include "graphics_3d.h"
#include "gfx_terminal.h"
#include "cursor.h"

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

#define MB2_TAG_END   0u
#define MB2_TAG_MMAP  6u
#define MB2_TAG_FB    8u
#define MB2_MAX_ITER  64u

typedef struct { uint32_t type; uint32_t size; }
    __attribute__((packed)) mb2_tag_t;

typedef struct { uint32_t type; uint32_t size;
                 uint32_t entry_size; uint32_t entry_version; }
    __attribute__((packed)) mb2_tag_mmap_t;

typedef struct { uint64_t base_addr; uint64_t length;
                 uint32_t type; uint32_t reserved; }
    __attribute__((packed)) mb2_mmap_entry_t;

/* Multiboot2 framebuffer info tag (type=8). Layout is fixed by the
 * multiboot2 spec: 64-bit physical address first (so the struct needs
 * 8-byte alignment, hence packed + explicit field order matching the
 * spec exactly — do not reorder these fields). */
typedef struct {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;   /* 0=indexed, 1=RGB, 2=EGA text */
    uint8_t  reserved;
    /* color info fields follow but we don't need them for RGB mode */
} __attribute__((packed)) mb2_tag_fb_t;

/* Filled in by parse_multiboot(), consumed by graphics_init() via the
 * mb_fb_*() accessors declared in kernel.h. fb_found stays 0 if GRUB
 * didn't hand us a usable RGB framebuffer (e.g. old GRUB, or it
 * silently fell back to EGA text) so callers can detect "no graphics
 * available" instead of drawing into garbage memory. Kept static +
 * accessor-wrapped (rather than plain externs) so graphics.c can't
 * accidentally write to these and drift out of sync with what was
 * actually parsed from the multiboot struct. */
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

        if (tag->type == MB2_TAG_MMAP) {
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
                /* type==1 is packed-RGB pixels — the only mode our
                 * graphics.c pixel-plotting code understands. Reject
                 * indexed (0) and EGA-text (2) rather than pretending
                 * we have a framebuffer we can't actually draw into. */
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
        }

        uint32_t next = (tag->size + 7u) & ~7u;
        if (next == 0) break;
        ptr += next;
    }
}

static volatile int kernel_initialized = 0;

static void kernel_init(uint32_t mb_info_phys) {
    if (kernel_initialized) return;

    kprint("\n");
    kprint("  ================================================\n");
    kprint("       Toriginal OS  -  freeNT v1.1\n");
    kprint("       Made by warqwert\n");
    kprint("  ================================================\n\n");

    parse_multiboot(mb_info_phys);
    kprint("\n");

    kprint("[1/7] Memory init...\n");
    memory_init(mb_info_phys);
    kprint("[1/7] Memory OK\n");

    /* graphics_init() moved here deliberately - it used to run before
     * memory_init(), which was harmless *today* only because
     * graphics_init() itself never calls kmalloc(). But gfx3d_init()
     * (added alongside the 3D rasterizer) DOES call kmalloc() for its
     * depth buffer, and gfx2d_surface_create() calls kmalloc() for
     * off-screen surfaces - both would run before heap.c's
     * mm_init_heap() has ever executed if this stayed above
     * memory_init(). That's a real "works by accident until someone
     * allocates a surface" hazard, not a hypothetical one - moving
     * both graphics inits below memory_init() removes it entirely
     * rather than relying on callers to remember the ordering
     * constraint. */
    kprint("[2/7] Graphics init...\n");
    if (graphics_init() == 0) {
        /* vga_write_dec prints straight to the VGA text-mode buffer,
         * which is fine here since graphics_init() succeeding doesn't
         * retire vga_write() as a serial/debug channel — only the
         * later terminal stage repoints user-visible output at the
         * framebuffer. */
        kprint("[2/7] Framebuffer graphics online (");
        vga_write_dec(g_framebuffer.width);
        vga_write("x");
        vga_write_dec(g_framebuffer.height);
        vga_write(", ");
        vga_write_dec(g_framebuffer.depth);
        vga_write("bpp)\n");
        serial_write("[GFX] Framebuffer graphics online\n");

        if (gfx3d_init() == 0) {
            kprint("[2/7] 3D rasterizer online (depth buffer allocated)\n");
        } else {
            kprint("[2/7] 3D rasterizer unavailable (depth buffer alloc failed)\n");
        }

        if (gterm_init() == 0) {
            /* From this point on, every vga_putc()/vga_write() call
             * anywhere in the kernel (including the rest of this very
             * boot log) transparently routes through the graphical
             * terminal instead of real VGA text memory - see vga.c's
             * gterm_is_active() checks. Nothing below this line needs
             * to change to "start using" the graphical terminal. */
            kprint("[2/7] Graphical terminal online (64x24 grid, 16x32 px/cell)\n");
        } else {
            kprint("[2/7] Graphical terminal unavailable - staying on VGA text mode\n");
        }
    } else {
        kprint("[2/7] No usable framebuffer - staying in VGA text mode\n");
        kprint("[2/7] 3D rasterizer unavailable (no framebuffer)\n");
    }

    kprint("[3/7] IDT init...\n");
    idt_init();
    kprint("[3/7] IDT OK\n");

    kprint("[4/7] PIT init (100 Hz)...\n");
    pit_init(100);
    kprint("[4/7] PIT OK\n");

    kprint("[5/7] Keyboard init...\n");
    keyboard_wire_idt();
    kprint("[5/7] Keyboard OK\n");

    kprint("[6/7] Mouse init...\n");
    mouse_wire_init();
    kprint("[6/7] Mouse OK\n");

    kprint("[7/7] Filesystem + process init...\n");
    fs_init();
    if (installer_try_automount() == 0) {
        kprint("[7/7] Persistent filesystem mounted (data restored from disk)\n");
        vga_set_statusbar_enabled(1);

        /* Cursor PNG assets live on the persistent disk (see
         * cursor.h) - only attempt to load them once a filesystem is
         * actually mounted. If no persistent disk exists yet (the
         * "run 'install' to set one up" branch below), cursor_draw()
         * falls back to a small procedural pointer shape rather than
         * being unable to render a cursor at all - see cursor.c. */
        if (cursor_assets_init() == 0) {
            kprint("[7/7] Cursor assets loaded from /sys/gui/assets/\n");
        } else {
            kprint("[7/7] Cursor assets not found - using fallback cursor shape\n");
        }
    } else {
        kprint("[7/7] No persistent disk found - run 'install' to set one up\n");
        kprint("[7/7] Cursor assets unavailable until a disk is installed - using fallback cursor shape\n");
    }
    process_init();
    scheduler_init();
    syscall_init();
    kprint("[7/7] Subsystems OK\n");

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
    serial_write("[boot] *** KERNEL FULLY BOOTED - freeNT v1.1 READY ***\n");
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