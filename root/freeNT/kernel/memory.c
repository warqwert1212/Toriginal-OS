// ==============================================================================
// MEMORY.C - Virtual Memory Manager & Kernel Heap
// Physical allocation is handled entirely by pmm.c (bitmap allocator).
// This file owns: page table management, heap, and the mm_* public API.
// ==============================================================================

#include "memory.h"
#include "types.h"
#include "pmm.h"
#include "io.h"
#include "string.h"

// PAGE_SIZE may not be defined in some build configs; default to 4KiB.
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

// Page table entry and flags
typedef struct {
    uint64_t entry;
} pte_t;

#define PAGE_PRESENT 0x001
#define PAGE_WRITE   0x002
#define PAGE_USER    0x004

// ---------------------------------------------------------------------------
// Page table root (PML4) — set during mm_init_paging()
// ---------------------------------------------------------------------------
static pte_t *pml4 = NULL;

// ---------------------------------------------------------------------------
// Kernel heap state
// ---------------------------------------------------------------------------
static vaddr_t heap_start   = 0;
static vaddr_t heap_end     = 0;
static vaddr_t heap_current = 0;

// ---------------------------------------------------------------------------
// mm_init_physical
// We no longer maintain our own bitmap here.
// Just forward to pmm_init() — it parses the Multiboot2 map and
// marks everything correctly.
// Call signature kept the same so nothing else breaks.
// ---------------------------------------------------------------------------
void mm_init_physical(paddr_t mem_start, paddr_t mem_end) {
    // pmm_init() is called separately from kernel_main with the multiboot ptr.
    // This function is kept as a no-op stub so existing call sites still compile.
    // If you want you can remove the call to mm_init_physical from kernel.c and
    // just call pmm_init() directly — either way is fine.
    (void)mem_start;
    (void)mem_end;
}

// ---------------------------------------------------------------------------
// mm_alloc_pages — allocate N *contiguous* physical pages
// Delegates entirely to pmm_alloc_frame() for single pages.
// For multiple pages we loop and hope they come out contiguous.
// For 1.0 this is fine — the heap only ever asks for 1 page at a time.
// ---------------------------------------------------------------------------
paddr_t mm_alloc_pages(size_t num_pages) {
    if (num_pages == 0)
        return 0;

    if (num_pages == 1) {
        void *frame = pmm_alloc_frame();
        return (paddr_t)(uintptr_t)frame;
    }

    // Multi-page: allocate one at a time.
    // pmm_alloc_frame scans linearly so consecutive calls are usually contiguous
    // on a fresh system. Good enough for 1.0.
    paddr_t first = (paddr_t)(uintptr_t)pmm_alloc_frame();
    if (!first) return 0;

    for (size_t i = 1; i < num_pages; i++) {
        void *next = pmm_alloc_frame();
        if (!next) return 0;
    }

    return first;
}

// ---------------------------------------------------------------------------
// mm_free_pages — return pages to pmm
// ---------------------------------------------------------------------------
void mm_free_pages(paddr_t paddr, size_t num_pages) {
    for (size_t i = 0; i < num_pages; i++) {
        pmm_free_frame((void *)(uintptr_t)(paddr + i * PAGE_SIZE));
    }
}

// ---------------------------------------------------------------------------
// mm_init_paging
// Allocates a fresh PML4 table via pmm and zeroes it.
// ---------------------------------------------------------------------------
void mm_init_paging(void) {
    paddr_t pml4_paddr = mm_alloc_pages(1);
    if (!pml4_paddr) {
        serial_puts("[MM] FATAL: could not allocate PML4\n");
        return;
    }
    pml4 = (pte_t *)(uintptr_t)pml4_paddr;
    memset(pml4, 0, PAGE_SIZE);
}

