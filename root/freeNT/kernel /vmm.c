// ==============================================================================
// VMM.C - 4-Level Paging Virtual Memory Manager
// Fixed: invlpg AT&T syntax  (was Intel syntax — crashes with GCC)
// Fixed: all physical allocations go through pmm_alloc_frame()
// ==============================================================================

#include "vmm.h"
#include "pmm.h"
#include "string.h"

void print_serial(const char *str);

// ---------------------------------------------------------------------------
// Virtual address field extraction
// ---------------------------------------------------------------------------
#define PML4_INDEX(v) (((uint64_t)(v) >> 39) & 0x1FF)
#define PDPT_INDEX(v) (((uint64_t)(v) >> 30) & 0x1FF)
#define PD_INDEX(v)   (((uint64_t)(v) >> 21) & 0x1FF)
#define PT_INDEX(v)   (((uint64_t)(v) >> 12) & 0x1FF)

// ---------------------------------------------------------------------------
// Recursive mapping slot — entry.s maps PML4[511] -> PML4 itself.
// This lets us reach any page table with a fixed virtual address formula.
// ---------------------------------------------------------------------------
static uint64_t * const PML4_RECURSIVE = (uint64_t *)0xFFFFFFFFFFFFF000ULL;

void vmm_init(void) {
    print_serial("[VMM] Virtual Memory Manager active (recursive paging).\n");
}

// ---------------------------------------------------------------------------
// vmm_map_page — map virt -> phys with given flags
// ---------------------------------------------------------------------------
void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx   = PD_INDEX(virt);
    uint64_t pt_idx   = PT_INDEX(virt);

    // --- Level 1: PML4 -> PDPT ---
    if (!(PML4_RECURSIVE[pml4_idx] & PAGE_PRESENT)) {
        void *new_table = pmm_alloc_frame();
        if (!new_table) { print_serial("[VMM] OOM allocating PDPT\n"); return; }
        PML4_RECURSIVE[pml4_idx] = (uint64_t)new_table | PAGE_PRESENT | PAGE_WRITABLE | flags;
        // Zero it via recursive address
        uint64_t *dest = (uint64_t *)(0xFFFFFFFFFFFFC000ULL + (pml4_idx * PAGE_SIZE));
        for (int i = 0; i < 512; i++) dest[i] = 0;
    }

    // --- Level 2: PDPT -> PD ---
    uint64_t *pdpt = (uint64_t *)(0xFFFFFFFFFFE00000ULL + (pml4_idx * 0x1000ULL));
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        void *new_table = pmm_alloc_frame();
        if (!new_table) { print_serial("[VMM] OOM allocating PD\n"); return; }
        pdpt[pdpt_idx] = (uint64_t)new_table | PAGE_PRESENT | PAGE_WRITABLE | flags;
        uint64_t *dest = (uint64_t *)(0xFFFFFFFFC0000000ULL
                         + (pml4_idx * 0x200000ULL)
                         + (pdpt_idx * PAGE_SIZE));
        for (int i = 0; i < 512; i++) dest[i] = 0;
    }

    // --- Level 3: PD -> PT ---
    uint64_t *pd = (uint64_t *)(0xFFFFFFFFC0000000ULL
                   + (pml4_idx * 0x200000ULL)
                   + (pdpt_idx * 0x1000ULL));
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        void *new_table = pmm_alloc_frame();
        if (!new_table) { print_serial("[VMM] OOM allocating PT\n"); return; }
        pd[pd_idx] = (uint64_t)new_table | PAGE_PRESENT | PAGE_WRITABLE | flags;
        uint64_t *dest = (uint64_t *)(0xFFFFFF8000000000ULL
                         + (pml4_idx * 0x40000000ULL)
                         + (pdpt_idx * 0x200000ULL)
                         + (pd_idx   * PAGE_SIZE));
        for (int i = 0; i < 512; i++) dest[i] = 0;
    }

    // --- Level 4: PT -> physical page ---
    uint64_t *pt = (uint64_t *)(0xFFFFFF8000000000ULL
                   + (pml4_idx * 0x40000000ULL)
                   + (pdpt_idx * 0x200000ULL)
                   + (pd_idx   * 0x1000ULL));
    pt[pt_idx] = (phys & ~0xFFFULL) | PAGE_PRESENT | flags;

    // --- Flush TLB for this page ---
    // FIXED: AT&T syntax  (%0)  not Intel syntax  [%0]
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

// ---------------------------------------------------------------------------
// vmm_unmap_page
// ---------------------------------------------------------------------------
void vmm_unmap_page(uint64_t virt) {
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx   = PD_INDEX(virt);
    uint64_t pt_idx   = PT_INDEX(virt);

    if (!(PML4_RECURSIVE[pml4_idx] & PAGE_PRESENT)) return;

    uint64_t *pdpt = (uint64_t *)(0xFFFFFFFFFFE00000ULL + (pml4_idx * 0x1000ULL));
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return;

    uint64_t *pd = (uint64_t *)(0xFFFFFFFFC0000000ULL
                   + (pml4_idx * 0x200000ULL)
                   + (pdpt_idx * 0x1000ULL));
    if (!(pd[pd_idx] & PAGE_PRESENT)) return;

    uint64_t *pt = (uint64_t *)(0xFFFFFF8000000000ULL
                   + (pml4_idx * 0x40000000ULL)
                   + (pdpt_idx * 0x200000ULL)
                   + (pd_idx   * 0x1000ULL));

    pt[pt_idx] = 0;

    // FIXED: AT&T syntax
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

// ---------------------------------------------------------------------------
// vmm_get_phys — walk page tables to find physical address for a virtual one
// Returns 0 if not mapped.
// ---------------------------------------------------------------------------
uint64_t vmm_get_phys(uint64_t virt) {
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx   = PD_INDEX(virt);
    uint64_t pt_idx   = PT_INDEX(virt);

    if (!(PML4_RECURSIVE[pml4_idx] & PAGE_PRESENT)) return 0;

    uint64_t *pdpt = (uint64_t *)(0xFFFFFFFFFFE00000ULL + (pml4_idx * 0x1000ULL));
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return 0;

    uint64_t *pd = (uint64_t *)(0xFFFFFFFFC0000000ULL
                   + (pml4_idx * 0x200000ULL)
                   + (pdpt_idx * 0x1000ULL));
    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;

    uint64_t *pt = (uint64_t *)(0xFFFFFF8000000000ULL
                   + (pml4_idx * 0x40000000ULL)
                   + (pdpt_idx * 0x200000ULL)
                   + (pd_idx   * 0x1000ULL));

    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;

    return (pt[pt_idx] & ~0xFFFULL) | (virt & 0xFFFULL);
}