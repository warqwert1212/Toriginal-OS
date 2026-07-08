#include "memory.h"
#include "types.h"
#include "pmm.h"
#include "string.h"
#include "serial.h"

/* ── Compile-time constants ───────────────────────────────────────────────── */

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define PAGE_PRESENT  0x001u
#define PAGE_WRITE    0x002u
#define PAGE_USER     0x004u

/* ── On-disk / in-memory page-table entry type ─────────---─────────────────── */

typedef struct { uint64_t entry; } pte_t;

/* ── Module-level state ───────────────────────────────────────────────────── */

static pte_t *pml4 = NULL;  /* physical address of PML4 table */

/* ═══════════════════════════════════════════════════════════════════════════
 * § Physical memory  (thin wrappers around pmm.c)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Kept for ABI compatibility — callers that used mm_init_physical() before
 * the PMM refactor still compile.  All real work now lives in pmm_init(). */
void mm_init_physical(paddr_t s, paddr_t e)
{
    (void)s;
    (void)e;
}

/* Allocate n physical frames.  For n == 1 this is exact; for n > 1 the PMM
 * bitmap is walked linearly so consecutive single-frame calls are normally
 * contiguous on a fresh system — sufficient for v1. */
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

/* ═══════════════════════════════════════════════════════════════════════════
 * § Paging — 4-level (PML4 → PDPT → PD → PT)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Internal helper ──────────────────────────────────────────────────────── *
 * Return a pointer to the child page table addressed by table[idx].
 * If the entry is not present a fresh frame is allocated, zeroed, and the
 * entry written with PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER).
 * Returns NULL on allocation failure.                                        */
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

/* Allocate a fresh PML4 table via the PMM and store its physical address in
 * the module-level pml4 pointer.  Must be called before mm_map_page(). */
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

/* Load the PML4 physical address into CR3 and set CR0.PG. */
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

/* Map virtual address va to physical address pa with the given PTE flags.
 * Intermediate page tables are allocated lazily via get_or_alloc().
 * A no-op if pml4 has not been initialised yet. */
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

/* Clear the PTE for va and flush the TLB.  Does not free the physical frame
 * (bump-allocator semantics — memory is never reclaimed in v1). */
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
 *
 * MOVED to heap.c — a real free-list allocator with coalescing now lives
 * there (mm_init_heap, kmalloc, kzalloc, kfree, krealloc, memory_init).
 * The old bump-pointer allocator that used to live here (where kfree()
 * was a permanent no-op and nothing was ever reclaimed) has been removed
 * entirely rather than left as dead code, to avoid duplicate-symbol
 * conflicts with heap.c's real implementations of the same functions.
 * ═══════════════════════════════════════════════════════════════════════════ */
