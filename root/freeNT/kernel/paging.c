/* paging.c - real per-process address spaces.
 *
 * WHY THIS FILE EXISTS:
 * Before this, there was no live per-process paging at all. memory.c
 * defines mm_init_paging()/mm_enable_paging()/a static `pml4`, but
 * NOTHING calls either function anywhere in the kernel (confirmed by
 * grep, not assumed) - the page tables actually active at runtime are
 * the ones boot64.s hand-builds directly in assembly before jumping
 * to long_mode_start (pd_table0..3: 2 MiB huge pages flat-identity-
 * mapping 0..4 GiB), entirely separate from memory.c's dead API.
 * process_t had a page_table_root field and cpu_context_t had a cr3
 * field, but nothing ever allocated a real per-process PML4 into the
 * former, and scheduler_yield() never wrote the latter into %cr3 on a
 * context switch (see process.c's frame_to_context() comment - this
 * gap was known and flagged in-place, not missed silently). Net
 * effect: every process, no matter how many existed, ran in the exact
 * same boot-time 2 MiB-huge-page address space as the kernel itself,
 * with the exact same permissions. That's not "isolation with a bug
 * in it" - it's no isolation at all.
 *
 * This file's mappings are 4 KiB (not 2 MiB huge pages like boot64.s
 * uses) - deliberate: PAGE_USER needs to be grantable per normal page
 * for a process's own memory without also exposing the other 2 MiB of
 * whatever huge page it shares with kernel data, and process code/heap
 * regions are typically far smaller than 2 MiB anyway. Once
 * paging_switch_to() below loads a process's own PML4, the boot-time
 * tables are simply no longer the active ones for that CPU - they're
 * never freed (nothing else claims that memory) but also never
 * consulted again once every process has real tables of its own.
 *
 * This file adds the real thing: mm_create_address_space() builds a
 * fresh PML4 for a process, mm_map_page_in() maps a page into a
 * SPECIFIC address space (not just the current global one), and
 * paging_switch_to() is what scheduler_yield() now calls on every
 * context switch to actually load %cr3.
 *
 * THE SHARED KERNEL RANGE:
 * This kernel is not higher-half (see kernel64.ld: loaded at 0x200000,
 * flat identity-mapped, no 0xFFFF800000000000 split despite what an
 * old, since-deleted BUILD.txt claimed). That means kernel code, the
 * IDT/GDT/TSS, the heap, and device MMIO all live in the SAME
 * low-physical-address range every process's own code will eventually
 * want to avoid colliding with. The standard, correct fix for a flat
 * (non-higher-half) kernel is: every process's PML4 maps that shared
 * low range (0 .. KERNEL_SHARED_TOP) with the exact same
 * physical->virtual identity mapping, kernel-only (no PAGE_USER),
 * alongside whatever the process's own user-mode mappings are. That
 * way a `mov %cr3` mid-interrupt (a timer IRQ firing while a user
 * process is running) never yanks the kernel's own code/IDT/stack out
 * from under itself - every address space agrees on what's at those
 * addresses, they just disagree on what's mapped ABOVE that range.
 *
 * KERNEL_SHARED_TOP is set past .trp_code_area's end (see kernel64.ld)
 * so every process's address space includes the fixed TRP load window
 * too - each process gets ITS OWN physical frames mapped at that same
 * virtual address (0x500000), which is what makes multiple processes
 * running TRP binaries concurrently possible; see trploader.c's
 * updated trp_exec_bin() for the other half of that.
 */

#include "paging.h"
#include "pmm.h"
#include "string.h"
#include "serial.h"

#define PAGE_SIZE 4096

#define PAGE_PRESENT    0x001ULL
#define PAGE_WRITE      0x002ULL
#define PAGE_USER       0x004ULL
#define PAGE_NX         (1ULL << 63)

typedef struct { uint64_t entry; } pml_entry_t;

/* Past __trp_code_area_end (0x600000) with round-number headroom.
 * Anything the kernel itself uses - its own image, heap, page-table
 * frames the PMM hands out for ITS OWN bookkeeping, the TRP load
 * window - must stay under this so the shared mapping (identical in
 * every process's PML4) actually covers it. kernel64.ld's own ASSERT
 * already guarantees __kernel_end <= 0x2000000 (32 MiB); this is set
 * well above that so kernel growth doesn't quietly walk past it the
 * way it once did with the old fixed TRP address (see kernel64.ld's
 * comment on why .trp_code_area moved from 0x300000 to 0x500000 in
 * the first place - same class of bug, closed here with margin). */
