#ifndef FREENT_KERNEL_H
#define FREENT_KERNEL_H

#include "types.h"

#define KERNEL_NAME    "freeNT"
#define KERNEL_VERSION "1.0"

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info);
void kernel_panic(const char* reason);

/* Multiboot2 framebuffer info parsed in kernel.c's parse_multiboot().
 * Populated before graphics_init() is called, so it's safe to read
 * these any time after kernel_init() begins. mb_fb_found() returns 0
 * if GRUB never handed us a usable packed-RGB framebuffer tag (old
 * GRUB, VM without VBE support, etc). */
int      mb_fb_found(void);
uint64_t mb_fb_addr(void);
uint32_t mb_fb_pitch(void);
uint32_t mb_fb_width(void);
uint32_t mb_fb_height(void);
uint8_t  mb_fb_bpp(void);

#endif
