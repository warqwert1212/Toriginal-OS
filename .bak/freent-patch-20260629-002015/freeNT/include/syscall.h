#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#include "types.h"

/* System call numbers */
#define SYS_EXIT        0
#define SYS_FORK        1
#define SYS_EXEC        2
#define SYS_WAIT        3
#define SYS_OPEN        4
#define SYS_CLOSE       5
#define SYS_READ        6
#define SYS_WRITE       7
#define SYS_SEEK        8
#define SYS_STAT        9
#define SYS_MKDIR       10
#define SYS_RMDIR       11
#define SYS_UNLINK      12
#define SYS_KILL        13
#define SYS_GETPID      14
#define SYS_GETPPID     15
#define SYS_GETUID      16
#define SYS_GETGID      17
#define SYS_GETTIMEOFDAY 18
#define SYS_BRK         19
#define SYS_MMAP        20
#define SYS_MUNMAP      21
#define SYS_YIELD       22
#define SYS_GETCWD      23
#define SYS_CHDIR       24
#define SYS_DUP         25
#define SYS_DUP2        26

#define NUM_SYSCALLS    27

/* Syscall handler function type */
typedef uint64_t (*syscall_handler_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

/* Initialize syscall handling */
void syscall_init(void);

/* Register syscall handler */
void syscall_register_handler(uint32_t syscall_num, syscall_handler_t handler);

/* Dispatch syscall */
uint64_t syscall_dispatch(uint32_t syscall_num, uint64_t arg1, uint64_t arg2, 
                          uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);

#endif /* _KERNEL_SYSCALL_H */