#define KERNEL_SHARED_TOP 0x4000000ULL /* 64 MiB */

extern uint8_t __kernel_end[];

/* NOTE: this kernel's boot-time mapping (memory.c's mm_init_paging(),
 * loaded per kernel64.ld at a flat 0x200000, no higher-half split) IS
 * a pure identity map: every physical address pa is mapped at virtual
 * address va == pa, for the whole low range. That means "read the
 * boot PML4 and copy its kernel mappings" and "just build a fresh
 * identity map for 0..KERNEL_SHARED_TOP" produce IDENTICAL results
 * here - so map_shared_kernel_range() below does the latter (simpler,
 * no dependency on the boot PML4's internal layout). memory.c still
 * exposes mm_get_boot_pml4() as an accessor for the boot-time PML4
 * itself, kept for any future caller that needs to inspect it
 * directly (e.g. a future higher-half migration, where this
 * "identity map == boot map" equivalence would stop holding and this
 * file would need to actually walk it instead). */

static pml_entry_t *walk_or_alloc(pml_entry_t *table, uint32_t idx, uint64_t extra_flags)
{
    if (!(table[idx].entry & PAGE_PRESENT)) {
        paddr_t p = (paddr_t)(uintptr_t)pmm_alloc_frame();
        if (!p) return NULL;
        pml_entry_t *child = (pml_entry_t *)(uintptr_t)p;
        memset(child, 0, PAGE_SIZE);
        table[idx].entry = (uint64_t)p | PAGE_PRESENT | PAGE_WRITE | (extra_flags & PAGE_USER);
        return child;
    }
    /* Widen an already-present intermediate table's permissions if this
     * caller needs PAGE_USER and it wasn't set before - an entry can be
     * shared by an earlier kernel-only mapping and a later user mapping
     * that walks through the same PML4/PDPT/PD slot (e.g. two different
     * processes' identical shared-range setup calls). Never clears bits
     * another mapping already relies on, only adds PAGE_USER when asked. */
    if ((extra_flags & PAGE_USER) && !(table[idx].entry & PAGE_USER))
        table[idx].entry |= PAGE_USER;
    return (pml_entry_t *)(uintptr_t)(table[idx].entry & ~0xFFFULL);
}

static void map_page_into(pml_entry_t *pml4, uint64_t va, uint64_t pa, uint64_t flags)
{
    uint32_t i4 = (va >> 39) & 0x1FFu;
    uint32_t i3 = (va >> 30) & 0x1FFu;
    uint32_t i2 = (va >> 21) & 0x1FFu;
    uint32_t i1 = (va >> 12) & 0x1FFu;

    pml_entry_t *l3 = walk_or_alloc(pml4, i4, flags); if (!l3) return;
    pml_entry_t *l2 = walk_or_alloc(l3,   i3, flags); if (!l2) return;
    pml_entry_t *l1 = walk_or_alloc(l2,   i2, flags); if (!l1) return;

    l1[i1].entry = (pa & ~0xFFFULL) | PAGE_PRESENT | flags;
}

/* Populate `pml4`'s low range with the exact same physical=virtual
 * identity mapping the boot PML4 already has for 0..KERNEL_SHARED_TOP,
 * kernel-only (no PAGE_USER) so user-mode code can't read kernel
 * memory just because it's mapped into the same address space - it's
 * mapped for the KERNEL's benefit (so an interrupt mid-user-code still
 * finds its own code/IDT/stack immediately after the cr3 switch back),
 * not for the user process to touch. */
/* The boot PML4 (identity-mapped 0..4 GiB with 2 MiB pages, which
 * includes all MMIO: Local APIC/IOAPIC, the framebuffer, PCI BARs).
 * Recorded once, early, while %cr3 is still the boot table. */
static uint64_t g_boot_cr3 = 0;
void mm_record_boot_cr3(void)
{
    uint64_t c;
    __asm__ volatile("mov %%cr3, %0" : "=r"(c));
    g_boot_cr3 = c & ~0xFFFULL;
}

