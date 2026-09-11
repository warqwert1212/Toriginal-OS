// ==============================================================================
// PMM.C - Physical Memory Manager (Bitmap Allocator)
// FIXED: Parses Multiboot2 memory map tags (not Multiboot1 struct layout)
// FIXED: added a second "capped" bitmap so the shell's `mem` command can
//        raise/lower the usable-RAM ceiling live, without a reboot, and
//        without ever mistaking a real in-use frame for one that's just
//        excluded by a user-configured cap (see pmm_apply_ram_config()).
// ==============================================================================

#include "pmm.h"
#include "serial.h"

// Multiboot2 tag types
#define MB2_TAG_END   0
#define MB2_TAG_MMAP  6

struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct mb2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed));

struct mb2_mmap_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;      // 1 = available RAM
    uint32_t reserved;
} __attribute__((packed));

// ---------------------------------------------------------------------------
// Bitmap — tracks up to 4GB of RAM (1,048,576 frames of 4KB each)
// ---------------------------------------------------------------------------
#define TOTAL_FRAMES 1048576
#define BITMAP_BYTES (TOTAL_FRAMES / 8)

static uint8_t  pmm_bitmap[BITMAP_BYTES];
static uint32_t total_frames    = TOTAL_FRAMES;
static uint32_t used_frames     = TOTAL_FRAMES;
static uint32_t last_free_frame = 0;

/* Frames excluded by a user-configured RAM cap (see
 * pmm_apply_ram_config()) - deliberately a *separate* bitmap from
 * pmm_bitmap's real used/free tracking. A frame can be "capped" only
 * while it's also free in pmm_bitmap; the moment something really
 * allocates a frame it's tracked there instead, so lowering or
 * raising the cap later can never mistake a live allocation for one
 * it's free to reclaim or exclude. */
static uint8_t  pmm_capped_bitmap[BITMAP_BYTES];
static uint32_t capped_frame_count = 0;

/* Total usable RAM (KB) as reported by the firmware/bootloader - set
 * once in pmm_init() and never changed afterward. g_ram_limit_kb is
 * the ceiling actually being enforced right now; it starts equal to
 * the total (uncapped) and only pmm_apply_ram_config() moves it. */
static uint32_t g_total_ram_kb = 0;
static uint32_t g_ram_limit_kb = 0;

static inline void bitmap_set(uint32_t bit)   { pmm_bitmap[bit>>3] |=  (1u << (bit&7)); }
static inline void bitmap_clear(uint32_t bit) { pmm_bitmap[bit>>3] &= ~(1u << (bit&7)); }
static inline int  bitmap_test(uint32_t bit)  { return (pmm_bitmap[bit>>3] >> (bit&7)) & 1; }

static inline void capped_set(uint32_t bit)   { pmm_capped_bitmap[bit>>3] |=  (1u << (bit&7)); }
static inline void capped_clear(uint32_t bit) { pmm_capped_bitmap[bit>>3] &= ~(1u << (bit&7)); }
static inline int  capped_test(uint32_t bit)  { return (pmm_capped_bitmap[bit>>3] >> (bit&7)) & 1; }

void pmm_free_frame(void *phys) {
    uint32_t frame = (uint32_t)((uint64_t)(uintptr_t)phys / PMM_FRAME_SIZE);
    if (frame >= total_frames) return;
    if (bitmap_test(frame)) { bitmap_clear(frame); used_frames--; }
}

void *pmm_alloc_frame(void) {
    for (uint32_t i = last_free_frame; i < total_frames; i++) {
        if (!bitmap_test(i) && !capped_test(i)) {
            bitmap_set(i); used_frames++; last_free_frame = i+1;
            return (void *)(uintptr_t)((uint64_t)i * PMM_FRAME_SIZE);
        }
    }
    for (uint32_t i = 0; i < last_free_frame; i++) {
        if (!bitmap_test(i) && !capped_test(i)) {
            bitmap_set(i); used_frames++; last_free_frame = i+1;
            return (void *)(uintptr_t)((uint64_t)i * PMM_FRAME_SIZE);
        }
    }
    print_serial("[PMM] FATAL: Out of physical memory!\n");
    return NULL;
}

uint32_t pmm_get_free_ram(void) {
    uint32_t free_frames = total_frames - used_frames - capped_frame_count;
    return free_frames * (PMM_FRAME_SIZE / 1024);
}

