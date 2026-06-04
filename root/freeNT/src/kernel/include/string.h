#ifndef _KERNEL_STRING_H
#define _KERNEL_STRING_H

#include "types.h"

/* String functions */
size_t strlen(const char *str);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char* strcpy(char *dest, const char *src);
char* strncpy(char *dest, const char *src, size_t n);
char* strcat(char *dest, const char *src);
char* strchr(const char *str, int c);
char* strstr(const char *haystack, const char *needle);

/* Memory functions */
void* memset(void *mem, int value, size_t size);
void* memcpy(void *dest, const void *src, size_t size);
int memcmp(const void *m1, const void *m2, size_t size);
void* memmove(void *dest, const void *src, size_t size);

#endif /* _KERNEL_STRING_H */
