// ==============================================================================
// HEAP.H - Kernel Dynamic Memory Manager
// ==============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

void  kmalloc_init(void);
void* kmalloc(size_t size);
void  kfree(void* ptr);