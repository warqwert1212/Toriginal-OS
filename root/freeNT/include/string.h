#ifndef STRING_H
#define STRING_H

#include <stddef.h>

size_t strlen(const char* str);

int strcmp(const char* a, const char* b);

int strncmp(const char* a,
            const char* b,
            size_t n);

char* strcpy(char* dest,
             const char* src);

char* strncpy(char* dest,
              const char* src,
              size_t n);

void* memset(void* ptr,
             int value,
             size_t size);

void* memcpy(void* dest,
             const void* src,
             size_t size);

int memcmp(const void* a,
           const void* b,
           size_t size);

#endif