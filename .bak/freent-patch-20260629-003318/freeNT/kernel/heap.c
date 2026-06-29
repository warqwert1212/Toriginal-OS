/* =============================================================================
 * heap.c — Kernel dynamic memory allocator (free-list, with coalescing)
 *
 * Replaces the old bump-pointer allocator that used to live in memory.c,
 * where kfree() was a permanent no-op and every allocation was leaked for
 * the lifetime of the kernel. That was fine for a kernel that allocated a
 * handful of long-lived structures at boot and never freed anything, but
 * it falls over immediately under any real workload — a shell running
 * `cat` or `cp` repeatedly, a filesystem doing per-call scratch buffers,
 * a process table churning through process_create()/free — all of those
 * leak memory every single call under the old allocator.
 *
 * Design: classic K&R-style free-list allocator with block headers and
 * O(1) coalescing via two intrusive doubly-linked lists per block:
 *
 *   - "all" list:  every block in the heap, address-ordered, immutable
 *                  once a block is created (only changes when a block is
 *                  split or merged with the truly-adjacent block at
 *                  alloc/free time). Used to find a block's physical
 *                  neighbors for coalescing without a footer.
 *   - "free" list: only free blocks, used for the first-fit allocation
 *                  search. A block is on this list iff in_use == 0.
 *
 * Block layout in memory:
 *
 *     [ heap_block_t header (32 bytes) ][ payload (block->size bytes) ]
 *
 * The pointer returned to callers always points at the start of the
 * payload, immediately after the header — so kfree(ptr) recovers the
 * header via simple pointer arithmetic: (heap_block_t*)ptr - 1.
 *
 * Growth: when no free block is large enough, the arena is extended by
 * mapping additional pages (via mm_map_page(), same mechanism the old
 * bump allocator used) and the new space is formatted as one large free
 * block, which is then merged with the heap's tail block if that block
 * happens to be free, then placed on the free list and the search retries.
 *
 * Corruption / double-free detection: every header carries a magic
 * number that is checked on every free() and overwritten on free so a
 * double-free is caught immediately instead of silently corrupting the
 * free list.
 * ========================================================================= */

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

/* Forward declarations of paging primitives implemented in memory.c.
 * heap.c owns allocation policy; memory.c owns physical/virtual memory
 * mechanics (page tables, frame allocation). Kept as two files on
 * purpose — different responsibilities, different rates of change. */
extern void mm_map_page(vaddr_t vaddr, paddr_t paddr, uint64_t flags);

/* Kernel heap virtual address window (above the 2 MiB GRUB load point).
 * 16 MiB – 80 MiB gives 64 MiB of virtual heap space. */
#define HEAP_VIRT_START  0x0000000001000000ULL   /* 16 MiB */
#define HEAP_VIRT_END    0x0000000005000000ULL   /* 80 MiB */

/* Pages to map per growth step when the allocator needs more arena. */
#define HEAP_GROW_PAGES   16u                     /* 64 KiB per growth */

/* Initial pages mapped eagerly at mm_init_heap() time, before the first
 * real allocation, so boot code doesn't pay a page-fault-shaped latency
 * (there is no page-fault handler wired up yet at that point in boot —
 * see memory.c's mm_enable_paging() comment) on its very first kmalloc. */
#define HEAP_PREMAP_PAGES 16u                     /* 64 KiB */

#define HEAP_MAGIC_FREE   0x46524545554E4B55ULL   /* "FREEUNKU" ascii-ish */
#define HEAP_MAGIC_USED   0x5553454442554B55ULL   /* "USEDBUKU" ascii-ish */

#define HEAP_ALIGN        16u

/* ── Block header ──────────────────────────────────────────────────────── */

typedef struct heap_block {
    uint64_t            size;       /* payload size in bytes, 16-aligned  */
    uint64_t            magic;      /* HEAP_MAGIC_FREE or HEAP_MAGIC_USED */
    struct heap_block  *all_next;   /* next block by address (or NULL)    */
    struct heap_block  *all_prev;   /* prev block by address (or NULL)    */
    struct heap_block  *free_next;  /* next FREE block (valid iff free)   */
    struct heap_block  *free_prev;  /* prev FREE block (valid iff free)   */
} heap_block_t;

#define HEAP_HEADER_SIZE  (sizeof(heap_block_t))

/* ── Module state ──────────────────────────────────────────────────────── */

static vaddr_t       heap_start    = 0;   /* first byte of the VA window     */
static vaddr_t       heap_end      = 0;   /* one-past-last mapped byte       */
static vaddr_t       heap_limit    = 0;   /* one-past-last byte of the window */
static heap_block_t *g_first_block = NULL;
static heap_block_t *g_free_list   = NULL;

