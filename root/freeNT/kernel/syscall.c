
#include <stdint.h>
#include "serial.h"
#include "io.h"
#include "installer.h"
#include "kernel.h"
#include "syscall.h"
#include "process.h"
#include "fs.h"
#include "rtc.h"
#include "graphics_core.h"  /* g_framebuffer - IOCTL_FB_GET_INFO */
#include "gfx_terminal.h"   /* gterm_is_active/gterm_get_grid_size - IOCTL_TTY_GET_WINSIZE */
#include "keyboard.h"       /* keyboard_has_input/keyboard_getc_nb - stdin (fd 0) */
#include "paging.h"         /* paging_user_range_ok() - real per-process
                              * pointer validation for syscall_ptr_ok()/
                              * syscall_buf_ok(), see their updated comment */
#include "wm.h"             /* wm_create_window_userbacked/wm_poll_event/
                              * wm_destroy_window - the GUI syscalls */
#include "pmm.h"            /* pmm_free_frame - releasing a destroyed
                              * window's pixel-buffer frames */
#include "string.h"         /* memcpy - copying the GUI syscalls' result
                              * structs out to user memory */

#define MSR_EFER          0xC0000080
#define MSR_STAR          0xC0000081
#define MSR_LSTAR         0xC0000082
#define MSR_SFMASK        0xC0000084
#define MSR_KERNEL_GS_BASE 0xC0000102

/* FIX: the old syscall_entry_stub ran entirely on whatever RSP the user
 * process happened to have at the time of `syscall` - the CPU does NOT
 * switch stacks automatically for SYSCALL/SYSRET (unlike an interrupt
 * gate through the TSS). A process with a corrupt, tiny, or unmapped
 * stack pointer would fault the moment the stub tried to push onto it,
 * before any handler could see it. This gives syscall entry its own
 * known-good stack, switched to via swapgs + a per-cpu scratch struct,
 * the standard x86-64 pattern for this.
 *
 * Layout matters: offset 0 is what `swapgs; mov %gs:0, %rsp` reads (the
 * kernel stack top), offset 8 is scratch space to stash the user RSP
 * while running on the kernel stack, so it can be restored before
 * sysretq. */
typedef struct {
    uint64_t kernel_rsp;
    uint64_t user_rsp_scratch;
} syscall_percpu_t;

static uint8_t g_syscall_kstack[16384] __attribute__((aligned(16)));
static syscall_percpu_t g_syscall_percpu;

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile("wrmsr"
        : : "c"(msr),
            "a"((uint32_t)(val & 0xFFFFFFFF)),
            "d"((uint32_t)(val >> 32)));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static syscall_handler_t g_handlers[NUM_SYSCALLS];

/* FIX: none of the syscall handlers validated user-supplied pointers at
 * all before dereferencing them - a process could pass any integer and
 * have it read/written as a kernel pointer. This OS doesn't have a real
 * user/kernel address-space split yet (single flat identity-mapped
 * range, no per-process page-table isolation enforced), so this can't
 * be a full "is this actually the caller's memory" check - that needs
 * the paging work this kernel doesn't have yet. What it *can* do cheaply
 * is reject the low guard page (catches NULL/near-NULL-offset bugs, by
 * far the most common mistake) and reject length values that would wrap
 * the pointer arithmetic. Treat this as a stopgap, not a security
 * boundary. */
#define SYSCALL_GUARD_PAGE 0x1000ULL

/* CHANGED: these three used to only check "is the pointer value
 * numerically above the first 4 KiB" - a NULL-pointer-style sanity
 * check, not real validation, and it was ALL that was possible before
 * real per-process address spaces existed (see paging.c's top
 * comment) - every process shared the kernel's own tables, so there
 * was no separate "is this actually mapped for THIS process, as
 * user-accessible memory" question to ask yet. Now that there is
 * (paging.c's mm_create_address_space()/per-process PML4s), these
 * gate the same way any real OS's copy_from_user/copy_to_user does:
 * walk the CALLING process's own page tables (paging_user_range_ok())
 * and confirm the pages are actually present and marked PAGE_USER,
 * not just "some address bigger than 0x1000". A pointer that's
 * unmapped, kernel-only, or wanders outside what this process ever
 * had mapped now correctly fails here instead of either page-faulting
 * the kernel or - worse - the kernel touching memory on the process's
 * behalf that the process itself never had rights to. */
static int syscall_ptr_ok(uint64_t ptr) {
    if (ptr < SYSCALL_GUARD_PAGE) return 0;
    return paging_user_range_ok(ptr, 1);
}

static int syscall_ptr_ok_or_null(uint64_t ptr) {
    if (ptr == 0) return 1;
    if (ptr < SYSCALL_GUARD_PAGE) return 0;
    return paging_user_range_ok(ptr, 1);
}

static int syscall_buf_ok(uint64_t ptr, uint64_t len) {
    if (ptr < SYSCALL_GUARD_PAGE) return 0;
    if (len > 0 && ptr + len < ptr) return 0; /* overflow */
    if (len == 0) return 1;
    return paging_user_range_ok(ptr, len);
}

void syscall_register_handler(uint32_t syscall_num, syscall_handler_t handler) {
    if (syscall_num < NUM_SYSCALLS) g_handlers[syscall_num] = handler;
}

uint64_t syscall_dispatch(uint32_t syscall_num, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    if (syscall_num >= NUM_SYSCALLS || !g_handlers[syscall_num]) {
        return (uint64_t)-1;
    }
    return g_handlers[syscall_num](arg1, arg2, arg3, arg4, arg5, arg6);
}

