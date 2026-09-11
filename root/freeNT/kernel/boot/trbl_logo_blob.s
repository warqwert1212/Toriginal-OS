/* =============================================================================
 * TRBL_LOGO_BLOB.S — embeds the TRBL boot logo PNG into the kernel
 * image as plain data.
 *
 * Decoded and blitted to the top-left corner of the screen once
 * graphics comes online (see kernel.c's show_boot_logo()) - a real
 * end-to-end graphics test: PNG decode, surface creation, and
 * alpha-blit all exercised on real boot-time data before anything
 * else draws to the screen.
 *
 * TRBL_LOGO_PATH is supplied on the assembler command line (-D...,
 * via -x assembler-with-cpp) by the Makefile. Same absolute-path
 * requirement as bootloader_blobs.s/asset_blob.s: .incbin resolves
 * relative to the assembler's own working directory, not this
 * file's location.
 * ============================================================================= */

.section .asset_blobs, "a"
.global g_trbl_logo_png
.global g_trbl_logo_png_size

#ifndef TRBL_LOGO_PATH
#error "TRBL_LOGO_PATH must be defined (absolute path to trbl_logo.png) - see Makefile"
#endif

.align 16
g_trbl_logo_png:
    .incbin TRBL_LOGO_PATH
g_trbl_logo_png_end:

.align 8
g_trbl_logo_png_size:
    .quad g_trbl_logo_png_end - g_trbl_logo_png
