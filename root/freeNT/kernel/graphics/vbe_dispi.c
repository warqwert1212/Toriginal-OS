/* =============================================================================
 * VBE_DISPI.C — runtime display mode switching via the Bochs dispi
 * interface. See vbe_dispi.h for the full rationale.
 * ============================================================================= */

#include "vbe_dispi.h"
#include "graphics_core.h"
#include "gfx_terminal.h"
#include "pci.h"
#include "port.h"
#include "heap.h"
#include "string.h"
#include "serial.h"

/* ── Fixed dispi I/O ports + register indices (Bochs/QEMU standard) ──────── */
#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

#define VBE_DISPI_INDEX_ID           0
#define VBE_DISPI_INDEX_XRES         1
#define VBE_DISPI_INDEX_YRES         2
#define VBE_DISPI_INDEX_BPP          3
#define VBE_DISPI_INDEX_ENABLE       4
#define VBE_DISPI_INDEX_BANK         5
#define VBE_DISPI_INDEX_VIRT_WIDTH   6
#define VBE_DISPI_INDEX_VIRT_HEIGHT  7
#define VBE_DISPI_INDEX_X_OFFSET     8
#define VBE_DISPI_INDEX_Y_OFFSET     9

#define VBE_DISPI_DISABLED  0x00
#define VBE_DISPI_ENABLED   0x01
#define VBE_DISPI_LFB_ENABLED 0x40
#define VBE_DISPI_NOCLEARMEM  0x80

/* Any BOCHS-compatible ID in this range is acceptable - the spec adds
 * new IDs over time (0xB0C0..0xB0C5 seen in the wild across Bochs/
 * QEMU/VirtualBox versions) without breaking the register layout. */
#define VBE_DISPI_ID_MIN 0xB0C0
#define VBE_DISPI_ID_MAX 0xB0C5

/* Known PCI (vendor,device) pairs for the display adapters that expose
 * this interface, used only to find the LFB physical base via BAR0 -
 * the dispi ports themselves are fixed regardless of which of these
 * is actually present. */
typedef struct { uint16_t vendor; uint16_t device; } vbe_pci_id_t;
static const vbe_pci_id_t VBE_KNOWN_IDS[] = {
    { 0x80EE, 0xBEEF }, /* VirtualBox VBoxVGA / VBoxSVGA */
    { 0x1234, 0x1111 }, /* QEMU / Bochs standard VGA     */
};

static void dispi_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t dispi_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

int vbe_dispi_available(void) {
    uint16_t id = dispi_read(VBE_DISPI_INDEX_ID);
    return (id >= VBE_DISPI_ID_MIN && id <= VBE_DISPI_ID_MAX);
}

/* Finds the display adapter's LFB physical base via its PCI BAR0.
 * Returns 0 if no known display adapter is found on the bus (caller
 * falls back to whatever multiboot already reported, which is valid
 * for the *boot* mode but not guaranteed to still be right after a
 * mode switch on every implementation - so this is the preferred
 * path whenever a PCI match succeeds). */
static uint64_t find_lfb_base_via_pci(void) {
    pci_device_t devices[64];
    int count = pci_scan(devices, 64);

    for (unsigned k = 0; k < sizeof(VBE_KNOWN_IDS) / sizeof(VBE_KNOWN_IDS[0]); k++) {
        pci_device_t dev;
        if (pci_find(devices, count, VBE_KNOWN_IDS[k].vendor, VBE_KNOWN_IDS[k].device, &dev)) {
            /* BAR0 low bit 0 = 0 means memory-mapped; mask off the
             * low 4 bits (type/prefetchable flags) to get the base. */
            uint32_t bar0 = dev.bar[0];
            if ((bar0 & 0x1) == 0) {
                return (uint64_t)(bar0 & 0xFFFFFFF0u);
            }
        }
    }
    return 0;
}

/* NOTE on mapping: boot64.s identity-maps the entire low 4 GiB up
 * front using 2 MiB huge pages (PDPT[0..3] -> 4 page directories of
 * 2 MiB PDEs, see boot64.s's zero/fill loop) - there is no 4-level
 * 4 KiB page table under it. Any LFB physical address this driver
 * finds (whether from a PCI BAR or from mb_fb_addr()) is therefore
 * already mapped and readable/writable with no extra work: calling
 * the 4-level mm_map_page() here would misinterpret those huge-page
 * PDEs as pointers to a next-level table and corrupt the address
 * space, so this driver deliberately does NOT touch paging at all. */

static void revert_to_previous_mode(void) {
    /* If graphics was never up before this call (e.g. still in plain
     * VGA text mode), g_framebuffer.width/height are 0 - fall back to
     * the one resolution this codebase has always shipped with
     * (boot64.s's original request tag) rather than writing 0x0. */
    uint16_t rw = g_framebuffer.width  ? (uint16_t)g_framebuffer.width  : 1024;
    uint16_t rh = g_framebuffer.height ? (uint16_t)g_framebuffer.height : 768;
    dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    dispi_write(VBE_DISPI_INDEX_XRES, rw);
    dispi_write(VBE_DISPI_INDEX_YRES, rh);
    dispi_write(VBE_DISPI_INDEX_BPP, VBE_DISPI_MAX_BPP);
    dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
}