static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    if (!syscall_buf_ok(buf, len)) return (uint64_t)-1;
    const char *p = (const char *)(uintptr_t)buf;
    if (fd == 1 || fd == 2) {
        for (uint64_t i = 0; i < len; i++) io_put_char(p[i]);
        return len;
    }
    /* FIX: previously treated every fd >= 3 as a raw global fd_t,
     * bypassing the per-process FD table entirely - a process's
     * dup()'d or close()'d fd numbers had no effect on what sys_write
     * actually did, because this never consulted proc_fd[] at all. */
    process_t *cur = process_get_current();
    int global_fd = process_fd_get(cur, (int)fd);
    if (global_fd < 0) return (uint64_t)-1;
    return (uint64_t)fs_write((fd_t)global_fd, p, (size_t)len);
}

static uint64_t sys_exit(uint64_t status, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_set_exit_context(1);
    process_exit((int)status);
    process_set_exit_context(0);

    return 0;
}

static uint64_t sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    return cur ? (uint64_t)cur->pid : (uint64_t)-1;
}

static uint64_t sys_getppid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    return cur ? (uint64_t)cur->ppid : (uint64_t)-1;
}

static uint64_t sys_fork(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return (uint64_t)process_fork();
}

static uint64_t sys_exec(uint64_t pid, uint64_t filename, uint64_t argv, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    if (!syscall_ptr_ok(filename) || !syscall_ptr_ok_or_null(argv)) return (uint64_t)-1;
    return (uint64_t)process_exec((pid_t)pid, (const char *)(uintptr_t)filename, (const char **)(uintptr_t)argv);
}

static uint64_t sys_wait(uint64_t pid, uint64_t status_ptr, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_ptr_ok_or_null(status_ptr)) return (uint64_t)-1;
    return (uint64_t)process_wait((pid_t)pid, (int *)(uintptr_t)status_ptr);
}

static uint64_t sys_kill(uint64_t pid, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_kill((pid_t)pid);
    return 0;
}

static uint64_t sys_yield(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return 0;
}

static uint64_t sys_open(uint64_t path, uint64_t flags, uint64_t mode, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    if (!syscall_ptr_ok(path)) return (uint64_t)-1;
    fd_t global_fd = fs_open((const char *)(uintptr_t)path, (int)flags, (int)mode);
    if (global_fd < 0) return (uint64_t)-1;

    /* FIX: previously returned the raw global fd_t straight to the
     * caller, so every process shared one flat fd numbering space -
     * two processes opening files independently could easily collide
     * on "fd 3", and close()ing fd 3 in one process would silently
     * invalidate fd 3 in every other process that happened to have
     * opened a file at the same global slot. Installing into the
     * caller's own proc_fd[] table gives each process its own,
     * independent low-numbered fd namespace, same as real Unix. */
    process_t *cur = process_get_current();
    int local_fd = process_fd_install(cur, (int)global_fd);
    if (local_fd < 0) {
        fs_close(global_fd);
        return (uint64_t)-1;
    }
    return (uint64_t)local_fd;
}

static uint64_t sys_close(uint64_t fd, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    int global_fd = process_fd_get(cur, (int)fd);
    if (global_fd < 0) return (uint64_t)-1;

    /* Only actually close the underlying global fd if no other
     * process-local slot (dup'd via SYS_DUP/SYS_DUP2) still points at
     * it - otherwise closing one dup'd handle would invalidate every
     * other handle sharing the same global fd, which is exactly the
     * bug dup()/dup2() exist to let callers avoid (e.g. redirecting
     * stdout to a file: dup2(file_fd, 1) then close(file_fd) must
     * leave fd 1 usable). Scans this process's own table only - the
     * global trpfs table has no cross-process refcount today (see
     * process.h's PROCESS_MAX_FDS comment), so a dup shared across
     * fork()'d processes isn't refcounted across that boundary yet;
     * within one process (the common dup/dup2 use case) this is
     * correct. */
    process_fd_close(cur, (int)fd);

    int still_referenced = 0;
    for (int i = 0; i < PROCESS_MAX_FDS; i++) {
        if (process_fd_get(cur, i) == global_fd) { still_referenced = 1; break; }
    }
    if (!still_referenced) {
        return (uint64_t)fs_close((fd_t)global_fd);
    }
    return 0;
}

static uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    if (!syscall_buf_ok(buf, count)) return (uint64_t)-1;

    /* FIX: fd 0 (stdin) had no handling at all - sys_write() already
     * special-cases fd 1/2 as "the console", but sys_read() fell
     * straight through to process_fd_get()/fs_read(), which returns
     * -1 for fd 0 (nothing was ever installed there), meaning every
     * previous .trp app was structurally unable to read keyboard
     * input through the syscall interface - a real terminal-class app
     * needs this to exist at all. Non-blocking (returns 0 immediately
     * if nothing is buffered, doesn't spin the CPU waiting) - matches
     * O_NONBLOCK-style stdin semantics rather than the blocking
     * default a real terminal's fd 0 usually has, since this kernel
     * has no process-parking/wake mechanism yet to block on
     * correctly (see process_thread_join()'s comment on the same
     * gap) - a caller wanting blocking behavior loops on sys_yield()
     * between zero-byte reads itself, which is exactly what
     * term_getline() in root/apps/term/term.c does. */
    if (fd == 0) {
        char *dst = (char *)(uintptr_t)buf;
        uint64_t n = 0;
        while (n < count) {
            int c = console_getc_nb();      /* PS/2 + USB HID + COM1 */
            if (!c) break;
            dst[n++] = (char)c;
        }
        return n;
    }

    process_t *cur = process_get_current();
    int global_fd = process_fd_get(cur, (int)fd);
    if (global_fd < 0) return (uint64_t)-1;
    return (uint64_t)fs_read((fd_t)global_fd, (void *)(uintptr_t)buf, (size_t)count);
}