/* Running counters, exposed for diagnostics (e.g. a future `free` command
 * in the shell, or `meminfo` debug output). */
static uint64_t stat_bytes_allocated = 0; /* currently live, payload bytes */
static uint64_t stat_bytes_arena     = 0; /* total heap arena size         */
static uint64_t stat_alloc_calls     = 0;
static uint64_t stat_free_calls      = 0;

/* ── Emergency pool ────────────────────────────────────────────────────────
 * Active only before mm_init_heap() has run (heap_start == 0). Covers the
 * handful of very-early-boot kmalloc() calls that happen before paging
 * is ready. Never freed individually — this pool itself is never reclaimed,
 * but it's small (256 KiB) and only used for a few early structures, so
 * that's an acceptable, bounded cost rather than a real leak. */
static uint8_t  emergency_pool[256 * 1024];
static size_t   emergency_used = 0;

static void *emergency_alloc(size_t size) {
    size = (size + (HEAP_ALIGN - 1)) & ~(size_t)(HEAP_ALIGN - 1);
    if (emergency_used + size > sizeof(emergency_pool)) {
        serial_puts("[HEAP] FATAL: emergency pool exhausted\n");
        return NULL;
    }
    void *p = emergency_pool + emergency_used;
    emergency_used += size;
    return p;
}

/* ── Free-list linkage helpers ─────────────────────────────────────────── */

static void free_list_insert(heap_block_t *b) {
    b->free_prev = NULL;
    b->free_next = g_free_list;
    if (g_free_list) g_free_list->free_prev = b;
    g_free_list = b;
}

static void free_list_remove(heap_block_t *b) {
    if (b->free_prev) b->free_prev->free_next = b->free_next;
    else               g_free_list = b->free_next;
    if (b->free_next) b->free_next->free_prev = b->free_prev;
    b->free_next = NULL;
    b->free_prev = NULL;
}

/* ── Arena growth ──────────────────────────────────────────────────────── *
 * Under boot64.s's identity map (PA == VA for all physical RAM), we just
 * allocate physical frames from the PMM and use their physical addresses
 * directly as virtual addresses. mm_map_page() is a no-op here (pml4==NULL)
 * so we skip it entirely — the frames are already accessible via the
 * identity map that boot64.s set up covering all available RAM. */
static heap_block_t *heap_grow(size_t min_payload) {
    size_t needed = HEAP_HEADER_SIZE + min_payload;
    size_t pages  = (needed + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages < HEAP_GROW_PAGES) pages = HEAP_GROW_PAGES;

    /* Allocate a contiguous run of frames from the PMM.
     * We ask for one frame at a time and chain them — if any alloc fails
     * we use however many we got. The frames won't be physically contiguous
     * in general, but that's fine: each frame becomes its own block in the
     * "all" list and they get coalesced on free if they happen to be adjacent. */
    heap_block_t *first_new = NULL;
    heap_block_t *prev_new  = NULL;

    for (size_t i = 0; i < pages; i++) {
        void *frame = pmm_alloc_frame();
        if (!frame) {
            if (i == 0) return NULL; /* total failure */
            break;                   /* use what we got */
        }

        /* Under identity mapping, the frame's PA is its VA. */
        heap_block_t *b = (heap_block_t *)frame;
        b->size      = PAGE_SIZE - HEAP_HEADER_SIZE;
        b->magic     = HEAP_MAGIC_FREE;
        b->all_next  = NULL;
        b->all_prev  = NULL;
        b->free_next = NULL;
        b->free_prev = NULL;

        stat_bytes_arena += PAGE_SIZE;

        if (!first_new) first_new = b;

        /* Link into global "all" list at the tail. */
        if (!g_first_block) {
            g_first_block = b;
        } else {
            /* Find the current tail. */
            heap_block_t *tail = g_first_block;
            while (tail->all_next) tail = tail->all_next;

            /* Merge if physically adjacent and tail is free. */
            uint8_t *tail_end = (uint8_t *)tail + HEAP_HEADER_SIZE + tail->size;
            if (tail->magic == HEAP_MAGIC_FREE && tail_end == (uint8_t *)b) {
                free_list_remove(tail);
                tail->size += HEAP_HEADER_SIZE + b->size;
                free_list_insert(tail);
                if (!prev_new) first_new = tail;
                continue; /* b absorbed into tail, no new node */
            }

            tail->all_next = b;
            b->all_prev    = tail;
        }

        free_list_insert(b);
        prev_new = b;
    }

    return first_new;
}

