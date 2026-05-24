/* Boot detection helpers (Multiboot2) */
#ifndef BOOT_DETECT_H
#define BOOT_DETECT_H

#include <stdint.h>

/* Parse multiboot tags and detect boot medium */
void detect_boot_medium(unsigned int multiboot_magic, void *multiboot_info);
const char *get_boot_device(void);
const char *get_boot_cmdline(void);
int get_boot_is_uefi(void);
const char *get_boot_mode(void);

#endif /* BOOT_DETECT_H */
