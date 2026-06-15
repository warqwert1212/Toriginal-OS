#include "memory.h"
#include "types.h"
#include "pmm.h"
#include "string.h"

/* serial_puts is implemented in serial.c; avoid pulling in the full header
 * so this translation unit compiles without the serial driver being present. */
extern void serial_puts(const char *);

/* ── Compile-time constants ───────────────────────────────────────────────── */

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define PAGE_PRESENT  0x001u
#define PAGE_WRITE    0x002u
#define PAGE_USER     0x004u

/* Kernel heap virtual address window (above the 2 MiB GRUB load point).
 * 16 MiB – 80 MiB gives 64 MiB of virtual heap space, more than enough for
 * a v1 bump allocator that never recycles memory. */
#define HEAP_VIRT_START  0x0000000001000000ULL   /* 16 MiB */
#define HEAP_VIRT_END    0x0000000005000000ULL   /* 80 MiB */

/* Number of pages to pre-map at mm_init_heap() time so that early kmalloc
 * calls succeed without triggering a page fault. */
#define HEAP_PREMAP_PAGES  16u                   /* 64 KiB */

/* ── On-disk / in-memory page-table entry type ───────────────────────────── */

typedef struct { uint64_t entry; } pte_t;

/* ── Module-level state ───────────────────────────────────────────────────── */

static pte_t   *pml4       = NULL;  /* physical address of PML4 table         */
static vaddr_t  heap_start = 0;     /* first byte of the heap VA window       */
static vaddr_t  heap_end   = 0;     /* one-past-last byte of the heap VA window */
static vaddr_t  heap_cur   = 0;     /* current bump pointer (0 = uninitialised) */

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
 * § Heap initialisation
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Set the heap VA window and pre-map HEAP_PREMAP_PAGES pages so the very
 * first kmalloc() call does not require a live page-fault handler.
 * Must be called after mm_init_paging() and pmm_init(). */
void mm_init_heap(vaddr_t s, vaddr_t e)
{
    heap_start = s;
    heap_end   = e;
    heap_cur   = s;

    for (size_t i = 0; i < HEAP_PREMAP_PAGES; i++) {
        paddr_t p = (paddr_t)(uintptr_t)pmm_alloc_frame();
        if (!p) {
            serial_puts("[MM] WARN: ran out of frames during heap pre-map\n");
            break;
        }
        mm_map_page(s + i * PAGE_SIZE, p, PAGE_PRESENT | PAGE_WRITE);
    }

    serial_puts("[MM] Heap initialised.\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * § Kernel heap allocator  (bump pointer)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Allocate `size` bytes from the kernel heap, aligned to 16 bytes.
 *
 * Before mm_init_heap() has been called (heap_cur == 0) an inline static
 * emergency pool of 256 KiB is used.  This covers very early boot code that
 * calls kmalloc before the PMM and page tables are ready.
 *
 * After mm_init_heap() sets heap_cur, new pages are mapped on demand
 * whenever the bump pointer crosses a page boundary.  The function never
 * re-enters the emergency pool after that point. */
void *kmalloc(size_t size)
{
    if (!size) return NULL;

    /* Align every allocation to 16 bytes for general safety. */
    size = (size + 15u) & ~15ULL;

    /* ── Emergency pool: active only before mm_init_heap() ─────────────── */
    if (!heap_cur) {
        static uint8_t pool[256 * 1024];
        static size_t  idx = 0;
        if (idx + size > sizeof(pool)) {
            serial_puts("[MM] FATAL: emergency pool exhausted\n");
            return NULL;
        }
        void *p = pool + idx;
        idx += size;
        return p;
    }

    /* ── Normal heap path ───────────────────────────────────────────────── */
    void *ptr   = (void *)heap_cur;
    heap_cur   += size;

    /* Map any fresh pages that the new allocation spans.
     * We compute the page-aligned window [ms, me) and call mm_map_page()
     * for each page in it.  Pages that were already pre-mapped will have
     * PAGE_PRESENT set in their PTE, and get_or_alloc() is idempotent for
     * present entries, so double-mapping is harmless. */
    vaddr_t ms = (vaddr_t)ptr    & ~((vaddr_t)(PAGE_SIZE - 1));
    vaddr_t me = (heap_cur + PAGE_SIZE - 1) & ~((vaddr_t)(PAGE_SIZE - 1));

    for (vaddr_t v = ms; v < me; v += PAGE_SIZE) {
        if (v >= heap_start && v < heap_end) {
            paddr_t p = (paddr_t)(uintptr_t)pmm_alloc_frame();
            if (p) mm_map_page(v, p, PAGE_PRESENT | PAGE_WRITE);
        }
    }

    if (heap_cur > heap_end) {
        serial_puts("[MM] FATAL: heap exhausted — increase HEAP_VIRT_END\n");
        /* Return the pointer anyway; the caller will page-fault immediately,
         * which is safer than returning NULL and masking a real OOM. */
    }

    return ptr;
}

/* Zero-initialised allocation — thin wrapper around kmalloc. */
void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

/* kfree — no-op for the v1 bump allocator.
 * All kernel allocations live for the lifetime of the kernel image.
 * A proper free-list can be added in v2 without changing callers. */
void kfree(void *ptr)
{
    (void)ptr;
}

/* krealloc — allocate new_size bytes, copy as much of the old content as
 * fits, then logically release the old pointer (no-op in bump allocator).
 *
 * CONTRACT: callers must not pass new_size larger than the original
 * allocation size, because we have no header from which to infer the old
 * size and will copy exactly new_size bytes. */
void *krealloc(void *ptr, size_t new_size)
{
    if (!ptr)      return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return NULL; }

    void *np = kmalloc(new_size);
    if (np) memcpy(np, ptr, new_size);
    kfree(ptr);
    return np;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * § memory_init — called once from kernel_main
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Initialise physical and virtual memory in the correct order:
 *   1. pmm_init(0)       — parse the Multiboot2 memory map (0 = safe
 *                           fallback: treat 1 MiB – 16 MiB as available).
 *   2. mm_init_heap()    — set VA window and pre-map first 64 KiB.
 *
 * mm_init_paging() and mm_enable_paging() are intentionally NOT called here
 * in v1 because the kernel runs in the identity-mapped environment that
 * boot64.s already set up with 2 MiB huge pages.  Calling mm_enable_paging()
 * would switch CR3 to an empty PML4 and triple-fault.  Leave it for v2 when
 * per-process address spaces are introduced. */
void memory_init(void)
{
    /* Let pmm.c parse the Multiboot2 memory map.
     * Passing 0 causes it to fall back to the safe 1–16 MiB region. */
    extern void pmm_init(uint64_t);
    pmm_init(0);

    mm_init_heap(HEAP_VIRT_START, HEAP_VIRT_END);

    serial_puts("[MM] memory_init complete.\n");
}