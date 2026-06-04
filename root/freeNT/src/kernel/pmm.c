// ==============================================================================
// PMM.C - Physical Memory Manager (Bitmap Allocator)
// ==============================================================================
#include "pmm.h"

// Define Multiboot structures locally for the PMM to parse
struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t num;
    uint32_t size;
    uint32_t addr;
    uint32_t mmap_length;
    uint32_t mmap_addr;
} __attribute__((packed));

extern void print_serial(const char* str); // For debugging output

// We support tracking up to 4GB of physical RAM for now.
// 4GB / 4096 bytes per frame = 1,048,576 frames.
// 1,048,576 frames / 8 bits per byte = 131,072 bytes (128 KB bitmap).
static uint8_t pmm_bitmap[131072]; 
static uint32_t total_frames = 1048576;
static uint32_t used_frames = 1048576; // Start with everything "used" (locked)
static uint32_t last_scanned_frame = 0;

// Bitmap manipulation helpers
static inline void bitmap_set(uint32_t bit) {
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(uint32_t bit) {
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline uint8_t bitmap_test(uint32_t bit) {
    return pmm_bitmap[bit / 8] & (1 << (bit % 8));
}

// Free a frame so it can be allocated later
void pmm_free_frame(void* phys_addr) {
    uint64_t addr = (uint64_t)phys_addr;
    uint32_t frame = addr / PMM_FRAME_SIZE;
    
    if (frame >= total_frames) return;
    
    if (bitmap_test(frame)) {
        bitmap_clear(frame);
        used_frames--;
    }
}

// Find the first free frame and mark it as used
void* pmm_alloc_frame(void) {
    for (uint32_t i = last_scanned_frame; i < total_frames; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_frames++;
            last_scanned_frame = i;
            return (void*)((uint64_t)i * PMM_FRAME_SIZE);
        }
    }
    
    // If we reach here, we wrap around and check from the beginning
    for (uint32_t i = 0; i < last_scanned_frame; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_frames++;
            last_scanned_frame = i;
            return (void*)((uint64_t)i * PMM_FRAME_SIZE);
        }
    }
    
    print_serial("[PANIC] Out of Physical Memory!\n");
    return NULL; // Out of memory
}

// Initialize the PMM based on the Multiboot memory map
void pmm_init(uint64_t multiboot_ptr) {
    print_serial("[PMM] Initializing Physical Memory Manager...\n");
    
    // 1. Lock all memory by default (Set all bits to 1)
    for (uint32_t i = 0; i < sizeof(pmm_bitmap); i++) {
        pmm_bitmap[i] = 0xFF;
    }
    used_frames = total_frames;

    struct multiboot_info* mbi = (struct multiboot_info*)multiboot_ptr;
    
    if (!(mbi->flags & (1 << 6))) {
        print_serial("[PMM FATAL] No memory map provided by bootloader!\n");
        return;
    }

    struct multiboot_mmap_entry* mmap = (struct multiboot_mmap_entry*)(uintptr_t)mbi->mmap_addr;
    uint32_t parsed_length = 0;

    // 2. Iterate through the memory map and ONLY free the available RAM regions
    while (parsed_length < mbi->mmap_length) {
        if (mmap->type == 1) { // Type 1 means "Available RAM"
            uint64_t start_addr = mmap->addr;
            uint64_t length = mmap->len;
            
            for (uint64_t i = 0; i < length; i += PMM_FRAME_SIZE) {
                pmm_free_frame((void*)(start_addr + i));
            }
        }
        parsed_length += mmap->size + sizeof(uint32_t);
        mmap = (struct multiboot_mmap_entry*)((uintptr_t)mmap + mmap->size + sizeof(uint32_t));
    }
    
    // 3. Re-lock the first 1MB of memory because it contains the kernel code, VGA buffer, and BIOS data!
    for (uint32_t i = 0; i < (0x100000 / PMM_FRAME_SIZE); i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_frames++;
        }
    }
    // Also explicitly lock where the kernel is physically loaded in memory
    // (We will pass the kernel size dynamically later, but for now lock up to 4MB)
    for (uint32_t i = (0x100000 / PMM_FRAME_SIZE); i < (0x400000 / PMM_FRAME_SIZE); i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            used_frames++;
        }
    }

    print_serial("[PMM] Physical Memory Manager Initialized.\n");
}

uint32_t pmm_get_free_ram(void) {
    return (total_frames - used_frames) * 4; // Return free RAM in KB
}