static uint64_t sys_seek(uint64_t fd, uint64_t offset, uint64_t whence, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    int global_fd = process_fd_get(cur, (int)fd);
    if (global_fd < 0) return (uint64_t)-1;
    return (uint64_t)fs_seek((fd_t)global_fd, (int64_t)offset, (int)whence);
}

static uint64_t sys_stat(uint64_t path, uint64_t out, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_ptr_ok(path) || !syscall_ptr_ok(out)) return (uint64_t)-1;
    return (uint64_t)fs_stat((const char *)(uintptr_t)path, (inode_t *)(uintptr_t)out);
}

static uint64_t sys_mkdir(uint64_t path, uint64_t mode, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_ptr_ok(path)) return (uint64_t)-1;
    return (uint64_t)fs_mkdir((const char *)(uintptr_t)path, (int)mode);
}

static uint64_t sys_unlink(uint64_t path, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_ptr_ok(path)) return (uint64_t)-1;
    return (uint64_t)fs_unlink((const char *)(uintptr_t)path);
}

typedef struct {
    uint64_t tv_sec;
    uint64_t tv_usec;
} syscall_timeval_t;

static uint64_t sys_gettimeofday(uint64_t tv_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_ptr_ok(tv_ptr)) return (uint64_t)-1;

    rtc_time_t t;
    rtc_read(&t);

    static const uint16_t days_before_month[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
    uint64_t years_since_epoch = t.year >= 1970 ? (uint64_t)(t.year - 1970) : 0;
    uint64_t leap_days = (years_since_epoch + 1) / 4;
    uint64_t day_of_year = days_before_month[(t.month - 1) % 12] + (t.day - 1);
    if (t.month > 2 && (t.year % 4 == 0)) day_of_year += 1;
    uint64_t total_days = years_since_epoch * 365 + leap_days + day_of_year;

    uint64_t seconds = total_days * 86400ULL + (uint64_t)t.hour * 3600ULL
                      + (uint64_t)t.minute * 60ULL + (uint64_t)t.second;

    syscall_timeval_t *tv = (syscall_timeval_t *)(uintptr_t)tv_ptr;
    tv->tv_sec = seconds;
    tv->tv_usec = 0;
    return 0;
}

static uint64_t sys_rmdir(uint64_t path, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_ptr_ok(path)) return (uint64_t)-1;
    /* fs.h exposes fs_unlink() but no distinct fs_rmdir() - trpfs
     * doesn't yet enforce "only unlink an empty directory" as a rule
     * separate from ordinary unlink, so this is a thin, honest alias
     * rather than a real recursive-refusal check that would just be
     * dead code against the current fs layer. */
    return (uint64_t)fs_unlink((const char *)(uintptr_t)path);
}

static uint64_t sys_getuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    /* Single-user system (see auth.h/auth.c) - every process runs as
     * the one account that logged in, so uid/gid 0 is simply "the
     * user", not "root" in the Unix-privilege sense (there's no
     * privilege separation to enforce that distinction yet). Returning
     * a fixed value rather than -1/ENOSYS means ported apps that just
     * want *a* stable uid for e.g. temp-file naming get one, instead
     * of having to special-case "getuid failed". */
    return 0;
}

static uint64_t sys_getgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return 0;
}

static uint64_t sys_brk(uint64_t new_end, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    if (!cur) return 0;
    return (uint64_t)process_brk(cur, (vaddr_t)new_end);
}

static uint64_t sys_mmap(uint64_t length, uint64_t prot, uint64_t flags, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    /* addr/fd/offset (the usual mmap(2) full argument list) are
     * deliberately not accepted here: this kernel only supports
     * anonymous, kernel-chosen-address mappings (see process_t's
     * mmap_region_t comment) - a hinted address or file-backed
     * mapping would silently be ignored if accepted, which is worse
     * than not accepting the parameters at all. Callers wanting a
     * file's contents in memory should fs_open()+fs_read() instead;
     * MAP_ANONYMOUS is required in flags as a clear signal the caller
     * knows this is an anonymous-only mmap. */
    if (!(flags & MAP_ANONYMOUS)) return MAP_FAILED;
    process_t *cur = process_get_current();
    if (!cur) return MAP_FAILED;
    vaddr_t base = process_mmap_anon(cur, length, (uint32_t)prot, (uint32_t)flags);
    return base ? (uint64_t)base : MAP_FAILED;
}

static uint64_t sys_munmap(uint64_t addr, uint64_t length, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    if (!cur) return (uint64_t)-1;
    return (uint64_t)process_munmap(cur, (vaddr_t)addr, length);
}

static uint64_t sys_getcwd(uint64_t buf, uint64_t size, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_buf_ok(buf, size)) return (uint64_t)-1;
    process_t *cur = process_get_current();
    const char *cwd = process_get_cwd(cur);

    uint64_t len = 0;
    while (cwd[len]) len++;
    if (len + 1 > size) return (uint64_t)-1; /* buffer too small, like real getcwd(2) */

    char *dst = (char *)(uintptr_t)buf;
    for (uint64_t i = 0; i <= len; i++) dst[i] = cwd[i];
    return buf; /* real getcwd(2) returns the buffer pointer on success */
}

static uint64_t sys_chdir(uint64_t path, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_ptr_ok(path)) return (uint64_t)-1;
    process_t *cur = process_get_current();

    /* Real chdir(2) fails if the path doesn't exist or isn't a
     * directory - verify via fs_stat() rather than blindly accepting
     * any string, which would let a process "cd" into a path that
     * silently breaks every subsequent relative fs_open() instead of
     * failing at the point the mistake was actually made. */
    inode_t st;
    if (fs_stat((const char *)(uintptr_t)path, &st) != 0) return (uint64_t)-1;

    return (uint64_t)process_set_cwd(cur, (const char *)(uintptr_t)path);
}