uint32_t pmm_get_total_ram_kb(void) { return g_total_ram_kb; }
uint32_t pmm_get_ram_limit_kb(void) { return g_ram_limit_kb; }

/* See pmm.h for the full contract. Safe to call at any time after
 * pmm_init() - kernel.c calls it once at boot (after reading the TRBL
 * settings sector, which needs ATA up - too late for pmm_init()
 * itself) and again any time the shell's `mem` command runs. */
int pmm_apply_ram_config(uint8_t mode, uint32_t custom_kb) {
    extern uint8_t __kernel_end[];
    uint32_t kernel_end_kb = (uint32_t)(((uint64_t)(uintptr_t)__kernel_end + 1023) / 1024);

    /* Bare-minimum floor: the kernel's own image plus a small working
     * margin for the heap to have somewhere to grow into. Derived
     * from __kernel_end rather than a fixed guess for the same reason
     * pmm_init() already locks the kernel's image that way below -
     * it can't silently drift out of sync as the kernel grows. */
    uint32_t least_kb = kernel_end_kb + 8192; /* +8 MiB headroom */

    uint32_t new_limit_kb;
    switch (mode) {
        case PMM_MEM_MODE_MAX:
            new_limit_kb = g_total_ram_kb;
            break;
        case PMM_MEM_MODE_LEAST:
            new_limit_kb = least_kb;
            if (new_limit_kb > g_total_ram_kb) new_limit_kb = g_total_ram_kb;
            break;
        case PMM_MEM_MODE_CUSTOM:
            if (custom_kb == 0) return -1;
            new_limit_kb = custom_kb;
            if (new_limit_kb < least_kb)      new_limit_kb = least_kb;
            if (new_limit_kb > g_total_ram_kb) new_limit_kb = g_total_ram_kb;
            break;
        default:
            return -1;
    }

    uint32_t boundary_frame = (uint32_t)(((uint64_t)new_limit_kb * 1024) / PMM_FRAME_SIZE);
    if (boundary_frame > total_frames) boundary_frame = total_frames;

    for (uint32_t i = 0; i < total_frames; i++) {
        if (i >= boundary_frame) {
            /* Above the new ceiling: cap it only if it's currently
             * free. A frame already handed out to a real allocation
             * is left alone and stays tracked in pmm_bitmap exactly
             * as before - lowering the cap can't reach in and steal
             * memory something is actively using. */
            if (!bitmap_test(i) && !capped_test(i)) { capped_set(i); capped_frame_count++; }
        } else {
            /* Within the new ceiling: un-cap anything a previous,
             * lower cap had excluded. Never touches real allocations
             * - those were never marked capped in the first place. */
            if (capped_test(i)) { capped_clear(i); capped_frame_count--; }
        }
    }

    g_ram_limit_kb   = new_limit_kb;
    last_free_frame  = 0; /* re-scan from the bottom now the boundary moved */
    return 0;
}