int vbe_dispi_set_mode(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return -1;
    if (width > VBE_DISPI_MAX_WIDTH || height > VBE_DISPI_MAX_HEIGHT) {
        serial_puts("[VBE] Rejected mode above 1080p cap.\n");
        return -1;
    }

    if (!vbe_dispi_available()) {
        serial_puts("[VBE] dispi interface not detected - cannot switch mode.\n");
        return -1;
    }

    uint64_t lfb = find_lfb_base_via_pci();

    uint32_t pitch = width * 4; /* 32bpp always, see vbe_dispi.h */
    uint64_t fb_bytes = (uint64_t)pitch * height;

    /* Disable output while we reprogram geometry - real hardware and
     * every emulator implementing this spec require this: writing
     * XRES/YRES/BPP with the display still enabled is undefined
     * behavior per the spec, even though some implementations happen
     * to tolerate it. */
    dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    dispi_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    dispi_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    dispi_write(VBE_DISPI_INDEX_BPP, VBE_DISPI_MAX_BPP);
    dispi_write(VBE_DISPI_INDEX_BANK, 0);
    dispi_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    dispi_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    dispi_write(VBE_DISPI_INDEX_ENABLE,
                VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM);

    /* Confirm the card actually accepted the resolution rather than
     * silently clamping/ignoring it - some virtual cards cap maximum
     * resolution below what dispi nominally allows. */
    uint16_t got_x = dispi_read(VBE_DISPI_INDEX_XRES);
    uint16_t got_y = dispi_read(VBE_DISPI_INDEX_YRES);
    if (got_x != width || got_y != height) {
        serial_puts("[VBE] Card did not accept requested mode - reverting.\n");
        revert_to_previous_mode();
        return -1;
    }

    if (lfb == 0) {
        /* No PCI match (or an unrecognized vendor/device pair for
         * this VM's virtual card) - fall back to the LFB base
         * multiboot/GRUB already reported at boot. On every dispi
         * implementation this driver targets, the BAR (and therefore
         * the LFB physical base) is fixed once at boot and never
         * moves just because XRES/YRES changed, so the boot-time
         * address is still correct after a mode switch. */
        extern uint64_t mb_fb_addr(void);
        lfb = mb_fb_addr();
    }
    if (lfb == 0) {
        serial_puts("[VBE] Could not determine LFB base - reverting.\n");
        revert_to_previous_mode();
        return -1;
    }

    if (lfb + fb_bytes > 0x100000000ULL) {
        /* Outside the low 4 GiB boot64.s identity-mapped - would fault
         * on first touch. Every VM this driver targets keeps its VBE
         * BAR well below this, so hitting it means something is very
         * wrong (bad BAR read) - fail safely rather than guess. */
        serial_puts("[VBE] LFB range exceeds identity-mapped 4GiB window - reverting.\n");
        revert_to_previous_mode();
        return -1;
    }

    /* Re-point g_framebuffer at the new geometry, replacing the shadow
     * back buffer graphics_init() originally allocated (it was sized
     * for the old resolution). Frees the old one only after the new
     * one is confirmed allocated, so a failed kmalloc leaves the
     * previous, working framebuffer state untouched. */
    uint8_t *new_shadow = (uint8_t *)kmalloc((size_t)fb_bytes);
    if (!new_shadow) {
        serial_puts("[VBE] Could not allocate new shadow framebuffer - reverting.\n");
        revert_to_previous_mode();
        return -1;
    }
    memset(new_shadow, 0, (size_t)fb_bytes);

    /* graphics_init() falls back to drawing straight into the hardware
     * LFB (g_framebuffer.framebuffer == the hw pointer) when its
     * initial shadow kmalloc fails - only free the old framebuffer
     * pointer if it was actually a separate shadow allocation, not the
     * hardware LFB itself. */
    uint8_t *old_shadow = g_framebuffer.framebuffer;
    int old_was_shadow = (old_shadow != (uint8_t *)(uintptr_t)lfb);

    g_framebuffer.width  = width;
    g_framebuffer.height = height;
    g_framebuffer.pitch  = pitch;
    g_framebuffer.depth  = VBE_DISPI_MAX_BPP;
    g_framebuffer.mode   = GRAPHICS_MODE_VESA_32BIT;
    g_framebuffer.framebuffer = new_shadow;
    graphics_set_hw_framebuffer((uint8_t *)(uintptr_t)lfb);

    if (old_was_shadow && old_shadow) kfree(old_shadow);

    /* gterm's cell grid is sized from g_framebuffer.width/height at
     * gterm_init() time - re-run it so the grid (and its backing
     * kmalloc) matches the new geometry instead of the stale one. */
    gterm_init();

    serial_puts("[VBE] Mode switch applied.\n");
    return 0;
}
