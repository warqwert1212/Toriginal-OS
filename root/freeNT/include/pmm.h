#pragma once
#include <stdint.h>
#include <stddef.h>

#define PMM_FRAME_SIZE 4096

/* RAM allocation modes for pmm_apply_ram_config() - numerically
 * identical to TRBL_MEM_MODE_* in trbl_settings.h (that sector is the
 * actual source of truth these come from; duplicated as plain
 * integers here so pmm.c doesn't have to pull in the ATA-backed
 * settings-sector API just for three constants - kernel.c is what
 * bridges the two, reading trbl_settings.h's values and passing them
 * straight through). */
#define PMM_MEM_MODE_MAX    0   /* use every byte of detected physical RAM */
#define PMM_MEM_MODE_CUSTOM 1   /* cap usable RAM at an exact KB amount    */
#define PMM_MEM_MODE_LEAST  2   /* bare minimum the kernel can run in      */

void pmm_init(uint64_t multiboot_ptr);
void* pmm_alloc_frame(void);
void pmm_free_frame(void* phys_addr);
uint32_t pmm_get_free_ram(void);

/* Total physical RAM the firmware/bootloader actually reported as
 * usable (KB) - independent of any cap applied below. This is what
 * `mem max` and `hardware` mean by "detected RAM"; it never changes
 * after pmm_init(). */
uint32_t pmm_get_total_ram_kb(void);

/* The RAM ceiling currently being enforced (KB). Equals
 * pmm_get_total_ram_kb() until pmm_apply_ram_config() narrows it. */
uint32_t pmm_get_ram_limit_kb(void);

/* Applies a `mem` command's choice live, no reboot required: for
 * MAX/LEAST, custom_kb is ignored; for CUSTOM it's the exact ceiling
 * in KB (clamped to [bare-minimum floor, total detected]). Frames
 * already handed out above a newly-lowered ceiling are left alone -
 * this can't reclaim memory something else is actively using, it
 * only keeps *future* allocations below the new ceiling. Raising the
 * ceiling back up (including back to MAX) makes any still-free frames
 * that a previous, lower cap had excluded available again immediately.
 * Returns 0 on success, -1 on an unrecognized mode. */
int pmm_apply_ram_config(uint8_t mode, uint32_t custom_kb);
