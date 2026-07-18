/* trsys.h - User-space syscall ABI for Toriginal OS .trp apps.
 *
 * This is the FIRST real user-space syscall wrapper in the codebase -
 * previously the only example app (root/sys/userpc/home/.../oobe.c)
 * called kernel-internal functions directly as `extern`s, which only
 * works because it's actually compiled INTO the kernel binary, not
 * loaded as a separate .trp payload the way app.ld's ENTRY
 * (main_explorer_executable) and the TRP loader (trploader.h) are
 * built for. A real .trp app can't call kernel C functions directly
 * (it doesn't link against kernel object files) - it has to cross
 * into the kernel via the `syscall` instruction, the same way any
 * real userspace program on any real OS does.
 *
 * ABI, reverse-engineered exactly from syscall.c's syscall_entry_stub
 * and syscall_dispatch signature (both in root/freeNT/kernel/
 * syscall.c) rather than assumed: syscall number in RAX, arguments in
 * RDI, RSI, RDX, R10, R8, R9 (R10 instead of RCX, because the
 * `syscall` instruction itself clobbers RCX/R11 - standard x86-64
 * SysV syscall convention, not just "coincidentally like Linux").
 * Return value comes back in RAX. Every wrapper below is a thin
 * `syscall` instruction plus the register shuffle needed to get
 * arguments into the right places from a normal C function call -
 * nothing here talks to any kernel data structure directly.
 */
#ifndef _TRSYS_H
#define _TRSYS_H

#include <stdint.h>

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
#define SYS_IOCTL       30
#define SYS_POLL        31
#define SYS_SIGACTION   40
#define SYS_SIGPROCMASK 41
#define SYS_SIGRAISE    42
#define SYS_THREAD_CREATE 50
#define SYS_THREAD_EXIT   51
#define SYS_THREAD_JOIN   52
#define SYS_THREAD_SELF   53

#define IOCTL_FB_GET_INFO     1
#define IOCTL_TTY_GET_WINSIZE 2

typedef struct {
    uint32_t width, height, pitch, bpp;
    uint64_t fb_base;
} fb_ioctl_info_t;

typedef struct {
    uint16_t cols, rows;
} tty_winsize_t;

#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0x100
#define O_TRUNC   0x200

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_ANONYMOUS  0x20

/* Six-argument form - every real syscall funnels through this one so
 * the inline asm (the only place that can go wrong at the ABI level)
 * exists in exactly one place. */
static inline int64_t trsys_call6(int64_t num, int64_t a1, int64_t a2,
                                   int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8  __asm__("r8")  = a5;
    register int64_t r9  __asm__("r9")  = a6;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t trsys_call0(int64_t num) {
    return trsys_call6(num, 0, 0, 0, 0, 0, 0);
}
static inline int64_t trsys_call1(int64_t num, int64_t a1) {
    return trsys_call6(num, a1, 0, 0, 0, 0, 0);
}
static inline int64_t trsys_call2(int64_t num, int64_t a1, int64_t a2) {
    return trsys_call6(num, a1, a2, 0, 0, 0, 0);
}
static inline int64_t trsys_call3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    return trsys_call6(num, a1, a2, a3, 0, 0, 0);
}

/* - Ergonomic wrappers - */

static inline void    sys_exit(int status)              { trsys_call1(SYS_EXIT, status); }
static inline int64_t sys_write(int fd, const void *buf, uint64_t len) {
    return trsys_call3(SYS_WRITE, fd, (int64_t)(uintptr_t)buf, (int64_t)len);
}
static inline int64_t sys_read(int fd, void *buf, uint64_t len) {
    return trsys_call3(SYS_READ, fd, (int64_t)(uintptr_t)buf, (int64_t)len);
}
static inline int     sys_open(const char *path, int flags, int mode) {
    return (int)trsys_call3(SYS_OPEN, (int64_t)(uintptr_t)path, flags, mode);
}
static inline int     sys_close(int fd)                 { return (int)trsys_call1(SYS_CLOSE, fd); }
static inline int64_t sys_seek(int fd, int64_t off, int whence) {
    return trsys_call3(SYS_SEEK, fd, off, whence);
}
static inline int     sys_getpid(void)                  { return (int)trsys_call0(SYS_GETPID); }
static inline int     sys_dup(int fd)                   { return (int)trsys_call1(SYS_DUP, fd); }
static inline int     sys_dup2(int oldfd, int newfd)     { return (int)trsys_call2(SYS_DUP2, oldfd, newfd); }
static inline int64_t sys_ioctl(int fd, int64_t req, void *arg) {
    return trsys_call3(SYS_IOCTL, fd, req, (int64_t)(uintptr_t)arg);
}
static inline void   *sys_brk(void *new_end) {
    return (void *)(uintptr_t)trsys_call1(SYS_BRK, (int64_t)(uintptr_t)new_end);
}
static inline void   *sys_mmap_anon(uint64_t length, int prot) {
    int64_t r = trsys_call3(SYS_MMAP, (int64_t)length, prot, MAP_ANONYMOUS);
    return (r == -1) ? (void *)0 : (void *)(uintptr_t)r;
}
static inline void    sys_yield(void)                    { trsys_call0(SYS_YIELD); }

#endif /* _TRSYS_H */