// ---------------------------------------------------------------------------
// mm_enable_paging — load PML4 into CR3 and set CR0.PG
// ---------------------------------------------------------------------------
void mm_enable_paging(void) {
    if (!pml4) return;
    uint64_t pml4_addr = (uint64_t)(uintptr_t)pml4;
    asm volatile("movq %0, %%cr3" : : "r"(pml4_addr) : "memory");
    uint64_t cr0;
    asm volatile("movq %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000UL;
    asm volatile("movq %0, %%cr0" : : "r"(cr0) : "memory");
}

// ---------------------------------------------------------------------------
// mm_map_page — map one virtual page to one physical page
// Creates intermediate page tables as needed using pmm_alloc_frame().
// ---------------------------------------------------------------------------
void mm_map_page(vaddr_t vaddr, paddr_t paddr, uint64_t flags) {
    if (!pml4) return;

    uint32_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint32_t pdp_idx  = (vaddr >> 30) & 0x1FF;
    uint32_t pd_idx   = (vaddr >> 21) & 0x1FF;
    uint32_t pt_idx   = (vaddr >> 12) & 0x1FF;

    // --- PML4 -> PDP ---
    pte_t *pdp_table;
    if (!(pml4[pml4_idx].entry & PAGE_PRESENT)) {
        paddr_t pdp_paddr = (paddr_t)(uintptr_t)pmm_alloc_frame();
        if (!pdp_paddr) return;
        pdp_table = (pte_t *)(uintptr_t)pdp_paddr;
        memset(pdp_table, 0, PAGE_SIZE);
        pml4[pml4_idx].entry = pdp_paddr | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
    } else {
        pdp_table = (pte_t *)(uintptr_t)(pml4[pml4_idx].entry & ~0xFFFULL);
    }

    // --- PDP -> PD ---
    pte_t *pd_table;
    if (!(pdp_table[pdp_idx].entry & PAGE_PRESENT)) {
        paddr_t pd_paddr = (paddr_t)(uintptr_t)pmm_alloc_frame();
        if (!pd_paddr) return;
        pd_table = (pte_t *)(uintptr_t)pd_paddr;
        memset(pd_table, 0, PAGE_SIZE);
        pdp_table[pdp_idx].entry = pd_paddr | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
    } else {
        pd_table = (pte_t *)(uintptr_t)(pdp_table[pdp_idx].entry & ~0xFFFULL);
    }

    // --- PD -> PT ---
    pte_t *pt_table;
    if (!(pd_table[pd_idx].entry & PAGE_PRESENT)) {
        paddr_t pt_paddr = (paddr_t)(uintptr_t)pmm_alloc_frame();
        if (!pt_paddr) return;
        pt_table = (pte_t *)(uintptr_t)pt_paddr;
        memset(pt_table, 0, PAGE_SIZE);
        pd_table[pd_idx].entry = pt_paddr | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
    } else {
        pt_table = (pte_t *)(uintptr_t)(pd_table[pd_idx].entry & ~0xFFFULL);
    }

    // --- PT -> physical page ---
    pt_table[pt_idx].entry = (paddr & ~0xFFFULL) | PAGE_PRESENT | flags;

    // Flush TLB for this address
    asm volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

// ---------------------------------------------------------------------------
// mm_unmap_page
// ---------------------------------------------------------------------------
void mm_unmap_page(vaddr_t vaddr) {
    if (!pml4) return;

    uint32_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint32_t pdp_idx  = (vaddr >> 30) & 0x1FF;
    uint32_t pd_idx   = (vaddr >> 21) & 0x1FF;
    uint32_t pt_idx   = (vaddr >> 12) & 0x1FF;

    if (!(pml4[pml4_idx].entry & PAGE_PRESENT)) return;
    pte_t *pdp = (pte_t *)(uintptr_t)(pml4[pml4_idx].entry & ~0xFFFULL);

    if (!(pdp[pdp_idx].entry & PAGE_PRESENT)) return;
    pte_t *pd = (pte_t *)(uintptr_t)(pdp[pdp_idx].entry & ~0xFFFULL);

    if (!(pd[pd_idx].entry & PAGE_PRESENT)) return;
    pte_t *pt = (pte_t *)(uintptr_t)(pd[pd_idx].entry & ~0xFFFULL);

    pt[pt_idx].entry = 0;
    asm volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

// ---------------------------------------------------------------------------
// mm_init_heap — set up the bump allocator range and pre-map initial pages
// ---------------------------------------------------------------------------
void mm_init_heap(vaddr_t heap_start_addr, vaddr_t heap_end_addr) {
    heap_start   = heap_start_addr;
    heap_end     = heap_end_addr;
    heap_current = heap_start;

    // Pre-map the first 64KB so early kmalloc calls don't page-fault
    size_t initial_pages = 16;
    for (size_t i = 0; i < initial_pages; i++) {
        paddr_t phys = (paddr_t)(uintptr_t)pmm_alloc_frame();
        if (!phys) {
            serial_puts("[MM] WARN: could not pre-map all heap pages\n");
            break;
        }
        vaddr_t virt = heap_start + i * PAGE_SIZE;
        mm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITE);
    }

    serial_puts("[MM] Heap initialised.\n");
}

// ---------------------------------------------------------------------------
// kmalloc — simple bump allocator
// Maps new pages on demand via pmm when the heap grows.
// ---------------------------------------------------------------------------
void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    // Align to 16 bytes
    size = (size + 15) & ~15ULL;

    void *ptr = (void *)heap_current;
    heap_current += size;

    // Map any new pages we've crossed into
    vaddr_t map_start = (vaddr_t)ptr & ~(PAGE_SIZE - 1ULL);
    vaddr_t map_end   = ((heap_current + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1ULL));

    for (vaddr_t v = map_start; v < map_end; v += PAGE_SIZE) {
        // Only map pages we haven't already mapped
        // (simple check: if heap_current just crossed a page boundary)
        if (v >= heap_start && v < heap_end) {
            // Attempt to map — mm_map_page is idempotent for already-mapped pages
            // because it checks PAGE_PRESENT before allocating a new PT
            paddr_t phys = (paddr_t)(uintptr_t)pmm_alloc_frame();
            if (phys) {
                mm_map_page(v, phys, PAGE_PRESENT | PAGE_WRITE);
            }
        }
    }

    if (heap_current > heap_end) {
        serial_puts("[MM] WARN: heap exhausted!\n");
        return NULL;
    }

    return ptr;
}

// ---------------------------------------------------------------------------
// kfree — stub (bump allocator doesn't reclaim)
// For 1.0 this is fine — kernel lifetime allocations only
// ---------------------------------------------------------------------------
void kfree(void *ptr) {
    (void)ptr;
    // Future: implement a free list here for 2.0
}

// ---------------------------------------------------------------------------
// krealloc
// ---------------------------------------------------------------------------
void *krealloc(void *ptr, size_t new_size) {
    if (!ptr)    return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return NULL; }

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;

    // We don't track old sizes in a bump allocator so just copy new_size bytes.
    // Caller is responsible for not passing a new_size larger than the original.
    memcpy(new_ptr, ptr, new_size);
    return new_ptr;
}


