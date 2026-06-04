#ifndef _KERNEL_MM_H
#define _KERNEL_MM_H

#include "types.h"
#include "config.h"

/* Page flags */
#define PAGE_PRESENT     0x001
#define PAGE_WRITE       0x002
#define PAGE_USER        0x004
#define PAGE_WRITETHROUGH 0x008
#define PAGE_CACHE_DISABLE 0x010
#define PAGE_ACCESSED    0x020
#define PAGE_DIRTY       0x040
#define PAGE_HUGE        0x080
#define PAGE_GLOBAL      0x100

/* Page table entry structure */
typedef struct {
    uint64_t entry;
} pte_t;

/* Page directory entry structure */
typedef struct {
    uint64_t entry;
} pde_t;

/* Physical memory bitmap allocator */
typedef struct {
    uint8_t *bitmap;
    size_t total_pages;
    size_t free_pages;
    size_t allocated_pages;
} phys_allocator_t;

/* Memory zone for NUMA-like allocation */
typedef struct {
    paddr_t start;
    paddr_t end;
    size_t free_pages;
    size_t allocated_pages;
} mem_zone_t;

/* Initialize physical memory allocator */
void mm_init_physical(paddr_t mem_start, paddr_t mem_end);

/* Allocate/deallocate physical pages */
paddr_t mm_alloc_pages(size_t num_pages);
void mm_free_pages(paddr_t paddr, size_t num_pages);

/* Page table operations */
void mm_init_paging(void);
void mm_enable_paging(void);
void mm_map_page(vaddr_t vaddr, paddr_t paddr, uint64_t flags);
void mm_unmap_page(vaddr_t vaddr);

/* Virtual memory allocation (kernel space) */
void* kmalloc(size_t size);
void kfree(void *ptr);
void* krealloc(void *ptr, size_t new_size);

/* Heap management */
void mm_init_heap(vaddr_t heap_start, vaddr_t heap_end);

#endif /* _KERNEL_MM_H */
