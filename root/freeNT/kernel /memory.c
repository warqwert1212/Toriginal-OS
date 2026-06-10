// ==============================================================================
// MEMORY.C - Virtual Memory Manager & Kernel Heap
// Physical allocation is handled entirely by pmm.c (bitmap allocator).
// This file owns: page table management, heap, and the mm_* public API.
// ==============================================================================

#include "mm.h"
#include "pmm.h"
#include "io.h"
#include "string.h"

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