static uint64_t sys_dup(uint64_t fd, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    int new_fd = process_fd_dup(cur, (int)fd);
    return new_fd < 0 ? (uint64_t)-1 : (uint64_t)new_fd;
}

static uint64_t sys_dup2(uint64_t old_fd, uint64_t new_fd, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    int result = process_fd_dup2(cur, (int)old_fd, (int)new_fd);
    return result < 0 ? (uint64_t)-1 : (uint64_t)result;
}

static uint64_t sys_ioctl(uint64_t fd, uint64_t request, uint64_t arg, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)fd; (void)a4; (void)a5; (void)a6;
    /* fd is accepted (matches the real ioctl(2) signature so ported
     * code compiles unmodified) but not currently consulted - both
     * request codes implemented today describe global device state
     * (the one framebuffer, the one active terminal) rather than
     * per-fd state, so which fd asked doesn't change the answer yet.
     * A future ioctl needing per-fd behavior should switch on fd via
     * process_fd_get() the same way sys_read/sys_write do. */
    if (!syscall_ptr_ok(arg)) return (uint64_t)-1;

    switch (request) {
    case IOCTL_FB_GET_INFO: {
        fb_ioctl_info_t *out = (fb_ioctl_info_t *)(uintptr_t)arg;
        out->width   = g_framebuffer.width;
        out->height  = g_framebuffer.height;
        out->pitch   = g_framebuffer.pitch;
        out->bpp     = g_framebuffer.depth;
        out->fb_base = (uint64_t)(uintptr_t)g_framebuffer.framebuffer;
        return 0;
    }
    case IOCTL_TTY_GET_WINSIZE: {
        tty_winsize_t *out = (tty_winsize_t *)(uintptr_t)arg;
        if (gterm_is_active()) {
            uint32_t cols, rows;
            gterm_get_grid_size(&cols, &rows);
            out->cols = (uint16_t)cols;
            out->rows = (uint16_t)rows;
        } else {
            /* Standard 80x25 VGA text mode geometry - real, not a
             * guess, since vga.c's VGA_WIDTH/VGA_HEIGHT are fixed at
             * exactly this when gterm isn't active. */
            out->cols = 80;
            out->rows = 25;
        }
        return 0;
    }
    default:
        return (uint64_t)-1;
    }
}

static uint64_t sys_poll(uint64_t fds_ptr, uint64_t nfds, uint64_t timeout_ms, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    (void)timeout_ms; /* Genuine blocking-with-timeout needs a park/wake
                        * primitive this kernel doesn't have yet (see
                        * process_thread_join()'s comment on the same
                        * gap) - this poll is a single readiness SNAPSHOT,
                        * matching poll(2)'s contract for timeout==0
                        * exactly and returning immediately regardless
                        * of what a nonzero timeout asked for, rather
                        * than silently pretending to honor a wait it
                        * can't actually perform. */
    if (!syscall_buf_ok(fds_ptr, nfds * sizeof(pollfd_t))) return (uint64_t)-1;

    process_t *cur = process_get_current();
    pollfd_t *fds = (pollfd_t *)(uintptr_t)fds_ptr;
    uint64_t ready_count = 0;

    for (uint64_t i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        if (!(fds[i].events & POLLIN_READY)) continue;

        if (fds[i].fd == 0) {
            /* stdin - see sys_read()'s comment on why this is checked
             * directly against the keyboard driver rather than through
             * process_fd_get()/fs_data_available() like every other fd. */
            if (keyboard_has_input()) {
                fds[i].revents |= POLLIN_READY;
                ready_count++;
            }
            continue;
        }

        int global_fd = process_fd_get(cur, fds[i].fd);
        if (global_fd < 0) continue;

        if (fs_data_available((fd_t)global_fd)) {
            fds[i].revents |= POLLIN_READY;
            ready_count++;
        }
    }
    return ready_count;
}

static uint64_t sys_sigaction(uint64_t signum, uint64_t handler, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (signum >= PROCESS_MAX_SIGNALS) return (uint64_t)-1;
    if (signum == SIGKILL) return (uint64_t)-1; /* not maskable/handleable - see syscall.h */
    process_t *cur = process_get_current();
    process_signal_set_handler(cur, (int)signum, (signal_handler_t)(uintptr_t)handler);
    return 0;
}

static uint64_t sys_sigprocmask(uint64_t signum, uint64_t blocked, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (signum >= PROCESS_MAX_SIGNALS) return (uint64_t)-1;
    if (signum == SIGKILL) return (uint64_t)-1;
    process_t *cur = process_get_current();
    process_signal_block(cur, (int)signum, (int)blocked);
    return 0;
}

static uint64_t sys_sigraise(uint64_t signum, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (signum >= PROCESS_MAX_SIGNALS) return (uint64_t)-1;
    process_t *cur = process_get_current();
    process_signal_raise(cur, (int)signum);
    return 0;
}

/* ── GUI syscalls ─────────────────────────────────────────────────────────
 * See kernel/wm.c's wm_create_window_userbacked()/wm_push_event()/
 * wm_poll_event() and kernel/pmm.c's pmm_alloc_contiguous_frames() for
 * the real underlying mechanics these wrap. This is the FIRST syscall
 * surface exposing any GUI capability to a userspace TRP program at
 * all (confirmed earlier: zero GUI-related syscalls existed before
 * this - the compositor/wm were kernel-internal only). */