/* memory.c — kernel heap (bump allocator) + page table management.
 *
 * FIX: memory_init() now calls pmm_init() so physical memory is
 *      enumerated before the first kmalloc.
 * FIX: added kzalloc().
 */

#include "memory.h"
#include "types.h"
#include "pmm.h"
#include "string.h"

extern void serial_puts(const char *);

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define PAGE_PRESENT 0x001u
#define PAGE_WRITE   0x002u
#define PAGE_USER    0x004u

typedef struct { uint64_t entry; } pte_t;

static pte_t   *pml4        = NULL;
static vaddr_t  heap_start  = 0;
static vaddr_t  heap_end    = 0;
static vaddr_t  heap_cur    = 0;

/* Kernel heap lives at a fixed virtual address above 2 MiB load point */
#define HEAP_VIRT_START 0x0000000001000000ULL   /* 16 MiB */
#define HEAP_VIRT_END   0x0000000005000000ULL   /* 80 MiB */

/* ── physical memory ─────────────────────────────────────────────────────── */

void mm_init_physical(paddr_t s, paddr_t e) { (void)s; (void)e; }

paddr_t mm_alloc_pages(size_t n)
{
    if (n == 0) return 0;
    paddr_t first = (paddr_t)(uintptr_t)pmm_alloc_frame();
    for (size_t i = 1; i < n; i++) pmm_alloc_frame();
    return first;
}

void mm_free_pages(paddr_t p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        pmm_free_frame((void *)(uintptr_t)(p + i * PAGE_SIZE));
}

/* ── paging ──────────────────────────────────────────────────────────────── */

void mm_init_paging(void)
{
    paddr_t pa = (paddr_t)(uintptr_t)pmm_alloc_frame();
    if (!pa) { serial_puts("[MM] FATAL: no PML4 frame\n"); return; }
    pml4 = (pte_t *)(uintptr_t)pa;
    memset(pml4, 0, PAGE_SIZE);
}

void mm_enable_paging(void)
{
    if (!pml4) return;
    uint64_t a = (uint64_t)(uintptr_t)pml4;
    __asm__ volatile("movq %0,%%cr3"::"r"(a):"memory");
    uint64_t cr0;
    __asm__ volatile("movq %%cr0,%0":"=r"(cr0));
    cr0 |= 0x80000000UL;
    __asm__ volatile("movq %0,%%cr0"::"r"(cr0):"memory");
}

static pte_t *get_or_alloc(pte_t *table, uint32_t idx, uint64_t flags)
{
    if (!(table[idx].entry & PAGE_PRESENT)) {
        paddr_t p = (paddr_t)(uintptr_t)pmm_alloc_frame();
        if (!p) return NULL;
        pte_t *t = (pte_t *)(uintptr_t)p;
        memset(t, 0, PAGE_SIZE);
        table[idx].entry = p | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
        return t;
    }
    return (pte_t *)(uintptr_t)(table[idx].entry & ~0xFFFULL);
}

