/* Boot detection helpers (Multiboot2) */
#ifndef BOOT_DETECT_H
#define BOOT_DETECT_H

/* FIX: Avoid pulling in host system headers which conflict between 32-bit/64-bit definitions.
   Use the kernel's own unified type configuration file instead. */
#include "types.h"

/* Parse multiboot tags and detect boot medium */
void detect_boot_medium(unsigned int multiboot_magic, void *multiboot_info);
const char *get_boot_device(void);
const char *get_boot_cmdline(void);
int get_boot_is_uefi(void);
const char *get_boot_mode(void);

#endif /* BOOT_DETECT_H */