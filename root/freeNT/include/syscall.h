#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#include "types.h"

/* System call numbers.
 *
 * NUM_SYSCALLS grew from 27 to accommodate the app-layer syscalls
 * added for real applications (GUI apps, ported engines like DOOM,
 * eventually browser/game-client-class software): real mmap/brk
 * backed by physical memory (see process_mmap_anon()/process_brk() in
 * process.c), ioctl for device-specific control (framebuffer mode
 * queries, terminal control), poll for readiness checks without
 * busy-looping raw reads, signals for inter-process notification, and
 * thread primitives for apps that want more than one execution
 * context sharing their address space. Every new number is placed
 * with room around it in blocks of 10 so a future related syscall can
 * be inserted into the right neighborhood without renumbering
 * existing ones (renumbering is an ABI break - any already-compiled
 * .trp binary would silently call the wrong handler). */
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

/* ── Device control / readiness (30s) ─────────────────────────────── */
#define SYS_IOCTL       30
#define SYS_POLL        31

/* ── Signals (40s) ─────────────────────────────────────────────────── */
#define SYS_SIGACTION   40  /* install/query a handler for a signal   */
#define SYS_SIGPROCMASK 41  /* block/unblock a signal                 */
#define SYS_SIGRAISE    42  /* raise a signal on self (== kill(getpid())) */

/* ── Threads (50s) ─────────────────────────────────────────────────── */
#define SYS_THREAD_CREATE 50
#define SYS_THREAD_EXIT   51
#define SYS_THREAD_JOIN   52
#define SYS_THREAD_SELF   53

#define NUM_SYSCALLS    64

/* ── ioctl request codes ──────────────────────────────────────────────
 * Small, deliberately generic set covering what a framebuffer-blitting
 * app (DOOM-class) or a terminal-oriented app actually needs to query
 * at startup: screen geometry/format for the former, terminal size for
 * the latter. Modeled loosely on Linux's FBIOGET_VSCREENINFO/TIOCGWINSZ
 * so porting code that already knows those shapes is familiar, without
 * pulling in either ioctl numbering scheme wholesale. */
#define IOCTL_FB_GET_INFO     1  /* arg: fb_ioctl_info_t* (out) */
#define IOCTL_TTY_GET_WINSIZE 2  /* arg: tty_winsize_t*   (out) */

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;   /* bytes per scanline */
    uint32_t bpp;     /* bits per pixel */
    uint64_t fb_base; /* usable base address - valid under this kernel's
                        * flat identity map, i.e. directly dereferenceable */
} fb_ioctl_info_t;

typedef struct {
    uint16_t cols;
    uint16_t rows;
} tty_winsize_t;

/* ── poll() request/response ────────────────────────────────────────
 * Minimal readiness model: a file descriptor is "readable" if the
 * underlying fs layer reports data available, matching the shape of
 * POSIX's POLLIN without the full POLLOUT/POLLERR/POLLHUP event set
 * this kernel's fs layer doesn't yet distinguish (fs_read() either
 * has bytes or it doesn't - there's no separate "writable" or "error"
 * signal to report today). Kept intentionally small rather than
 * defining event bits for conditions nothing can actually produce
 * yet. */
#define POLLIN_READY   0x0001

typedef struct {
    int      fd;       /* process-local fd to check */
    uint32_t events;    /* requested - POLLIN_READY */
    uint32_t revents;   /* returned  - POLLIN_READY if ready */
} pollfd_t;

/* ── Signal numbers ────────────────────────────────────────────────── */
#define SIGKILL   9   /* always fatal, cannot be blocked/handled by
                        * convention - sys_sigaction refuses to install
                        * a handler for it, matching real Unix */
#define SIGTERM   15
#define SIGUSR1   10
#define SIGUSR2   12
#define SIGCHLD   17
#define SIGWINCH  28

/* ── mmap prot/flags ───────────────────────────────────────────────────
 * Subset of the real POSIX bit values (same numeric values as Linux's
 * <sys/mman.h> for PROT_*, so code ported from a real mmap() call site
 * doesn't need its flag constants translated) - only MAP_ANONYMOUS is
 * actually meaningful to process_mmap_anon() today (see its comment in
 * process.c: this kernel only supports anonymous backing, not file-
 * backed mmap), the rest are accepted and stored but not yet enforced
 * (no page-table permission bits are actually set differently for
 * PROT_READ vs PROT_WRITE under the current flat identity map). */
#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_SHARED     0x01
#define MAP_PRIVATE    0x02
#define MAP_ANONYMOUS  0x20
#define MAP_FAILED     ((uint64_t)-1)

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
