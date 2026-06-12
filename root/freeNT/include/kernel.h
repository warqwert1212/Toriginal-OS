#ifndef FREENT_KERNEL_H
#define FREENT_KERNEL_H

#include "types.h"

#define KERNEL_NAME    "freeNT"
#define KERNEL_VERSION "1.0"

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info);
void kernel_panic(const char* reason);

#endif