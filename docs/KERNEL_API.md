# freeNT Kernel API Reference

## Kernel Headers

Include these in your kernel code:

```c
#include "kernel/types.h"    /* Type definitions */
#include "kernel/config.h"   /* Configuration */
#include "kernel/kernel.h"   /* Kernel main */
#include "kernel/mm.h"       /* Memory management */
#include "kernel/process.h"  /* Process management */
#include "kernel/fs.h"       /* Filesystem */
#include "kernel/interrupts.h" /* Interrupt handling */
#include "kernel/syscall.h"  /* System calls */
#include "kernel/loader.h"   /* Executable loaders */
#include "kernel/io.h"       /* I/O operations */
#include "kernel/string.h"   /* String utilities */
```

## Memory Management API

### Functions

```c
/* Physical memory allocation */
paddr_t mm_alloc_pages(size_t num_pages);
void mm_free_pages(paddr_t paddr, size_t num_pages);

/* Virtual memory setup */
void mm_init_paging(void);
void mm_enable_paging(void);
void mm_map_page(vaddr_t vaddr, paddr_t paddr, uint64_t flags);
void mm_unmap_page(vaddr_t vaddr);

/* Kernel heap */
void mm_init_heap(vaddr_t heap_start, vaddr_t heap_end);
void* kmalloc(size_t size);
void kfree(void *ptr);
void* krealloc(void *ptr, size_t new_size);
```

### Page Flags

```c
#define PAGE_PRESENT        0x001
#define PAGE_WRITE          0x002
#define PAGE_USER           0x004
#define PAGE_ACCESSED       0x020
#define PAGE_DIRTY          0x040
#define PAGE_GLOBAL         0x100
```

## Process Management API

### Functions

```c
void process_init(void);
process_t* process_create(const char *name, uint8_t priority);
pid_t process_fork(void);
int process_exec(pid_t pid, const char *filename, const char **argv);
int process_wait(pid_t pid, int *status);
void process_exit(int status);
void process_kill(pid_t pid);
process_t* process_get_current(void);
process_t* process_get_by_pid(pid_t pid);

void scheduler_init(void);
process_t* scheduler_next(void);
void scheduler_yield(void);
```

### Process States

```c
PROCESS_CREATED
PROCESS_RUNNING
PROCESS_RUNNABLE
PROCESS_WAITING
PROCESS_STOPPED
PROCESS_ZOMBIE
PROCESS_TERMINATED
```

## Filesystem API

### Functions

```c
void fs_init(void);
fd_t fs_open(const char *path, int flags, int mode);
ssize_t fs_read(fd_t fd, void *buf, size_t count);
ssize_t fs_write(fd_t fd, const void *buf, size_t count);
int fs_close(fd_t fd);
int fs_seek(fd_t fd, int64_t offset, int whence);
int fs_stat(const char *path, inode_t *stat);
int fs_mkdir(const char *path, int mode);
int fs_rmdir(const char *path);
int fs_readdir(fd_t fd, dir_entry_t *entry);
inode_t* fs_resolve_path(const char *path);
```

### File Permissions

```c
#define FILE_PERM_OWNER_R  0400
#define FILE_PERM_OWNER_W  0200
#define FILE_PERM_OWNER_X  0100
#define FILE_PERM_GROUP_R  0040
#define FILE_PERM_GROUP_W  0020
#define FILE_PERM_GROUP_X  0010
#define FILE_PERM_OTHER_R  0004
#define FILE_PERM_OTHER_W  0002
#define FILE_PERM_OTHER_X  0001
```

## Interrupt/Exception API

### Functions

```c
void interrupts_init(void);
void idt_init(void);
void interrupts_register_handler(uint32_t interrupt_num, 
                                interrupt_handler_t handler);
void interrupts_enable(void);
void interrupts_disable(void);
```

### Handler Signature

```c
typedef void (*interrupt_handler_t)(interrupt_frame_t *frame);

struct interrupt_frame {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t interrupt_number;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};
```

## System Call API

### Functions

```c
void syscall_init(void);
void syscall_register_handler(uint32_t syscall_num, 
                             syscall_handler_t handler);
uint64_t syscall_dispatch(uint32_t syscall_num, 
                         uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, 
                         uint64_t arg5, uint64_t arg6);
```

### Syscall Numbers

```c
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
#define SYS_GETPID      14
#define SYS_GETPPID     15
#define SYS_YIELD       22
```

## Executable Loader API

### Functions

```c
int loader_load_exe(const char *filename, pid_t pid);
int loader_load_trp(const char *filename, pid_t pid);
int loader_load_elf(const char *filename, pid_t pid);
int loader_apply_relocations(pid_t pid, 
                            relocation_entry_t *relocs, 
                            size_t count);
```

## I/O API

### Functions

```c
void io_put_char(char c);
void io_put_string(const char *str);
void io_clear_screen(void);
void io_write_char(uint16_t x, uint16_t y, char c, uint8_t color);

void serial_init(void);
void serial_putc(char c);
char serial_getc(void);

/* Low-level I/O */
void outb(uint16_t port, uint8_t value);
uint8_t inb(uint16_t port);
void outw(uint16_t port, uint16_t value);
uint16_t inw(uint16_t port);
void outl(uint16_t port, uint32_t value);
uint32_t inl(uint16_t port);
```

## String Utilities

```c
size_t strlen(const char *str);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char* strcpy(char *dest, const char *src);
char* strncpy(char *dest, const char *src, size_t n);
char* strcat(char *dest, const char *src);
char* strchr(const char *str, int c);
char* strstr(const char *haystack, const char *needle);

void* memset(void *mem, int value, size_t size);
void* memcpy(void *dest, const void *src, size_t size);
int memcmp(const void *m1, const void *m2, size_t size);
void* memmove(void *dest, const void *src, size_t size);
```

## Example: Creating a Kernel Driver

```c
#include "kernel/interrupts.h"
#include "kernel/io.h"

void my_irq_handler(interrupt_frame_t *frame) {
    io_put_string("IRQ received!\n");
}

void init_my_driver(void) {
    interrupts_register_handler(0x20, my_irq_handler);
}
```

## Example: Using System Calls

```c
/* From user-mode program */
#include "kernel/syscall.h"

int write(int fd, const void *buf, size_t count) {
    uint64_t result;
    asm volatile("syscall"
        : "=a"(result)
        : "a"(SYS_WRITE), "D"(fd), "S"(buf), "d"(count)
        : "rcx", "r11"
    );
    return (int)result;
}
```