/* Mirrors what trplibc's own trp_gui.h declares byte-for-byte (same
 * discipline as every other kernel<->trplibc struct-sharing syscall
 * this engagement has built - sys_stat()'s inode_t translation,
 * sys_gettimeofday()'s syscall_timeval_t, etc.) - a mismatch here
 * would silently corrupt every field past the point they diverge. */
typedef struct {
    uint32_t window_id;
    uint64_t pixels_vaddr;   /* where the window's pixel buffer was
                               * mapped in the CALLING process's own
                               * address space - see mm_map_page_in()
                               * call below */
    int32_t  pitch;
    int32_t  client_w;
    int32_t  client_h;
} syscall_wm_create_result_t;

#define SYS_WM_PIXELS_VADDR_BASE 0x600000000ULL /* far above the fixed
    * 0x500000 TRP code load window (kernel64.ld) and the 0x7FFFFFFF000
    * default stack top (process.c) - a window's pixel buffer lives in
    * its own, separate, fixed region of the process's virtual address
    * space so it can never collide with either. Windows are mapped
    * one after another starting here, each window_index slots spaced
    * generously apart (see the multiplication below) so even a large
    * window's buffer can't run into the next slot. */

static uint64_t sys_wm_create_window(uint64_t x, uint64_t y, uint64_t w, uint64_t h,
                                      uint64_t title_ptr, uint64_t out_result_ptr) {
    if (!syscall_ptr_ok_or_null(title_ptr)) return (uint64_t)-1;
    if (!syscall_buf_ok(out_result_ptr, sizeof(syscall_wm_create_result_t))) return (uint64_t)-1;

    process_t *cur = process_get_current();
    int slot = -1;
    for (int i = 0; i < PROCESS_MAX_OWNED_WINDOWS; i++) {
        if (cur->owned_windows[i] == 0) { slot = i; break; }
    }
    if (slot < 0) return (uint64_t)-1; /* process already owns the max
        * number of windows this table tracks - see process.h's
        * PROCESS_MAX_OWNED_WINDOWS comment */

    /* Compute the frame count the SAME way wm_create_window_userbacked()
     * internally will, so the caller-supplied array is sized correctly
     * - duplicated here deliberately rather than having wm.c report it
     * back after the fact, since the physical-frames array needs to
     * exist (sized correctly) BEFORE calling into wm.c at all. A
     * generous fixed cap (64 frames = 256 KiB window) bounds this
     * array's stack usage to something reasonable rather than sizing
     * it from an unbounded, syscall-caller-controlled w/h. */
    #define SYS_WM_MAX_WINDOW_FRAMES 64
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return (uint64_t)-1; /* reject
        * absurd/zero dimensions before any allocation is attempted -
        * matches wm_create_window_userbacked()'s own internal minimum
        * clamp (100x100) existing anyway, this just fails fast for
        * something that would obviously never fit SYS_WM_MAX_WINDOW_FRAMES */

    uint64_t phys_frames[SYS_WM_MAX_WINDOW_FRAMES];
    uint32_t frame_count = 0;

    char title_buf[64];
    const char *title_arg = 0;
    if (title_ptr) {
        if (!syscall_buf_ok(title_ptr, 1)) return (uint64_t)-1;
        /* Bounded copy from user memory - never trust a user pointer's
         * NUL-termination blindly (same reasoning fs_open()'s own
         * path-string handling already applies elsewhere in this
         * file). */
        const char *src = (const char *)(uintptr_t)title_ptr;
        size_t i = 0;
        for (; i < sizeof(title_buf) - 1; i++) {
            if (!syscall_ptr_ok(title_ptr + i)) break;
            title_buf[i] = src[i];
            if (src[i] == '\0') break;
        }
        title_buf[i] = '\0';
        title_arg = title_buf;
    }

    uint32_t window_id = wm_create_window_userbacked(
        (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, title_arg,
        phys_frames, &frame_count);
    if (window_id == 0) return (uint64_t)-1;
    if (frame_count > SYS_WM_MAX_WINDOW_FRAMES) {
        /* Should be unreachable given the w/h cap above, but if it
         * ever did happen, leaking the already-created window would
         * be worse than failing loudly - destroy what was just
         * created rather than return success with a result the
         * caller's fixed-size array can't actually hold. */
        wm_destroy_window(window_id);
        return (uint64_t)-1;
    }

    wm_window_t *win = wm_get_window(window_id);
    if (!win) { wm_destroy_window(window_id); return (uint64_t)-1; }

    /* Map every physical frame into the CALLING process's own address
     * space, contiguously, starting at this window's reserved virtual
     * slot - see SYS_WM_PIXELS_VADDR_BASE's comment. PAGING_WRITE so
     * the process can actually draw into it; PAGING_USER so ring-3
     * code (which is ALL TRP code, as of this OS's per-process
     * paging/ring-3 milestone) can touch it at all. */
    uint64_t vaddr_base = SYS_WM_PIXELS_VADDR_BASE + (uint64_t)slot * 0x10000000ULL;
    for (uint32_t i = 0; i < frame_count; i++) {
        mm_map_page_in(cur->page_table_root, vaddr_base + (uint64_t)i * 4096,
                        phys_frames[i], PAGING_WRITE | PAGING_USER);
    }
    if ((paddr_t)cur->page_table_root == paging_current()) {
        for (uint32_t i = 0; i < frame_count; i++) {
            paging_invalidate_current(vaddr_base + (uint64_t)i * 4096);
        }
    }

    cur->owned_windows[slot] = window_id;

    syscall_wm_create_result_t result;
    result.window_id = window_id;
    result.pixels_vaddr = vaddr_base;
    result.pitch = win->pitch;
    result.client_w = win->client_w;
    result.client_h = win->client_h;
    memcpy((void *)(uintptr_t)out_result_ptr, &result, sizeof(result));

    return 0;
}

/* Finds which owned_windows[] slot (if any) holds window_id for the
 * CURRENT process - returns the slot index, or -1 if this process
 * doesn't own that window. Shared by destroy/poll below so both
 * enforce the same ownership check the same way (see
 * process.h's owned_windows comment on why this check exists at all -
 * without it, any process could act on any other process's window). */
static int sys_wm_find_owned_slot(process_t *cur, uint32_t window_id) {
    for (int i = 0; i < PROCESS_MAX_OWNED_WINDOWS; i++) {
        if (cur->owned_windows[i] == window_id) return i;
    }
    return -1;
}

static uint64_t sys_wm_destroy_window(uint64_t window_id, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    int slot = sys_wm_find_owned_slot(cur, (uint32_t)window_id);
    if (slot < 0) return (uint64_t)-1; /* not this process's window -
        * refuse, don't silently no-op (a silent no-op could mask a
        * real caller bug - a loud, consistent failure is more useful
        * to debug against) */

    /* FIX: originally this destroyed the window and freed its PMM
     * frames (via wm_destroy_window()) but never unmapped the
     * corresponding pages from the CALLING process's own page table -
     * flagged as a real use-after-free risk while writing it (a
     * process writing to its old, now-dangling vaddr after this call
     * would corrupt whatever the PMM has since handed those freed
     * frames to), then fixed properly here rather than left as a
     * known gap: unmap every page this window's buffer occupied in
     * the owning process's OWN address space BEFORE wm_destroy_window()
     * frees the underlying frames, using the exact frame count
     * wm_get_window() can still report (must read it before
     * destroying, not after). */
    wm_window_t *win = wm_get_window((uint32_t)window_id);
    uint32_t frame_count = win ? win->userbacked_frame_count : 0;
    uint64_t vaddr_base = SYS_WM_PIXELS_VADDR_BASE + (uint64_t)slot * 0x10000000ULL;

    wm_destroy_window((uint32_t)window_id);
    cur->owned_windows[slot] = 0;

    int is_current = ((paddr_t)cur->page_table_root == paging_current());
    for (uint32_t i = 0; i < frame_count; i++) {
        mm_unmap_page_in(cur->page_table_root, vaddr_base + (uint64_t)i * 4096);
        if (is_current) paging_invalidate_current(vaddr_base + (uint64_t)i * 4096);
    }
    return 0;
}

static uint64_t sys_wm_poll_event(uint64_t window_id, uint64_t out_event_ptr, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    if (sys_wm_find_owned_slot(cur, (uint32_t)window_id) < 0) return (uint64_t)-1;
    if (!syscall_buf_ok(out_event_ptr, sizeof(wm_event_t))) return (uint64_t)-1;

    wm_event_t ev;
    if (!wm_poll_event((uint32_t)window_id, &ev)) return 0; /* no event
        * available - 0 return means "no event", NOT "error"; matches
        * wm_poll_event()'s own 0/1 (not found/found) contract rather
        * than inventing a different convention at the syscall layer */

    memcpy((void *)(uintptr_t)out_event_ptr, &ev, sizeof(ev));
    return 1;
}

/* SYS_WM_PRESENT: a no-op today, kept as a real, registered syscall
 * (not omitted) so trplibc's trp_gui_present() has something real to
 * call rather than being a pure client-side stub - a future version
 * of this kernel with explicit double-buffering (drawing into an
 * off-screen buffer, then "presenting" it to the compositor on
 * request, rather than every pixel write being immediately visible)
 * would give this a real body without needing trplibc or any
 * existing caller to change at all. Today, a USERBACKED window's
 * pixel buffer IS the same memory the compositor reads directly
 * (confirmed: wm_create_window_userbacked() backs win->pixels with
 * the exact frames mapped into the process) - so every write is
 * already immediately visible next frame the compositor draws,
 * with nothing to "present". */
static uint64_t sys_wm_present(uint64_t window_id, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    if (sys_wm_find_owned_slot(cur, (uint32_t)window_id) < 0) return (uint64_t)-1;
    return 0;
}

static uint64_t sys_thread_create(uint64_t entry, uint64_t arg, uint64_t stack_size, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    if (!syscall_ptr_ok(entry)) return (uint64_t)-1;
    process_t *cur = process_get_current();
    pid_t tid = process_thread_create(cur, (vaddr_t)entry, (vaddr_t)arg, stack_size);
    return tid < 0 ? (uint64_t)-1 : (uint64_t)tid;
}

static uint64_t sys_thread_exit(uint64_t status, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    /* Same underlying mechanism as process_exit() (this IS a
     * process_t, just one flagged is_thread) - deliberately does NOT
     * go through process_exit() itself, since that calls
     * process_free_memory(), which would release the entire thread
     * group's shared heap/mmap regions out from under any sibling
     * threads still running. A thread exiting only ever gives up its
     * own stack's frames (todo: not yet reclaimed here - see note
     * below) and marks itself terminated for process_thread_join(). */
    process_t *cur = process_get_current();
    if (!cur) return (uint64_t)-1;
    cur->exit_code = (int)status;
    cur->state = PROCESS_TERMINATED;
    /* NOTE: cur->stack_start..stack_end's frames are intentionally
     * still leaked on thread exit today - freeing them requires the
     * same per-frame tracking mmap regions get (see mmap_frame_node_t)
     * which the stack allocation in process_thread_create() doesn't
     * yet build, since the stack is currently freed as a side effect
     * of the *process* (not thread) exit path. Flagging honestly
     * rather than silently pretending this is leak-free: a long-running
     * app that spawns and joins many short-lived threads will exhaust
     * physical memory over time until this is closed. */
    return 0;
}

static uint64_t sys_thread_join(uint64_t tid, uint64_t out_status_ptr, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_ptr_ok_or_null(out_status_ptr)) return (uint64_t)-1;
    int exit_code = 0;
    int result = process_thread_join((pid_t)tid, &exit_code);
    if (result == 0 && out_status_ptr) {
        *(int *)(uintptr_t)out_status_ptr = exit_code;
    }
    return (uint64_t)result;
}