/* ── mm_init_heap — called once from memory_init() ───────────────────── */
void mm_init_heap(vaddr_t s, vaddr_t e) {
    /* Under boot64.s identity mapping we don't use a fixed VA window —
     * heap_grow() allocates PMM frames and uses their PA directly as VA.
     * Keep s/e as hints for future use but don't depend on them. */
    heap_start = s;
    heap_limit = e;
    heap_end   = s;

    g_first_block = NULL;
    g_free_list   = NULL;
    stat_bytes_allocated = 0;
    stat_bytes_arena     = 0;
    stat_alloc_calls     = 0;
    stat_free_calls      = 0;

    /* Pre-grow so the first kmalloc() doesn't need to call heap_grow(). */
    heap_grow(0);

    serial_puts("[HEAP] Free-list heap initialised.\n");
}

/* ── Split a free block if it's large enough to usefully carve a smaller
 *    allocation out of it, leaving a new free remainder block behind ──── */
static void maybe_split(heap_block_t *b, size_t want) {
    /* Only split if the remainder would be big enough to hold a header
     * plus a minimally useful payload — otherwise we'd create a free
     * block too small for anything to ever use, pure fragmentation. */
    const size_t min_remainder = HEAP_HEADER_SIZE + HEAP_ALIGN;

    if (b->size < want + min_remainder) return; /* not worth splitting */

    uint8_t *remainder_addr = (uint8_t *)b + HEAP_HEADER_SIZE + want;
    heap_block_t *remainder = (heap_block_t *)remainder_addr;

    remainder->size      = b->size - want - HEAP_HEADER_SIZE;
    remainder->magic     = HEAP_MAGIC_FREE;
    remainder->all_prev  = b;
    remainder->all_next  = b->all_next;
    if (remainder->all_next) remainder->all_next->all_prev = remainder;
    remainder->free_next = NULL;
    remainder->free_prev = NULL;

    b->all_next = remainder;
    b->size     = want;

    free_list_insert(remainder);
}

/* ── kmalloc ───────────────────────────────────────────────────────────── */
void *kmalloc(size_t size) {
    if (!size) return NULL;

    size = (size + (HEAP_ALIGN - 1)) & ~(size_t)(HEAP_ALIGN - 1);

    if (!heap_start) {
        /* Heap not initialised yet — serve from the emergency pool. */
        return emergency_alloc(size);
    }

    /* First-fit search of the free list. */
    heap_block_t *b = g_free_list;
    while (b) {
        if (b->size >= size) break;
        b = b->free_next;
    }

    if (!b) {
        b = heap_grow(size);
        if (!b) return NULL; /* genuinely out of memory */
        if (b->size < size) return NULL; /* grow() clamped smaller than asked */
    }

    free_list_remove(b);
    maybe_split(b, size);
    b->magic = HEAP_MAGIC_USED;

    stat_bytes_allocated += b->size;
    stat_alloc_calls++;

    return (void *)(b + 1); /* payload starts right after the header */
}

/* ── kzalloc ───────────────────────────────────────────────────────────── */
void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

/* ── Coalesce a freed block with its immediate "all"-list neighbors if
 *    they are also free. Returns the (possibly merged) block.
 *
 * Both branches follow the same shape: remove the SURVIVING block from
 * the free list before resizing it (its size is about to change, and
 * the free list itself doesn't care about size, but consistently
 * removing-then-reinserting keeps the invariant "every free block's
 * free_next/free_prev are valid for its CURRENT size" trivially true
 * rather than relying on in-place size mutation being safe), and
 * explicitly remove the ABSORBED block from the free list since it
 * stops being an independent node — its memory becomes part of the
 * surviving block and must not be reachable via a second, stale
 * free-list entry pointing at memory that will later be handed out
 * (whole or split) as part of a live allocation. ───────────────────── */
static heap_block_t *coalesce(heap_block_t *b) {
    heap_block_t *next = b->all_next;
    if (next && next->magic == HEAP_MAGIC_FREE) {
        free_list_remove(next); /* next is absorbed; discard its node */
        b->size += HEAP_HEADER_SIZE + next->size;
        b->all_next = next->all_next;
        if (b->all_next) b->all_next->all_prev = b;
    }

    heap_block_t *prev = b->all_prev;
    if (prev && prev->magic == HEAP_MAGIC_FREE) {
        free_list_remove(prev); /* prev survives but is resizing */
        free_list_remove(b);    /* b is absorbed; discard its node */
        prev->size += HEAP_HEADER_SIZE + b->size;
        prev->all_next = b->all_next;
        if (prev->all_next) prev->all_next->all_prev = prev;
        free_list_insert(prev); /* re-add the survivor at its new size */
        b = prev;
    }

    return b;
}

