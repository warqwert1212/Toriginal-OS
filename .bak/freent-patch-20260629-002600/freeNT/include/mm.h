#ifndef _KERNEL_MM_H
#define _KERNEL_MM_H

#include "types.h"
#include "config.h"
#include "memory.h"   /* kmalloc / kzalloc / kfree live here — single source of truth */

#define PAGE_PRESENT     0x001
#define PAGE_WRITE       0x002
#define PAGE_USER        0x004

typedef struct {
    uint64_t entry;
} pte_t;

typedef struct {
    uint64_t entry;
} pde_t;

void mm_init_physical(paddr_t mem_start, paddr_t mem_end);

paddr_t mm_alloc_pages(size_t num_pages);
void mm_free_pages(paddr_t paddr, size_t num_pages);

void mm_init_paging(void);
void mm_enable_paging(void);
void mm_map_page(vaddr_t vaddr, paddr_t paddr, uint64_t flags);
void mm_unmap_page(vaddr_t vaddr);

/* krealloc is implemented alongside kmalloc/kfree in heap.c */
void* krealloc(void *ptr, size_t new_size);

void mm_init_heap(vaddr_t heap_start, vaddr_t heap_end);

#endif /* _KERNEL_MM_H */
