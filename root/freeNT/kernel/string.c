#include "string.h"

size_t strlen(const char* str)
{
    size_t len = 0;

    while(str[len])
        len++;

    return len;
}

int strcmp(const char* a, const char* b)
{
    while(*a && (*a == *b))
    {
        a++;
        b++;
    }

    return *(unsigned char*)a -
           *(unsigned char*)b;
}

int strncmp(const char* a,
            const char* b,
            size_t n)
{
    while(n && *a && (*a == *b))
    {
        a++;
        b++;
        n--;
    }

    if(n == 0)
        return 0;

    return *(unsigned char*)a -
           *(unsigned char*)b;
}

char* strcpy(char* dest,
             const char* src)
{
    char* ret = dest;

    while((*dest++ = *src++))
        ;

    return ret;
}

char* strncpy(char* dest,
              const char* src,
              size_t n)
{
    char* ret = dest;

    while(n && *src)
    {
        *dest++ = *src++;
        n--;
    }

    while(n--)
        *dest++ = 0;

    return ret;
}

void* memset(void* ptr,
             int value,
             size_t size)
{
    unsigned char* p = ptr;

    while(size--)
        *p++ = (unsigned char)value;

    return ptr;
}

void* memcpy(void* dest,
             const void* src,
             size_t size)
{
    unsigned char* d = dest;
    const unsigned char* s = src;

    while(size--)
        *d++ = *s++;

    return dest;
}

int memcmp(const void* a,
           const void* b,
           size_t size)
{
    const unsigned char* p1 = a;
    const unsigned char* p2 = b;

    while(size--)
    {
        if(*p1 != *p2)
            return *p1 - *p2;

        p1++;
        p2++;
    }

    return 0;
}