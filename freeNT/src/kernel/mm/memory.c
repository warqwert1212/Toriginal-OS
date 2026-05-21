#include "mm.h"
#include "io.h"
#include "string.h"

/* Physical memory allocator */
static phys_allocator_t phys_allocator = {0};
static mem_zone_t mem_zones[4] = {0};
static int num_zones = 0;

/* Page table root (PML4) */
static pte_t *pml4 = NULL;

/* Kernel heap */
static vaddr_t heap_start = 0;
static vaddr_t heap_end = 0;
static vaddr_t heap_current = 0;

/* Initialize physical memory management */
void mm_init_physical(paddr_t mem_start, paddr_t mem_end) {
    uint64_t total_memory = mem_end - mem_start;
    uint64_t total_pages = total_memory / PAGE_SIZE;
    uint64_t bitmap_size = (total_pages + 7) / 8;  /* bits to bytes */
    
    /* Place bitmap at start of physical memory */
    phys_allocator.bitmap = (uint8_t *)mem_start;
    phys_allocator.total_pages = total_pages;
    phys_allocator.free_pages = total_pages;
    phys_allocator.allocated_pages = 0;
    
    /* Initialize bitmap - all pages marked as free initially */
    memset(phys_allocator.bitmap, 0, bitmap_size);
    
    /* Mark bitmap pages as allocated */
    uint64_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < bitmap_pages; i++) {
        phys_allocator.bitmap[i / 8] |= (1 << (i % 8));
        phys_allocator.allocated_pages++;
    }
    phys_allocator.free_pages -= bitmap_pages;
}

/* Allocate physical pages */
paddr_t mm_alloc_pages(size_t num_pages) {
    if (num_pages == 0)
        return 0;
    
    size_t pages_found = 0;
    uint64_t start_page = 0;
    
    for (uint64_t i = 0; i < phys_allocator.total_pages; i++) {
        uint8_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;
        
        if (!(phys_allocator.bitmap[byte_idx] & (1 << bit_idx))) {
            /* Page is free */
            if (pages_found == 0)
                start_page = i;
            pages_found++;
            
            if (pages_found == num_pages)
                break;
        } else {
            pages_found = 0;
        }
    }
    
    if (pages_found < num_pages)
        return 0;  /* Allocation failed */
    
    /* Mark pages as allocated */
    for (size_t i = 0; i < num_pages; i++) {
        uint64_t page = start_page + i;
        uint8_t byte_idx = page / 8;
        uint8_t bit_idx = page % 8;
        phys_allocator.bitmap[byte_idx] |= (1 << bit_idx);
    }
    
    phys_allocator.allocated_pages += num_pages;
    phys_allocator.free_pages -= num_pages;
    
    return start_page * PAGE_SIZE;
}

/* Deallocate physical pages */
void mm_free_pages(paddr_t paddr, size_t num_pages) {
    uint64_t start_page = paddr / PAGE_SIZE;
    
    for (size_t i = 0; i < num_pages; i++) {
        uint64_t page = start_page + i;
        uint8_t byte_idx = page / 8;
        uint8_t bit_idx = page % 8;
        phys_allocator.bitmap[byte_idx] &= ~(1 << bit_idx);
    }
    
    phys_allocator.allocated_pages -= num_pages;
    phys_allocator.free_pages += num_pages;
}

/* Initialize paging structures */
void mm_init_paging(void) {
    /* Allocate PML4 (top-level page table) */
    paddr_t pml4_paddr = mm_alloc_pages(1);
    pml4 = (pte_t *)(uintptr_t)pml4_paddr;
    memset(pml4, 0, PAGE_SIZE);
}