/* ── kfree ─────────────────────────────────────────────────────────────── */
void kfree(void *ptr) {
    if (!ptr) return;

    /* Pointers from the emergency pool can't be freed individually (no
     * header was ever written for them) — recognise and silently ignore
     * rather than corrupt memory by misinterpreting pool bytes as a
     * heap_block_t header. */
    uint8_t *p = (uint8_t *)ptr;
    if (p >= emergency_pool && p < emergency_pool + sizeof(emergency_pool)) {
        return;
    }

    heap_block_t *b = (heap_block_t *)ptr - 1;

    if (b->magic == HEAP_MAGIC_FREE) {
        serial_puts("[HEAP] WARNING: double free detected, ignoring\n");
        return;
    }
    if (b->magic != HEAP_MAGIC_USED) {
        serial_puts("[HEAP] WARNING: kfree() on corrupt or invalid pointer, ignoring\n");
        return;
    }

    stat_bytes_allocated -= b->size;
    stat_free_calls++;

    b->magic = HEAP_MAGIC_FREE;
    free_list_insert(b);
    coalesce(b);
}

/* ── krealloc ──────────────────────────────────────────────────────────── *
 * Unlike the old bump-allocator version, this can now genuinely grow or
 * shrink in place when the adjacent block has room, and otherwise does a
 * real alloc+copy+free using the ACTUAL old size (tracked in the header)
 * rather than trusting the caller's new_size as a copy length. */
void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return NULL; }

    new_size = (new_size + (HEAP_ALIGN - 1)) & ~(size_t)(HEAP_ALIGN - 1);

    uint8_t *raw = (uint8_t *)ptr;
    if (raw >= emergency_pool && raw < emergency_pool + sizeof(emergency_pool)) {
        /* Emergency-pool pointer: no header, no in-place growth possible.
         * Fall back to alloc + copy with a conservative copy length. */
        void *np = kmalloc(new_size);
        if (np) memcpy(np, ptr, new_size);
        return np;
    }

    heap_block_t *b = (heap_block_t *)ptr - 1;
    if (b->magic != HEAP_MAGIC_USED) {
        serial_puts("[HEAP] WARNING: krealloc() on corrupt or invalid pointer\n");
        return NULL;
    }

    if (new_size <= b->size) {
        /* Shrinking (or same size): split off the tail as a free block
         * if there's enough room to make that worthwhile. */
        maybe_split(b, new_size);
        return ptr;
    }

    /* Growing: try to absorb the immediately-following block if it's
     * free and large enough, avoiding a copy entirely. */
    heap_block_t *next = b->all_next;
    if (next && next->magic == HEAP_MAGIC_FREE &&
        b->size + HEAP_HEADER_SIZE + next->size >= new_size) {
        free_list_remove(next);
        b->size += HEAP_HEADER_SIZE + next->size;
        b->all_next = next->all_next;
        if (b->all_next) b->all_next->all_prev = b;
        maybe_split(b, new_size);
        return ptr;
    }

    /* No room to grow in place — allocate fresh, copy the real old size
     * (not new_size, which may be larger than what's actually valid to
     * read from the old block), then free the old block. */
    size_t old_size = b->size;
    void *np = kmalloc(new_size);
    if (!np) return NULL;
    memcpy(np, ptr, old_size < new_size ? old_size : new_size);
    kfree(ptr);
    return np;
}

/* ── Diagnostics ───────────────────────────────────────────────────────── *
 * Exposed for a future `free`/`meminfo` shell command. Not part of
 * memory.h's public contract (that header only promises kmalloc family),
 * so callers that want this must declare it themselves — kept minimal
 * and explicit rather than growing memory.h's surface for a debug-only
 * feature. */
void heap_get_stats(uint64_t *out_allocated, uint64_t *out_arena,
                     uint64_t *out_allocs, uint64_t *out_frees) {
    if (out_allocated) *out_allocated = stat_bytes_allocated;
    if (out_arena)     *out_arena     = stat_bytes_arena;
    if (out_allocs)    *out_allocs    = stat_alloc_calls;
    if (out_frees)     *out_frees     = stat_free_calls;
}

/* ── memory_init — called once from kernel_main ───────────────────────── *
 * Moved here from memory.c verbatim (just relocated, not changed) since
 * heap.c is now the natural owner of "bring up the kernel heap". */
void memory_init(void) {
    extern void pmm_init(uint64_t);
    pmm_init(0);

    mm_init_heap(HEAP_VIRT_START, HEAP_VIRT_END);

    serial_puts("[MM] memory_init complete.\n");
}
