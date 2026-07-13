#include "memory.h"
#include "types.h"
#include "pmm.h"
#include "string.h"
#include "serial.h"



#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define PAGE_PRESENT  0x001u
#define PAGE_WRITE    0x002u
#define PAGE_USER     0x004u



typedef struct { uint64_t entry; } pte_t;



static pte_t *pml4 = NULL;  


void mm_init_physical(paddr_t s, paddr_t e)
{
    (void)s;
    (void)e;
}


paddr_t mm_alloc_pages(size_t n)
{
    if (n == 0) return 0;

    paddr_t first = (paddr_t)(uintptr_t)pmm_alloc_frame();
    if (!first) return 0;

    for (size_t i = 1; i < n; i++) {
        if (!pmm_alloc_frame()) return 0;
    }

    return first;
}

void mm_free_pages(paddr_t p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        pmm_free_frame((void *)(uintptr_t)(p + i * PAGE_SIZE));
}


static pte_t *get_or_alloc(pte_t *table, uint32_t idx, uint64_t flags)
{
    if (!(table[idx].entry & PAGE_PRESENT)) {
        paddr_t p = (paddr_t)(uintptr_t)pmm_alloc_frame();
        if (!p) return NULL;

        pte_t *child = (pte_t *)(uintptr_t)p;
        memset(child, 0, PAGE_SIZE);

        table[idx].entry = p
                         | PAGE_PRESENT
                         | PAGE_WRITE
                         | (flags & PAGE_USER);
        return child;
    }

    return (pte_t *)(uintptr_t)(table[idx].entry & ~0xFFFULL);
}


void mm_init_paging(void)
{
    paddr_t pa = (paddr_t)(uintptr_t)pmm_alloc_frame();
    if (!pa) {
        serial_puts("[MM] FATAL: no frame available for PML4\n");
        return;
    }
    pml4 = (pte_t *)(uintptr_t)pa;
    memset(pml4, 0, PAGE_SIZE);
}


void mm_enable_paging(void)
{
    if (!pml4) return;

    uint64_t addr = (uint64_t)(uintptr_t)pml4;
    __asm__ volatile("movq %0, %%cr3" :: "r"(addr) : "memory");

    uint64_t cr0;
    __asm__ volatile("movq %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000UL;
    __asm__ volatile("movq %0, %%cr0" :: "r"(cr0) : "memory");
}


void mm_map_page(vaddr_t va, paddr_t pa, uint64_t flags)
{
    if (!pml4) return;

    uint32_t i4 = (va >> 39) & 0x1FFu;
    uint32_t i3 = (va >> 30) & 0x1FFu;
    uint32_t i2 = (va >> 21) & 0x1FFu;
    uint32_t i1 = (va >> 12) & 0x1FFu;

    pte_t *l3 = get_or_alloc(pml4, i4, flags);  if (!l3) return;
    pte_t *l2 = get_or_alloc(l3,   i3, flags);  if (!l2) return;
    pte_t *l1 = get_or_alloc(l2,   i2, flags);  if (!l1) return;

    l1[i1].entry = (pa & ~0xFFFULL) | PAGE_PRESENT | flags;

    /* Flush TLB for this virtual address (AT&T syntax). */
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
}

//we have dipshit and dumbass over here
void mm_unmap_page(vaddr_t va)
{
    if (!pml4) return;

    uint32_t i4 = (va >> 39) & 0x1FFu;
    uint32_t i3 = (va >> 30) & 0x1FFu;
    uint32_t i2 = (va >> 21) & 0x1FFu;
    uint32_t i1 = (va >> 12) & 0x1FFu;

    if (!(pml4[i4].entry & PAGE_PRESENT)) return;
    pte_t *l3 = (pte_t *)(uintptr_t)(pml4[i4].entry & ~0xFFFULL);

    if (!(l3[i3].entry & PAGE_PRESENT)) return;
    pte_t *l2 = (pte_t *)(uintptr_t)(l3[i3].entry & ~0xFFFULL);

    if (!(l2[i2].entry & PAGE_PRESENT)) return;
    pte_t *l1 = (pte_t *)(uintptr_t)(l2[i2].entry & ~0xFFFULL);

    l1[i1].entry = 0;
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * § Kernel heap allocator
 * This is a simple bump-pointer allocator for the kernel heap. its pretty shity, some times, but at lest the wole system takes 4mb, thats fucking great.
 * ═══════════════════════════════════════════════════════════════════════════ */