static void map_shared_kernel_range(pml_entry_t *pml4)
{
    for (uint64_t pa = 0; pa < KERNEL_SHARED_TOP; pa += PAGE_SIZE) {
        map_page_into(pml4, pa, pa, PAGE_WRITE); /* present|write, no PAGE_USER */
    }

    /* FIX: everything ABOVE KERNEL_SHARED_TOP used to be left unmapped in
     * a process's address space. The kernel runs under the process's %cr3
     * for every syscall and every interrupt that lands while user code
     * runs, so the first timer tick touched the Local APIC (0xFEE00000) -
     * and the first console write touched the framebuffer - and
     * page-faulted. Ring-3 programs therefore died on their first
     * interrupt. Share the boot identity map for 64 MiB..4 GiB
     * (kernel-only, no PAGE_USER): this process's own PD for 0..1 GiB
     * gets the boot PD's 2 MiB entries from index 32 up, and PDPT slots
     * 1..3 point at the boot PDs directly. */
    if (g_boot_cr3) {
        pml_entry_t *bp4 = (pml_entry_t *)(uintptr_t)g_boot_cr3;
        if (!(bp4[0].entry & PAGE_PRESENT) || !(pml4[0].entry & PAGE_PRESENT)) return;
        pml_entry_t *bl3 = (pml_entry_t *)(uintptr_t)(bp4[0].entry & ~0xFFFULL);
        pml_entry_t *nl3 = (pml_entry_t *)(uintptr_t)(pml4[0].entry & ~0xFFFULL);
        if ((bl3[0].entry & PAGE_PRESENT) && (nl3[0].entry & PAGE_PRESENT)) {
            pml_entry_t *bl2 = (pml_entry_t *)(uintptr_t)(bl3[0].entry & ~0xFFFULL);
            pml_entry_t *nl2 = (pml_entry_t *)(uintptr_t)(nl3[0].entry & ~0xFFFULL);
            for (uint32_t j = (uint32_t)(KERNEL_SHARED_TOP >> 21); j < 512; j++)
                nl2[j].entry = bl2[j].entry;
        }
        for (uint32_t i3 = 1; i3 < 4; i3++) nl3[i3].entry = bl3[i3].entry;
    }
}

paddr_t mm_create_address_space(void)
{
    paddr_t pa = (paddr_t)(uintptr_t)pmm_alloc_frame();
    if (!pa) {
        serial_puts("[PAGING] OOM allocating new PML4.\n");
        return 0;
    }
    pml_entry_t *pml4 = (pml_entry_t *)(uintptr_t)pa;
    memset(pml4, 0, PAGE_SIZE);
    map_shared_kernel_range(pml4);
    return pa;
}

/* Clears the PTE for va in pml4_phys's address space and returns the
 * physical frame that WAS mapped there (0 if nothing was mapped, or
 * if any intermediate table level wasn't present - i.e. "nothing to
 * unmap" is reported the same way as "already unmapped", which is
 * the correct, idempotent behavior a caller freeing frames on brk()
 * shrink or process teardown wants: it should never double-free or
 * fault just because a page in the requested range was never
 * actually backed in the first place). Does NOT free the frame
 * itself - the caller (process_brk()'s shrink path, eventually
 * process teardown) decides that, since sometimes a caller wants to
 * KEEP the frame (e.g. remapping it elsewhere) rather than free it. */
paddr_t mm_unmap_page_in(paddr_t pml4_phys, uint64_t va)
{
    if (!pml4_phys) return 0;
    pml_entry_t *pml4 = (pml_entry_t *)(uintptr_t)pml4_phys;

    uint32_t i4 = (va >> 39) & 0x1FFu;
    uint32_t i3 = (va >> 30) & 0x1FFu;
    uint32_t i2 = (va >> 21) & 0x1FFu;
    uint32_t i1 = (va >> 12) & 0x1FFu;

    if (!(pml4[i4].entry & PAGE_PRESENT)) return 0;
    pml_entry_t *l3 = (pml_entry_t *)(uintptr_t)(pml4[i4].entry & ~0xFFFULL);

    if (!(l3[i3].entry & PAGE_PRESENT)) return 0;
    pml_entry_t *l2 = (pml_entry_t *)(uintptr_t)(l3[i3].entry & ~0xFFFULL);

    if (!(l2[i2].entry & PAGE_PRESENT)) return 0;
    pml_entry_t *l1 = (pml_entry_t *)(uintptr_t)(l2[i2].entry & ~0xFFFULL);

    uint64_t pte = l1[i1].entry;
    if (!(pte & PAGE_PRESENT)) return 0;

    paddr_t frame = (paddr_t)(pte & ~0xFFFULL);
    l1[i1].entry = 0;
    return frame;
    /* No invlpg here, same reasoning as mm_map_page_in() - the caller
     * (process_brk()) explicitly invalidates when unmapping from the
     * CURRENTLY loaded address space, since only it knows whether
     * that's actually the case. */
}

