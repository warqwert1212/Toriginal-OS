#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

void memory_init(void);

void* kmalloc(size_t size);

void* kzalloc(size_t size);

void kfree(void* ptr);

/* Heap diagnostics — currently-live bytes, total arena size, and running
 * alloc/free call counts. Used by shell commands like `free`/`meminfo`. */
void heap_get_stats(uint64_t *out_allocated, uint64_t *out_arena,
                     uint64_t *out_allocs, uint64_t *out_frees);

#endif