static uint64_t sys_thread_self(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    return cur ? (uint64_t)cur->pid : (uint64_t)-1;
}

static void __attribute__((naked)) syscall_entry_stub(void) {
    __asm__ volatile(
        /* Switch onto a dedicated kernel stack before touching
         * anything. swapgs brings KERNEL_GS_BASE (our percpu struct,
         * programmed in syscall_init) into active GS; g_syscall_kstack's
         * top is 16-byte aligned (the array itself is aligned(16) and
         * its size, 16384, is itself a multiple of 16 - so base+size
         * is 16-aligned too).
         *
         * FIX: the 9 pushes below (72 bytes) followed by 6 pops (48
         * bytes) net to -24 bytes - leaving RSP at (16-aligned - 24),
         * i.e. RSP % 16 == 8, at the point of `call syscall_dispatch`
         * below. The SysV ABI requires RSP % 16 == 0 immediately
         * BEFORE a call (that's what makes RSP % 16 == 8 correct at
         * the *callee's entry*, one instruction later, after call's
         * own 8-byte return-address push - the previous version of
         * this comment had those two the wrong way round). Net
         * result: syscall_dispatch used to be called 8 bytes short of
         * proper alignment, on every single syscall - invisible right
         * up until something reachable from it needs a genuinely
         * 16-byte-aligned stack (e.g. an aligned SSE spill; see
         * kernel/interrupts.c's isr_trampoline for the same bug class
         * and why graphics_3d.c specifically makes that concrete in
         * this codebase). The extra sub/add pair below closes the
         * 8-byte gap without touching anything else - the existing
         * cleanup sequence after the call is unchanged, just shifted
         * to account for it. */
        "swapgs\n"
        "mov %rsp, %gs:8\n"
        "mov %gs:0, %rsp\n"

        "push %rcx\n"
        "push %r11\n"

        "push %r9\n"
        "push %r8\n"
        "push %r10\n"
        "push %rdx\n"
        "push %rsi\n"
        "push %rdi\n"
        "push %rax\n"

        "pop %rdi\n"
        "pop %rsi\n"
        "pop %rdx\n"
        "pop %rcx\n"
        "pop %r8\n"
        "pop %r9\n"

        "sub $8, %rsp\n"             /* close the 8-byte alignment gap */
        "call syscall_dispatch\n"
        "add $16, %rsp\n"            /* undo the sub above, plus the original
                                       * discard of the leftover r9-slot value */

        "pop %r11\n"
        "pop %rcx\n"

        "mov %gs:8, %rsp\n"
        "swapgs\n"
        "sysretq\n"
    );
}


