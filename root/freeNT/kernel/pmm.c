// ==============================================================================
// PMM.C - Physical Memory Manager (Bitmap Allocator)
// FIXED: Parses Multiboot2 memory map tags (not Multiboot1 struct layout)
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

static inline void bitmap_set(uint32_t bit)   { pmm_bitmap[bit>>3] |=  (1u << (bit&7)); }
static inline void bitmap_clear(uint32_t bit) { pmm_bitmap[bit>>3] &= ~(1u << (bit&7)); }
static inline int  bitmap_test(uint32_t bit)  { return (pmm_bitmap[bit>>3] >> (bit&7)) & 1; }

void pmm_free_frame(void *phys) {
    uint32_t frame = (uint32_t)((uint64_t)(uintptr_t)phys / PMM_FRAME_SIZE);
    if (frame >= total_frames) return;
    if (bitmap_test(frame)) { bitmap_clear(frame); used_frames--; }
}

void *pmm_alloc_frame(void) {
    for (uint32_t i = last_free_frame; i < total_frames; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i); used_frames++; last_free_frame = i+1;
            return (void *)(uintptr_t)((uint64_t)i * PMM_FRAME_SIZE);
        }
    }
    for (uint32_t i = 0; i < last_free_frame; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i); used_frames++; last_free_frame = i+1;
            return (void *)(uintptr_t)((uint64_t)i * PMM_FRAME_SIZE);
        }
    }
    print_serial("[PMM] FATAL: Out of physical memory!\n");
    return NULL;
}

uint32_t pmm_get_free_ram(void) {
    return (total_frames - used_frames) * (PMM_FRAME_SIZE / 1024);
}

void pmm_init(uint64_t multiboot_ptr) {
    print_serial("[PMM] Initializing...\n");

    // Lock everything
    for (uint32_t i = 0; i < BITMAP_BYTES; i++) pmm_bitmap[i] = 0xFF;
    used_frames = total_frames;
    last_free_frame = 0;

    if (multiboot_ptr == 0) {
        print_serial("[PMM] No multiboot ptr - fallback 16MB region\n");
        for (uint32_t i = 256; i < 4096; i++)
            pmm_free_frame((void*)(uintptr_t)((uint64_t)i * PMM_FRAME_SIZE));
        return;
    }

    // Walk Multiboot2 tags (skip 8-byte header)
    uint8_t *ptr = (uint8_t *)(uintptr_t)(multiboot_ptr + 8);
    int found_mmap = 0;

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
                        if (fr < total_frames) pmm_free_frame((void*)(uintptr_t)a);
                    }
                }
                ep += mt->entry_size;
            }
        }
        ptr += (tag->size + 7) & ~7u;
    }

    if (!found_mmap) {
        print_serial("[PMM] No mmap tag found - fallback\n");
        for (uint32_t i = 256; i < 4096; i++)
            pmm_free_frame((void*)(uintptr_t)((uint64_t)i * PMM_FRAME_SIZE));
    }

    // Lock low 1MB (BIOS, VGA, real-mode)
    for (uint32_t i = 0; i < 256; i++)
        if (!bitmap_test(i)) { bitmap_set(i); used_frames++; }

    // Lock kernel image (1MB - 4MB physical)
    for (uint32_t i = 256; i < 1024; i++)
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
