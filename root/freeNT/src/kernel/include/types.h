#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

/* Standard integer types */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long int64_t;

typedef uint64_t uintptr_t;
typedef int64_t intptr_t;
typedef uint64_t size_t;
typedef int64_t ssize_t;
typedef int pid_t;

/* Physical and Virtual Addresses */
typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;

/* File descriptor */
typedef int fd_t;

/* Boolean type */
typedef uint8_t bool_t;
#define TRUE 1
#define FALSE 0

/* NULL definition */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Likely/Unlikely hints */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#endif /* _KERNEL_TYPES_H */
