#ifndef _KERNEL_PAGING_H
#define _KERNEL_PAGING_H

#include "types.h"

#define PAGING_PRESENT   0x001ULL
#define PAGING_WRITE     0x002ULL
#define PAGING_USER      0x004ULL

/* Allocates and returns the physical address of a brand-new PML4 with
 * the shared kernel range (see paging.c) already mapped in, kernel-
 * only. Returns 0 on OOM. This is what process_create() now stores
 * into process_t.page_table_root instead of the old bare
 * mm_alloc_pages(1) (a single physical frame with nothing mapped into
 * it at all - not usable as a real PML4). */
paddr_t mm_create_address_space(void);
void    mm_record_boot_cr3(void);   /* call once, early, before any process exists */

/* Maps one 4 KiB page into a SPECIFIC address space (identified by its
 * PML4's physical address), not necessarily the one currently loaded
 * in %cr3. flags is any combination of PAGING_WRITE/PAGING_USER
 * (PAGING_PRESENT is implied and always set). */
void mm_map_page_in(paddr_t pml4_phys, uint64_t va, uint64_t pa, uint64_t flags);

/* Clears the mapping for va in a specific address space and returns
 * the physical frame that was mapped there (0 if nothing was mapped -
 * idempotent, safe to call on an already-unmapped page). Does not
 * free the returned frame; the caller decides that (see
 * process_brk()'s shrink path in process.c for the typical use: unmap
 * then pmm_free_frame() the result). Like mm_map_page_in(), does not
 * invalidate the TLB itself - call paging_invalidate_current() after,
 * if and only if pml4_phys is the currently loaded address space. */
paddr_t mm_unmap_page_in(paddr_t pml4_phys, uint64_t va);

/* Loads pml4_phys into %cr3 - the actual address-space switch.
 * Flushes the entire TLB as an unconditional side effect (this kernel
 * doesn't use global pages), which is what makes a fresh mapping
 * elsewhere (mm_map_page_in on a NON-current address space) safe
 * without a separate invlpg - see mm_map_page_in()'s comment. */
void paging_switch_to(paddr_t pml4_phys);

/* Returns the physical address currently loaded in %cr3. */
paddr_t paging_current(void);

/* Confirms every page in [va, va+len) is present AND user-accessible
 * in the CURRENTLY loaded address space. This is the real check
 * syscall.c's syscall_ptr_ok()/syscall_buf_ok() now use before the
 * kernel dereferences a user-supplied pointer on that process's
 * behalf - see paging.c's comment on this function for why the old
 * "is it above 0x1000" check stopped being sufficient once real
 * per-process address spaces existed. len==0 always returns 1 (an
 * empty range is trivially "ok", matching syscall_buf_ok's existing
 * len==0 short-circuit before this was added). */
int paging_user_range_ok(uint64_t va, uint64_t len);

/* invlpg for a single page in the CURRENTLY loaded address space only
 * - use this (not mm_map_page_in, which never flushes) when modifying
 * the address space a process is actively running in right now, e.g.
 * process_brk()/process_mmap_anon() growing the CURRENT process's own
 * mappings. */
void paging_invalidate_current(uint64_t va);

#endif /* _KERNEL_PAGING_H */
