// ==============================================================================
// VMM.C - 4-Level Paging Implementation
// ==============================================================================
#include "vmm.h"
#include "pmm.h"

void print_serial(const char* str);

// Bitwise cracking of virtual layout coordinates
#define PML4_INDEX(v) (((v) >> 39) & 0x1FF)
#define PDPT_INDEX(v) (((v) >> 30) & 0x1FF)
#define PD_INDEX(v)   (((v) >> 21) & 0x1FF)
#define PT_INDEX(v)   (((v) >> 12) & 0x1FF)

// Recursive access pointers matching entry.s slot 511
static uint64_t* const PML4_RECURSIVE = (uint64_t*)0xFFFFFFFFFFFFF000;

void vmm_init(void) {
    print_serial("[VMM] Virtual Memory Management Active via Recursive Paging.\n");
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx   = PD_INDEX(virt);
    uint64_t pt_idx   = PT_INDEX(virt);

    // 1. Traverse / Create PDPT window
    if (!(PML4_RECURSIVE[pml4_idx] & PAGE_PRESENT)) {
        void* new_table = pmm_alloc_frame();
        PML4_RECURSIVE[pml4_idx] = (uint64_t)new_table | PAGE_PRESENT | PAGE_WRITABLE | flags;
        // Clean newly assigned block
        uint64_t* dest = (uint64_t*)(0xFFFFFFFFFFFFC000 + (pml4_idx * PAGE_SIZE));
        for(int i=0; i<512; i++) dest[i] = 0;
    }

    // 2. Traverse / Create PD window
    uint64_t* pdpt = (uint64_t*)(0xFFFFFFFFFFE00000 + (pml4_idx * 0x4000));
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        void* new_table = pmm_alloc_frame();
        pdpt[pdpt_idx] = (uint64_t)new_table | PAGE_PRESENT | PAGE_WRITABLE | flags;
        uint64_t* dest = (uint64_t*)(0xFFFFFFFFC0000000 + (pml4_idx * 0x800000) + (pdpt_idx * PAGE_SIZE));
        for(int i=0; i<512; i++) dest[i] = 0;
    }

    // 3. Traverse / Create PT window
    uint64_t* pd = (uint64_t*)(0xFFFFFFFFC0000000 + (pml4_idx * 0x800000) + (pdpt_idx * 0x4000));
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        void* new_table = pmm_alloc_frame();
        pd[pd_idx] = (uint64_t)new_table | PAGE_PRESENT | PAGE_WRITABLE | flags;
        uint64_t* dest = (uint64_t*)(0xFFFFFF8000000000 + (pml4_idx * 0x400000000) + (pdpt_idx * 0x2000000) + (pd_idx * PAGE_SIZE));
        for(int i=0; i<512; i++) dest[i] = 0;
    }

    // 4. Set leaf page descriptor properties
    uint64_t* pt = (uint64_t*)(0xFFFFFF8000000000 + (pml4_idx * 0x400000000) + (pdpt_idx * 0x2000000) + (pd_idx * 0x4000));
    pt[pt_idx] = (phys & ~0xFFF) | PAGE_PRESENT | flags;

    // Flush Translation Lookaside Buffer (TLB)
    __asm__ volatile("invlpg [%0]" : : "r"(virt) : "memory");
}

void vmm_unmap_page(uint64_t virt) {
    uint64_t pml4_idx = PML4_INDEX(virt);
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t pd_idx   = PD_INDEX(virt);
    uint64_t pt_idx   = PT_INDEX(virt);

    uint64_t* pt = (uint64_t*)(0xFFFFFF8000000000 + (pml4_idx * 0x400000000) + (pdpt_idx * 0x2000000) + (pd_idx * 0x4000));
    if (pt[pt_idx] & PAGE_PRESENT) {
        pt[pt_idx] = 0;
        __asm__ volatile("invlpg [%0]" : : "r"(virt) : "memory");
    }
}