/* ── Install-time program support (partition manager / OOBE) ──────────────
 * ABI in include/install_abi.h. Raw disk access is deliberately NOT
 * exposed: the partition manager works on a validated 4-entry MBR via
 * SYS_PART_READ/WRITE, and SYS_PART_WRITE only works while the live
 * installer is running it. */
#include "install_abi.h"
#include "installer.h"
#include "ata.h"
#include "pmm.h"
#include "vga.h"

static uint64_t sys_disk_info(uint64_t buf, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_buf_ok(buf, sizeof(disk_info_t))) return (uint64_t)-1;
    disk_info_t *out = (disk_info_t *)(uintptr_t)buf;
    memset(out, 0, sizeof(*out));
    uint64_t sec = 0; char model[48];
    out->boot_reserved = INSTALL_PART_ALIGN_LBA;
    out->sector_size   = 512;
    out->install_session = (unsigned)install_session_active();
    if (ata_drive_info(0, &sec, model, sizeof(model)) == 0) {
        out->present = 1;
        out->total_sectors = sec;
        for (int i = 0; i < 47 && model[i]; i++) out->model[i] = model[i];
    }
    return 0;
}

static uint64_t sys_part_read(uint64_t buf, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_buf_ok(buf, 512)) return (uint64_t)-1;
    uint8_t sec[512];
    if (ata_raw_read_sector(0, sec) != 0) return (uint64_t)-1;
    memcpy((void *)(uintptr_t)buf, sec, 512);
    return 0;
}

static uint64_t sys_part_write(uint64_t buf, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_buf_ok(buf, 512)) return (uint64_t)-1;
    return (uint64_t)(int64_t)install_part_commit((const unsigned char *)(uintptr_t)buf);
}

static uint64_t sys_install_ctl(uint64_t cmd, uint64_t arg, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    switch (cmd) {
    case INSTALL_CTL_SELECT_TARGET: return (uint64_t)(int64_t)install_session_select((int)arg);
    case INSTALL_CTL_GET_TARGET:    return (uint64_t)(int64_t)install_session_selected();
    default: return (uint64_t)-1;
    }
}

static uint64_t sys_oobe_submit(uint64_t buf, uint64_t len, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (len == 0 || len > 2000 || !syscall_buf_ok(buf, len)) return (uint64_t)-1;
    return (uint64_t)(int64_t)install_session_oobe_submit((const char *)(uintptr_t)buf, (unsigned)len);
}

