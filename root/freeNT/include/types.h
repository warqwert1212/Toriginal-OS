#ifndef FREENT_TYPES_H
#define FREENT_TYPES_H

/* Only define our types if the system headers haven't already */
#ifndef __UINT8_TYPE__
typedef unsigned char       uint8_t;
typedef signed char         int8_t;
typedef unsigned short      uint16_t;
typedef signed short        int16_t;
typedef unsigned int        uint32_t;
typedef signed int          int32_t;
typedef unsigned long       uint64_t;
typedef signed long         int64_t;
typedef unsigned long       size_t;
typedef signed long         ssize_t;
#else
#include <stdint.h>
#include <stddef.h>
typedef long int            ssize_t;
#endif

/* Additional kernel convenience typedefs */
typedef unsigned long uintptr_t;
typedef signed long intptr_t;
typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;
typedef int fd_t;
typedef int pid_t;

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

typedef uint8_t             boot;

#ifndef true
#define true  1
#define false 0
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

#endif