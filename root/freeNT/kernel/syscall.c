
#include <stdint.h>
#include "serial.h"
#include "io.h"
#include "syscall.h"
#include "process.h"
#include "fs.h"
#include "rtc.h"

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

static int syscall_ptr_ok(uint64_t ptr) {
    return ptr >= SYSCALL_GUARD_PAGE;
}

static int syscall_ptr_ok_or_null(uint64_t ptr) {
    return ptr == 0 || ptr >= SYSCALL_GUARD_PAGE;
}

static int syscall_buf_ok(uint64_t ptr, uint64_t len) {
    if (ptr < SYSCALL_GUARD_PAGE) return 0;
    if (len > 0 && ptr + len < ptr) return 0; /* overflow */
    return 1;
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
    return (uint64_t)fs_write((fd_t)fd, p, (size_t)len);
}

static uint64_t sys_exit(uint64_t status, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_exit((int)status);

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
    return (uint64_t)fs_open((const char *)(uintptr_t)path, (int)flags, (int)mode);
}

static uint64_t sys_close(uint64_t fd, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return (uint64_t)fs_close((fd_t)fd);
}

static uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    if (!syscall_buf_ok(buf, count)) return (uint64_t)-1;
    return (uint64_t)fs_read((fd_t)fd, (void *)(uintptr_t)buf, (size_t)count);
}

static uint64_t sys_seek(uint64_t fd, uint64_t offset, uint64_t whence, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return (uint64_t)fs_seek((fd_t)fd, (int64_t)offset, (int)whence);
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

static void __attribute__((naked)) syscall_entry_stub(void) {
    __asm__ volatile(
        /* FIX: switch onto a dedicated kernel stack before touching
         * anything. swapgs brings KERNEL_GS_BASE (our percpu struct,
         * programmed in syscall_init) into active GS; g_syscall_kstack's
         * top is 16-byte aligned, and the 9 pushes below (72 bytes) plus
         * 6 pops (48 bytes) leave RSP at exactly the mod-16-== 8
         * alignment the SysV ABI requires right before `call` - same as
         * this code always relied on, just now on a stack we control. */
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

        "call syscall_dispatch\n"
        "add $8, %rsp\n"

        "pop %r11\n"
        "pop %rcx\n"

        "mov %gs:8, %rsp\n"
        "swapgs\n"
        "sysretq\n"
    );
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

    serial_puts("[SYS] Syscall gate ready: write, exit, getpid, getppid, fork, exec, wait, kill, yield, open, close, read, seek, stat, mkdir, unlink, gettimeofday.\n");
}