static uint64_t sys_console_ctl(uint64_t cmd, uint64_t a, uint64_t b, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    if (cmd == CONSOLE_CTL_CLEAR) { io_clear_screen(); return 0; }
    if (cmd == CONSOLE_CTL_SETCOLOR) {
        if (a > 15 || b > 15) return (uint64_t)-1;
        vga_set_color((vga_color_t)a, (vga_color_t)b);
        return 0;
    }
    return (uint64_t)-1;
}

static uint64_t sys_sysinfo(uint64_t buf, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!syscall_buf_ok(buf, sizeof(sysinfo_t))) return (uint64_t)-1;
    sysinfo_t *o = (sysinfo_t *)(uintptr_t)buf;
    memset(o, 0, sizeof(*o));
    o->ram_kb    = pmm_get_total_ram_kb();
    o->fb_width  = mb_fb_width();
    o->fb_height = mb_fb_height();
    return 0;
}

void syscall_init(void) {
    for (int i = 0; i < NUM_SYSCALLS; i++) g_handlers[i] = 0;
    syscall_register_handler(SYS_WRITE,   sys_write);
    syscall_register_handler(SYS_EXIT,    sys_exit);
    syscall_register_handler(SYS_GETPID,  sys_getpid);
    syscall_register_handler(SYS_GETPPID, sys_getppid);
    syscall_register_handler(SYS_FORK,    sys_fork);
    syscall_register_handler(SYS_EXEC,    sys_exec);
    syscall_register_handler(SYS_WAIT,    sys_wait);
    syscall_register_handler(SYS_KILL,    sys_kill);
    syscall_register_handler(SYS_YIELD,   sys_yield);
    syscall_register_handler(SYS_OPEN,    sys_open);
    syscall_register_handler(SYS_CLOSE,   sys_close);
    syscall_register_handler(SYS_READ,    sys_read);
    syscall_register_handler(SYS_SEEK,    sys_seek);
    syscall_register_handler(SYS_STAT,    sys_stat);
    syscall_register_handler(SYS_MKDIR,   sys_mkdir);
    syscall_register_handler(SYS_UNLINK,  sys_unlink);
    syscall_register_handler(SYS_GETTIMEOFDAY, sys_gettimeofday);
    syscall_register_handler(SYS_RMDIR,   sys_rmdir);
    syscall_register_handler(SYS_GETUID,  sys_getuid);
    syscall_register_handler(SYS_GETGID,  sys_getgid);
    syscall_register_handler(SYS_BRK,     sys_brk);
    syscall_register_handler(SYS_MMAP,    sys_mmap);
    syscall_register_handler(SYS_MUNMAP,  sys_munmap);
    syscall_register_handler(SYS_GETCWD,  sys_getcwd);
    syscall_register_handler(SYS_CHDIR,   sys_chdir);
    syscall_register_handler(SYS_DUP,     sys_dup);
    syscall_register_handler(SYS_DUP2,    sys_dup2);
    syscall_register_handler(SYS_IOCTL,   sys_ioctl);
    syscall_register_handler(SYS_POLL,    sys_poll);
    syscall_register_handler(SYS_DISK_INFO,   sys_disk_info);
    syscall_register_handler(SYS_PART_READ,   sys_part_read);
    syscall_register_handler(SYS_PART_WRITE,  sys_part_write);
    syscall_register_handler(SYS_INSTALL_CTL, sys_install_ctl);
    syscall_register_handler(SYS_OOBE_SUBMIT, sys_oobe_submit);
    syscall_register_handler(SYS_CONSOLE_CTL, sys_console_ctl);
    syscall_register_handler(SYS_SYSINFO,     sys_sysinfo);
    syscall_register_handler(SYS_SIGACTION,   sys_sigaction);
    syscall_register_handler(SYS_SIGPROCMASK, sys_sigprocmask);
    syscall_register_handler(SYS_SIGRAISE,    sys_sigraise);
    syscall_register_handler(SYS_THREAD_CREATE, sys_thread_create);
    syscall_register_handler(SYS_THREAD_EXIT,   sys_thread_exit);
    syscall_register_handler(SYS_THREAD_JOIN,   sys_thread_join);
    syscall_register_handler(SYS_THREAD_SELF,   sys_thread_self);

    /* GUI syscalls - see their handlers' own top comment for what
     * these actually expose and why none of this existed before. */
    syscall_register_handler(SYS_WM_CREATE_WINDOW,  sys_wm_create_window);
    syscall_register_handler(SYS_WM_DESTROY_WINDOW, sys_wm_destroy_window);
    syscall_register_handler(SYS_WM_POLL_EVENT,     sys_wm_poll_event);
    syscall_register_handler(SYS_WM_PRESENT,        sys_wm_present);

    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1);

    uint64_t star = ((uint64_t)0x001B << 48) | ((uint64_t)0x0008 << 32);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry_stub);
    wrmsr(MSR_SFMASK, 0x200);

    /* FIX: point KERNEL_GS_BASE at our percpu struct so the entry stub's
     * `swapgs; mov %gs:0, %rsp` lands on a real kernel stack instead of
     * whatever the user process's RSP was. g_syscall_kstack is a static
     * array, so its end address is fixed at link time - no allocator
     * involved, nothing that can fail here. */
    g_syscall_percpu.kernel_rsp =
        (uint64_t)(uintptr_t)(g_syscall_kstack + sizeof(g_syscall_kstack));
    wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)(uintptr_t)&g_syscall_percpu);

    serial_puts("[SYS] Syscall gate ready: 31 syscalls registered "
                "(process/fs/mmap+brk/fd-dup/ioctl/poll/signals/threads).\n");
}

