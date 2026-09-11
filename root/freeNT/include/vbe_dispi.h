#ifndef _VBE_DISPI_H
#define _VBE_DISPI_H

#include "types.h"

/* =============================================================================
 * VBE_DISPI.H — runtime display mode switching via the Bochs VBE
 * "dispi" (DIsplay SPecific Interface) extension.
 *
 * This is the real mechanism real display drivers use in this class of
 * VM: VirtualBox's default graphics adapter, QEMU's "std"/stdvga, and
 * Bochs itself all implement the same fixed pair of I/O ports (first
 * introduced by the Bochs project, adopted as a de facto standard).
 * grub.cfg already loads GRUB's video_bochs/video_cirrus modules to
 * drive this exact hardware to get the boot-time framebuffer - this
 * header lets the *kernel* drive it too, after boot, with no BIOS/
 * real-mode call and no v86 monitor needed. That's what makes this a
 * genuine runtime mode switch instead of "ask the bootloader and hope."
 *
 * Ports and register indices are fixed by the spec, not discovered
 * via PCI - PCI is only used here to locate the linear framebuffer
 * (LFB) base, since that varies by BAR assignment.
 * ========================================================================= */

#define VBE_DISPI_MAX_WIDTH   1920
#define VBE_DISPI_MAX_HEIGHT  1080
#define VBE_DISPI_MAX_BPP     32

/* Returns 1 if dispi I/O ports respond with the expected ID signature
 * (i.e. this is really Bochs-compatible VBE hardware, not just two
 * ports that happen to read back whatever was last written). Safe to
 * call speculatively - does not touch the display if it returns 0. */
int vbe_dispi_available(void);

/* Sets a new mode and re-points g_framebuffer (graphics_core.h) at the
 * new geometry/LFB. Width/height are clamped to
 * VBE_DISPI_MAX_WIDTH/HEIGHT (i.e. 1080p) by the caller (settings.c) -
 * this function itself additionally rejects anything above those caps
 * defensively. bpp is always 32 - every framebuffer draw primitive in
 * graphics_core.c is 32bpp-first and this codebase's whole graphics
 * stack assumes it.
 *
 * On success: reinitializes gterm (grid dimensions depend on
 * framebuffer size) and returns 0. On failure (unsupported hardware,
 * resolution rejected by the virtual card, mapping failure): leaves
 * the previous mode fully intact and returns -1 - callers must not
 * assume partial application. */
int vbe_dispi_set_mode(uint32_t width, uint32_t height);

#endif /* _VBE_DISPI_H */
