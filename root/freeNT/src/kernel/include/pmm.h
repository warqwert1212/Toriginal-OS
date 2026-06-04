#pragma once
#include <stdint.h>
#include <stddef.h>

#define PMM_FRAME_SIZE 4096

void pmm_init(uint64_t multiboot_ptr);
void* pmm_alloc_frame(void);
void pmm_free_frame(void* phys_addr);
uint32_t pmm_get_free_ram(void);