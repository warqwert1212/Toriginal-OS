/* =============================================================================
 * ASSET_BLOB.S — embeds the generated asset archive (wallpapers,
 * start menu images, cursors) into the kernel image as plain data.
 *
 * See kernel.c's seed_embedded_assets_to_fs() for why this exists:
 * TRBL (this OS's own bootloader, which replaces GRUB after the
 * first install) has no way to deliver GRUB-style "modules" to the
 * kernel, so assets that only ever arrived via multiboot2 module
 * tags were silently never seeded on any TRBL-only boot. Baking them
 * directly into kernel.elf - the same .incbin technique
 * bootloader_blobs.s already uses for TRBL's own stage1/stage2 -
 * makes asset seeding work identically no matter which bootloader
 * ran.
 *
 * ASSET_BLOB_PATH is supplied on the assembler command line (-D...,
 * via -x assembler-with-cpp) by the Makefile, pointing at the file
 * build_asset_blob.py generates. Same absolute-path requirement as
 * bootloader_blobs.s: .incbin resolves relative to the assembler's
 * own working directory, not this file's location.
 * ============================================================================= */

.section .asset_blobs, "a"
.global g_asset_blob
.global g_asset_blob_size

#ifndef ASSET_BLOB_PATH
#error "ASSET_BLOB_PATH must be defined (absolute path to the generated asset blob) - see Makefile"
#endif

.align 16
g_asset_blob:
    .incbin ASSET_BLOB_PATH
g_asset_blob_end:

.align 8
g_asset_blob_size:
    .quad g_asset_blob_end - g_asset_blob