void mm_map_page_in(paddr_t pml4_phys, uint64_t va, uint64_t pa, uint64_t flags)
{
    if (!pml4_phys) return;
    map_page_into((pml_entry_t *)(uintptr_t)pml4_phys, va, pa, flags);
    /* No invlpg here on purpose: this may be mapping into an address
     * space that ISN'T the one currently loaded in %cr3 (the common
     * case - setting up a new process's memory before it ever runs).
     * invlpg only flushes the CURRENTLY ACTIVE TLB's entry for that VA;
     * flushing it here would do nothing useful when the target isn't
     * live yet, and would be actively wrong if some OTHER process
     * happens to have that same VA valid right now. paging_switch_to()
     * loading a fresh %cr3 flushes the ENTIRE TLB (except global
     * pages, which this kernel doesn't use) as an unconditional side
     * effect of the reload - correct and sufficient for the case that
     * matters (a process's own address space changing while it's the
     * one currently running, e.g. brk()/mmap() growing it: see
     * process_brk()/process_mmap_anon() in process.c, updated to call
     * this and then explicitly invlpg only when modifying the
     * CURRENTLY loaded address space - see paging_invalidate_current()). */
}

void paging_invalidate_current(uint64_t va)
{
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
}

void paging_switch_to(paddr_t pml4_phys)
{
    if (!pml4_phys) return;
    __asm__ volatile("movq %0, %%cr3" :: "r"((uint64_t)pml4_phys) : "memory");
}

paddr_t paging_current(void)
{
    uint64_t cr3;
    __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
    return (paddr_t)cr3;
}

/* Walks the CURRENTLY loaded PML4 (whatever process is running right
 * now - always the syscall caller, since this is only ever called
 * from within syscall_ptr_ok()/syscall_buf_ok() in syscall.c) and
 * confirms va is actually mapped PRESENT and USER. Returns 0 (not
 * accessible) for an unmapped page, a kernel-only page (no
 * PAGE_USER), or a not-present page table at any of the 4 levels.
 *
 * WHY THIS NEEDS TO EXIST: syscall.c's syscall_ptr_ok()/
 * syscall_buf_ok() previously only checked "is this pointer value
 * numerically above 0x1000" - a NULL-pointer-style sanity check, not
 * a real validity check. Before per-process paging existed (see this
 * file's top comment), that was roughly as good as it could get,
 * because every process shared the kernel's own address space and
 * "any non-tiny pointer" mapped to SOMETHING - not safely, but not
 * to a hard kernel-vs-user boundary either, since that boundary
 * didn't exist yet. Now that every process has its own real address
 * space, that old check is actively insufficient: a user process
 * could pass a syscall a pointer into memory it never mapped at all
 * (previously would page-fault - now still does, so not a new crash
 * risk) or, more importantly, the kernel had NO way to confirm a
 * pointer a process claims is its own buffer is actually mapped
 * PAGE_USER in that process's OWN table before touching it with
 * kernel privileges on the process's behalf. This closes that gap. */
int paging_user_range_ok(uint64_t va, uint64_t len)
{
    if (len == 0) return 1;
    if (va + len < va) return 0; /* overflow, same check syscall_buf_ok already did */

    uint64_t cr3;
    __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
    pml_entry_t *pml4 = (pml_entry_t *)(uintptr_t)(cr3 & ~0xFFFULL);

    uint64_t start_page = va & ~0xFFFULL;
    uint64_t end_page    = (va + len - 1) & ~0xFFFULL;

    for (uint64_t page = start_page; ; page += PAGE_SIZE) {
        uint32_t i4 = (page >> 39) & 0x1FFu;
        uint32_t i3 = (page >> 30) & 0x1FFu;
        uint32_t i2 = (page >> 21) & 0x1FFu;
        uint32_t i1 = (page >> 12) & 0x1FFu;

        if (!(pml4[i4].entry & PAGE_PRESENT)) return 0;
        pml_entry_t *l3 = (pml_entry_t *)(uintptr_t)(pml4[i4].entry & ~0xFFFULL);

        if (!(l3[i3].entry & PAGE_PRESENT)) return 0;
        pml_entry_t *l2 = (pml_entry_t *)(uintptr_t)(l3[i3].entry & ~0xFFFULL);

        if (!(l2[i2].entry & PAGE_PRESENT)) return 0;
        pml_entry_t *l1 = (pml_entry_t *)(uintptr_t)(l2[i2].entry & ~0xFFFULL);

        uint64_t pte = l1[i1].entry;
        if (!(pte & PAGE_PRESENT) || !(pte & PAGE_USER)) return 0;

        if (page == end_page) break;
    }
    return 1;
}