void pmm_init(uint64_t multiboot_ptr) {
    print_serial("[PMM] Initializing...\n");

    // Lock everything
    for (uint32_t i = 0; i < BITMAP_BYTES; i++) pmm_bitmap[i] = 0xFF;
    for (uint32_t i = 0; i < BITMAP_BYTES; i++) pmm_capped_bitmap[i] = 0x00;
    used_frames = total_frames;
    capped_frame_count = 0;
    last_free_frame = 0;

    if (multiboot_ptr == 0) {
        print_serial("[PMM] No multiboot ptr - fallback 16MB region\n");
        for (uint32_t i = 256; i < 4096; i++)
            pmm_free_frame((void*)(uintptr_t)((uint64_t)i * PMM_FRAME_SIZE));
        /* Fallback assumes 16 MiB total (4096 frames), including the
         * always-reserved low 1 MiB - matches the loop bound above. */
        g_total_ram_kb = 4096 * (PMM_FRAME_SIZE / 1024);
        g_ram_limit_kb = g_total_ram_kb;
        return;
    }

    // Walk Multiboot2 tags (skip 8-byte header)
    uint8_t *ptr = (uint8_t *)(uintptr_t)(multiboot_ptr + 8);
    int found_mmap = 0;
    uint32_t usable_frames_seen = 0;

    while (1) {
        struct mb2_tag *tag = (struct mb2_tag *)ptr;
        if (tag->type == MB2_TAG_END || tag->size == 0) break;

        if (tag->type == MB2_TAG_MMAP) {
            found_mmap = 1;
            struct mb2_tag_mmap *mt = (struct mb2_tag_mmap *)tag;
            uint8_t *ep  = (uint8_t *)mt + sizeof(struct mb2_tag_mmap);
            uint8_t *end = (uint8_t *)tag + tag->size;

            while (ep < end) {
                struct mb2_mmap_entry *e = (struct mb2_mmap_entry *)ep;
                if (e->type == 1) {
                    uint64_t s = (e->base_addr + PMM_FRAME_SIZE - 1) & ~(uint64_t)(PMM_FRAME_SIZE-1);
                    uint64_t f = (e->base_addr + e->length)          & ~(uint64_t)(PMM_FRAME_SIZE-1);
                    for (uint64_t a = s; a < f; a += PMM_FRAME_SIZE) {
                        uint32_t fr = (uint32_t)(a / PMM_FRAME_SIZE);
                        if (fr < total_frames) { pmm_free_frame((void*)(uintptr_t)a); usable_frames_seen++; }
                    }
                }
                ep += mt->entry_size;
            }
        }
        ptr += (tag->size + 7) & ~7u;
    }

    if (!found_mmap) {
        print_serial("[PMM] No mmap tag found - fallback\n");
        /* 128 MiB - comfortably covers the kernel's own image (up to
         * the 32 MiB ceiling kernel64.ld enforces) plus the full 48
         * MiB heap window above it, with real margin left over. Every
         * x86-64 machine actually capable of running this OS has far
         * more physical RAM than this in reality; the true, complete
         * fix is TRBL doing real E820 detection and handing the
         * kernel a genuine MMAP tag the way GRUB already does - this
         * fallback exists because TRBL doesn't yet, not because 128
         * MiB is meant to be the OS's real ceiling. */
        for (uint32_t i = 256; i < 32768; i++)
            pmm_free_frame((void*)(uintptr_t)((uint64_t)i * PMM_FRAME_SIZE));
        usable_frames_seen = 32768;
    }

    /* Total detected RAM = every usable frame the firmware/bootloader
     * actually reported (or the fallback assumed), counted during the
     * pass above - before the BIOS/kernel lock-down loops below
     * re-reserve part of it. This is what "detected RAM" means to
     * `mem max` and `hardware`: the real physical total, not what's
     * currently free for allocation (that's `free`'s job). */
    g_total_ram_kb = usable_frames_seen * (PMM_FRAME_SIZE / 1024);
    g_ram_limit_kb = g_total_ram_kb; /* uncapped until something calls
                                       * pmm_apply_ram_config() */

    // Lock low 1MB (BIOS, VGA, real-mode)
    for (uint32_t i = 0; i < 256; i++)
        if (!bitmap_test(i)) { bitmap_set(i); used_frames++; }

    /* Lock the kernel's own image - computed from the linker's own
     * __kernel_end symbol, not a hardcoded guess. The previous fixed
     * "1MB-4MB" range was sized for a much smaller kernel; once the
     * image legitimately grew past that (exactly what embedding
     * boot assets did), everything beyond 4MB was silently left
     * marked "free" - real physical memory holding live kernel code
     * and data, available for the allocator to hand out and
     * overwrite. Deriving this from __kernel_end means it can never
     * drift out of sync with the kernel's actual size again,
     * regardless of how large the kernel grows in the future. */
    extern uint8_t __kernel_end[];
    uint32_t kernel_end_frame = (uint32_t)(((uintptr_t)__kernel_end + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE);
    for (uint32_t i = 256; i < kernel_end_frame && i < total_frames; i++)
        if (!bitmap_test(i)) { bitmap_set(i); used_frames++; }

    // Print free RAM
    uint32_t fkb = pmm_get_free_ram();
    char buf[12]; int idx = 0;
    if (fkb == 0) { buf[idx++]='0'; }
    else { char t[12]; int ti=0; uint32_t v=fkb;
           while(v>0){t[ti++]='0'+v%10;v/=10;}
           while(ti>0) buf[idx++]=t[--ti]; }
    buf[idx]='\0';
    print_serial("[PMM] Free RAM: "); print_serial(buf); print_serial(" KB\n");
}