/* Enable paging (to be called from assembly) */
void mm_enable_paging(void) {
    if (!pml4)
        return;
    
    /* Set CR3 to PML4 address */
    uint64_t pml4_addr = (uint64_t)pml4;
    asm volatile("movq %0, %%cr3" : : "r"(pml4_addr));
    
    /* Enable paging by setting CR0.PG bit */
    uint64_t cr0;
    asm volatile("movq %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("movq %0, %%cr0" : : "r"(cr0));
}

/* Map a virtual page to physical page */
void mm_map_page(vaddr_t vaddr, paddr_t paddr, uint64_t flags) {
    /* Extract page table indices from virtual address */
    uint32_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint32_t pdp_idx = (vaddr >> 30) & 0x1FF;
    uint32_t pd_idx = (vaddr >> 21) & 0x1FF;
    uint32_t pt_idx = (vaddr >> 12) & 0x1FF;
    
    /* Navigate/create page tables */
    pte_t *pdp_table = NULL;
    pte_t *pd_table = NULL;
    pte_t *pt_table = NULL;
    
    /* Get/create PDP */
    if (!(pml4[pml4_idx].entry & PAGE_PRESENT)) {
        paddr_t pdp_paddr = mm_alloc_pages(1);
        pdp_table = (pte_t *)(uintptr_t)pdp_paddr;
        memset(pdp_table, 0, PAGE_SIZE);
        pml4[pml4_idx].entry = pdp_paddr | flags;
    } else {
        pdp_table = (pte_t *)(pml4[pml4_idx].entry & ~0xFFF);
    }
    
    /* Get/create PD */
    if (!(pdp_table[pdp_idx].entry & PAGE_PRESENT)) {
        paddr_t pd_paddr = mm_alloc_pages(1);
        pd_table = (pte_t *)(uintptr_t)pd_paddr;
        memset(pd_table, 0, PAGE_SIZE);
        pdp_table[pdp_idx].entry = pd_paddr | flags;
    } else {
        pd_table = (pte_t *)(pdp_table[pdp_idx].entry & ~0xFFF);
    }
    
    /* Get/create PT */
    if (!(pd_table[pd_idx].entry & PAGE_PRESENT)) {
        paddr_t pt_paddr = mm_alloc_pages(1);
        pt_table = (pte_t *)(uintptr_t)pt_paddr;
        memset(pt_table, 0, PAGE_SIZE);
        pd_table[pd_idx].entry = pt_paddr | flags;
    } else {
        pt_table = (pte_t *)(pd_table[pd_idx].entry & ~0xFFF);
    }
    
    /* Set page table entry */
    pt_table[pt_idx].entry = paddr | flags;
}

/* Unmap a virtual page */
void mm_unmap_page(vaddr_t vaddr) {
    uint32_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint32_t pdp_idx = (vaddr >> 30) & 0x1FF;
    uint32_t pd_idx = (vaddr >> 21) & 0x1FF;
    uint32_t pt_idx = (vaddr >> 12) & 0x1FF;
    
    pte_t *pdp_table = (pte_t *)(pml4[pml4_idx].entry & ~0xFFF);
    pte_t *pd_table = (pte_t *)(pdp_table[pdp_idx].entry & ~0xFFF);
    pte_t *pt_table = (pte_t *)(pd_table[pd_idx].entry & ~0xFFF);
    
    pt_table[pt_idx].entry = 0;
}

/* Initialize kernel heap */
void mm_init_heap(vaddr_t heap_start_addr, vaddr_t heap_end_addr) {
    heap_start = heap_start_addr;
    heap_end = heap_end_addr;
    heap_current = heap_start;
    
    /* Map initial heap pages */
    size_t initial_pages = 16;  /* 64KB initial heap */
    for (size_t i = 0; i < initial_pages; i++) {
        paddr_t paddr = mm_alloc_pages(1);
        vaddr_t vaddr = heap_start + i * PAGE_SIZE;
        mm_map_page(vaddr, paddr, PAGE_PRESENT | PAGE_WRITE);
    }
}

/* Kernel malloc */
void* kmalloc(size_t size) {
    if (size == 0)
        return NULL;
    
    size = (size + 15) & ~15;  /* Align to 16 bytes */
    
    void *ptr = (void *)heap_current;
    heap_current += size;
    
    if (heap_current > heap_end) {
        /* Allocate more pages if needed */
        size_t pages_needed = ((heap_current - heap_start) + PAGE_SIZE - 1) / PAGE_SIZE;
        size_t pages_allocated = ((void *)heap_current - (void *)heap_start) / PAGE_SIZE;
        
        while (pages_allocated < pages_needed) {
            paddr_t paddr = mm_alloc_pages(1);
            vaddr_t vaddr = heap_start + pages_allocated * PAGE_SIZE;
            mm_map_page(vaddr, paddr, PAGE_PRESENT | PAGE_WRITE);
            pages_allocated++;
        }
    }
    
    return ptr;
}

/* Kernel free */
void kfree(void *ptr) {
    /* Simple allocator - no actual freeing in this version */
    (void)ptr;
}

/* Kernel realloc */
void* krealloc(void *ptr, size_t new_size) {
    if (!ptr)
        return kmalloc(new_size);
    
    /* For this simple allocator, just allocate new block */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr)
        return NULL;
    
    /* Copy old data (assume previous allocation was tracked somewhere) */
    return new_ptr;
}