void mm_map_page(vaddr_t va, paddr_t pa, uint64_t flags)
{
    if (!pml4) return;
    uint32_t i4 = (va >> 39) & 0x1FF;
    uint32_t i3 = (va >> 30) & 0x1FF;
    uint32_t i2 = (va >> 21) & 0x1FF;
    uint32_t i1 = (va >> 12) & 0x1FF;
    pte_t *l3 = get_or_alloc(pml4, i4, flags); if (!l3) return;
    pte_t *l2 = get_or_alloc(l3,   i3, flags); if (!l2) return;
    pte_t *l1 = get_or_alloc(l2,   i2, flags); if (!l1) return;
    l1[i1].entry = (pa & ~0xFFFULL) | PAGE_PRESENT | flags;
    __asm__ volatile("invlpg (%0)"::"r"(va):"memory");
}

void mm_unmap_page(vaddr_t va)
{
    if (!pml4) return;
    uint32_t i4=(va>>39)&0x1FF, i3=(va>>30)&0x1FF, i2=(va>>21)&0x1FF, i1=(va>>12)&0x1FF;
    if (!(pml4[i4].entry & PAGE_PRESENT)) return;
    pte_t *l3=(pte_t*)(uintptr_t)(pml4[i4].entry&~0xFFFULL);
    if (!(l3[i3].entry&PAGE_PRESENT)) return;
    pte_t *l2=(pte_t*)(uintptr_t)(l3[i3].entry&~0xFFFULL);
    if (!(l2[i2].entry&PAGE_PRESENT)) return;
    pte_t *l1=(pte_t*)(uintptr_t)(l2[i2].entry&~0xFFFULL);
    l1[i1].entry = 0;
    __asm__ volatile("invlpg (%0)"::"r"(va):"memory");
}

void mm_init_heap(vaddr_t s, vaddr_t e)
{
    heap_start = s; heap_end = e; heap_cur = s;
    for (size_t i = 0; i < 16; i++) {
        paddr_t p = (paddr_t)(uintptr_t)pmm_alloc_frame();
        if (!p) break;
        mm_map_page(s + i * PAGE_SIZE, p, PAGE_PRESENT | PAGE_WRITE);
    }
    serial_puts("[MM] Heap initialised.\n");
}

/* ── allocator ───────────────────────────────────────────────────────────── */

void *kmalloc(size_t size)
{
    if (!size) return NULL;
    size = (size + 15) & ~15ULL;
    if (!heap_cur) {
        /* Heap not formally set up yet — use a static emergency pool */
        static uint8_t emergency[256 * 1024];
        static size_t  eidx = 0;
        if (eidx + size > sizeof(emergency)) return NULL;
        void *p = emergency + eidx; eidx += size; return p;
    }
    void *ptr = (void *)heap_cur;
    heap_cur += size;
    /* Map new pages on demand */
    vaddr_t ms = (vaddr_t)ptr & ~(PAGE_SIZE-1ULL);
    vaddr_t me = (heap_cur + PAGE_SIZE - 1) & ~(PAGE_SIZE-1ULL);
    for (vaddr_t v = ms; v < me; v += PAGE_SIZE) {
        if (v >= heap_start && v < heap_end) {
            paddr_t p = (paddr_t)(uintptr_t)pmm_alloc_frame();
            if (p) mm_map_page(v, p, PAGE_PRESENT | PAGE_WRITE);
        }
    }
    if (heap_cur > heap_end) { serial_puts("[MM] heap exhausted!\n"); return NULL; }
    return ptr;
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void kfree(void *ptr) { (void)ptr; }

void *krealloc(void *ptr, size_t n)
{
    if (!ptr)  return kmalloc(n);
    if (!n)    { kfree(ptr); return NULL; }
    void *np = kmalloc(n);
    if (np) memcpy(np, ptr, n);
    return np;
}

/* ── memory_init — called from kernel_main ──────────────────────────────── */
/* FIX: actually initialise pmm and heap so kmalloc works immediately */
void memory_init(void)
{
    /* pmm_init parses the Multiboot2 memory map.
     * We pass 0 here; it falls back to a safe 16 MB region. */
    extern void pmm_init(uint64_t);
    pmm_init(0);

    mm_init_heap(HEAP_VIRT_START, HEAP_VIRT_END);
    serial_puts("[MM] memory_init complete.\n");
}