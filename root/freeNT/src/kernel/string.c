#include "string.h"

/* strlen - return length of string */
size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

/* strcmp - compare two strings */
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/* strncmp - compare n bytes of strings */
int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i])
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (!s1[i])
            return 0;
    }
    return 0;
}

/* strcpy - copy string */
char* strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

/* strncpy - copy n bytes of string */
char* strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = 0;
    return dest;
}

/* strcat - concatenate strings */
char* strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d)
        d++;
    while ((*d++ = *src++));
    return dest;
}

/* strchr - find character in string */
char* strchr(const char *str, int c) {
    while (*str) {
        if (*str == c)
            return (char *)str;
        str++;
    }
    if (c == 0)
        return (char *)str;
    return NULL;
}

/* strstr - find substring */
char* strstr(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    
    for (; *haystack; haystack++) {
        if (strncmp(haystack, needle, needle_len) == 0)
            return (char *)haystack;
    }
    return NULL;
}

/* memset - fill memory with value */
void* memset(void *mem, int value, size_t size) {
    unsigned char *p = (unsigned char *)mem;
    for (size_t i = 0; i < size; i++)
        p[i] = (unsigned char)value;
    return mem;
}

/* memcpy - copy memory */
void* memcpy(void *dest, const void *src, size_t size) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < size; i++)
        d[i] = s[i];
    return dest;
}

/* memcmp - compare memory */
int memcmp(const void *m1, const void *m2, size_t size) {
    const unsigned char *p1 = (const unsigned char *)m1;
    const unsigned char *p2 = (const unsigned char *)m2;
    
    for (size_t i = 0; i < size; i++) {
        if (p1[i] != p2[i])
            return p1[i] - p2[i];
    }
    return 0;
}

/* memmove - copy memory (handles overlap) */
void* memmove(void *dest, const void *src, size_t size) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    
    if (d < s) {
        for (size_t i = 0; i < size; i++)
            d[i] = s[i];
    } else if (d > s) {
        for (size_t i = size; i > 0; i--)
            d[i-1] = s[i-1];
    }
    return dest;
}
