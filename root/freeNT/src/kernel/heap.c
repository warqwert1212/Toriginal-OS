// ==============================================================================
// HEAP.C - Kernel Memory Allocation Engine
// ==============================================================================
#include "heap.h"
#include "vmm.h"
#include "pmm.h"

void print_serial(const char* str);

struct heap_chunk {
    size_t size;
    uint8_t is_free;
    struct heap_chunk* next;
    struct heap_chunk* prev;
};

static struct heap_chunk* heap_start = NULL;
static uint64_t heap_break = 0xFFFF800010000000; // Distinct higher-half memory region

static void heap_expand(size_t size) {
    size_t needed_pages = (size + sizeof(struct heap_chunk) + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < needed_pages; i++) {
        void* phys = pmm_alloc_frame();
        vmm_map_page(heap_break, (uint64_t)phys, PAGE_WRITABLE);
        heap_break += PAGE_SIZE;
    }
}

void kmalloc_init(void) {
    // Prime heap canvas zone with initial allocations
    heap_start = (struct heap_chunk*)heap_break;
    heap_expand(PAGE_SIZE * 4);

    heap_start->size = (PAGE_SIZE * 4) - sizeof(struct heap_chunk);
    heap_start->is_free = 1;
    heap_start->next = NULL;
    heap_start->prev = NULL;

    print_serial("[HEAP] Kernel Dynamic Space Configured at 0xFFFF800010000000\n");
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    // Align structural chunks to 8-byte boundaries
    size = (size + 7) & ~7;
    struct heap_chunk* curr = heap_start;

    while (curr) {
        if (curr->is_free && curr->size >= size) {
            // Split block if excess space exists
            if (curr->size >= size + sizeof(struct heap_chunk) + 8) {
                struct heap_chunk* next_chunk = (struct heap_chunk*)((uintptr_t)curr + sizeof(struct heap_chunk) + size);
                next_chunk->size = curr->size - size - sizeof(struct heap_chunk);
                next_chunk->is_free = 1;
                next_chunk->next = curr->next;
                next_chunk->prev = curr;

                if (curr->next) curr->next->prev = next_chunk;
                curr->next = next_chunk;
                curr->size = size;
            }
            curr->is_free = 0;
            return (void*)((uintptr_t)curr + sizeof(struct heap_chunk));
        }
        
        if (!curr->next) { // Tail reached, expand dynamic boundaries
            heap_expand(size);
            struct heap_chunk* expanded_chunk = (struct heap_chunk*)((uintptr_t)curr + sizeof(struct heap_chunk) + curr->size);
            expanded_chunk->size = size + PAGE_SIZE; // Buffer space
            expanded_chunk->is_free = 1;
            expanded_chunk->next = NULL;
            expanded_chunk->prev = curr;
            curr->next = expanded_chunk;
        }
        curr = curr->next;
    }
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;

    struct heap_chunk* chunk = (struct heap_chunk*)((uintptr_t)ptr - sizeof(struct heap_chunk));
    chunk->is_free = 1;

    // Merge forwards
    if (chunk->next && chunk->next->is_free) {
        chunk->size += sizeof(struct heap_chunk) + chunk->next->size;
        chunk->next = chunk->next->next;
        if (chunk->next) chunk->next->prev = chunk;
    }
    // Merge backwards
    if (chunk->prev && chunk->prev->is_free) {
        chunk->prev->size += sizeof(struct heap_chunk) + chunk->size;
        chunk->prev->next = chunk->next;
        if (chunk->next) chunk->next->prev = chunk->prev;
    }
}