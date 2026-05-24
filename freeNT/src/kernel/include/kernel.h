#ifndef _KERNEL_H
#define _KERNEL_H

#include "types.h"
#include "config.h"

/* Kernel version */
#define KERNEL_VERSION_MAJOR 1
#define KERNEL_VERSION_MINOR 0
#define KERNEL_VERSION_PATCH 0

/* Kernel entry point (defined in boot assembly)
    Accepts Multiboot2 magic and info pointer (both 32-bit values passed
    through the 64-bit boot stub). */
void kernel_main(unsigned int multiboot_magic, unsigned int multiboot_info);

/* Kernel initialization routines */
void kernel_init(void);
void kernel_panic(const char *fmt, ...);

/* Kernel memory stats */
typedef struct {
    uint64_t total_memory;
    uint64_t free_memory;
    uint64_t used_memory;
    uint64_t pages_allocated;
    uint64_t pages_free;
} kernel_mem_stats_t;

kernel_mem_stats_t* kernel_get_mem_stats(void);

#endif /* _KERNEL_H */
