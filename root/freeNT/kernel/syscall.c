// ==============================================================================
// SYSCALL.C - System Call Gate
//
// This used to be an intentionally empty stub: the entry point immediately
// executed `sysretq` with no register save/restore and no dispatch, and
// native_syscall_handler() did nothing. That was fine right up until
// something actually needed to make a syscall — which is exactly what the
// trpc-compiled .trp binaries now need, to do anything beyond writing
// straight to VGA memory.
//
// IMPORTANT — this could not be boot-tested. There is no QEMU available in
// the environment this was written in, and no way to install it (network
// egress is restricted to package-index domains that returned 403 for the
// actual .deb files). Every register-marshaling and calling-convention
// decision below was worked out by hand against the AMD64 SYSCALL/SYSRET
// spec and the System V x86-64 calling convention, not verified by running
// it. Boot-test this before trusting it in anything real.
// ==============================================================================

#include <stdint.h>
#include "serial.h"
#include "io.h"
#include "syscall.h"
#include "process.h"

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084

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

void syscall_register_handler(uint32_t syscall_num, syscall_handler_t handler) {
    if (syscall_num < NUM_SYSCALLS) g_handlers[syscall_num] = handler;
}

/* Called from the asm entry stub with the syscall number and up to six
 * arguments, already marshaled into normal SysV calling convention. This
 * is the only place that needs to know how to route a syscall number to
 * a handler — the asm stub itself is purely mechanical. */
uint64_t syscall_dispatch(uint32_t syscall_num, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    if (syscall_num >= NUM_SYSCALLS || !g_handlers[syscall_num]) {
        return (uint64_t)-1; /* unimplemented/out-of-range: -ENOSYS-ish */
    }
    return g_handlers[syscall_num](arg1, arg2, arg3, arg4, arg5, arg6);
}

/* ---- individual syscall handlers ------------------------------------- */

/* write(fd, buf, len) — fd is currently ignored (no real fd table for
 * stdout/stderr yet), everything goes to the console (VGA + serial via
 * io_put_char) and to the serial log. buf is written exactly `len` bytes,
 * NOT assumed null-terminated. Safe to deref buf directly: this kernel
 * has no per-process address space yet, so a process's pointers are
 * already valid kernel-visible addresses — no copy_from_user needed
 * (also means no protection between processes; a real limitation, not
 * something this syscall path tries to paper over). */
static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)fd; (void)a4; (void)a5; (void)a6;
    const char *p = (const char *)(uintptr_t)buf;
    for (uint64_t i = 0; i < len; i++) io_put_char(p[i]);
    return len;
}

static uint64_t sys_exit(uint64_t status, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_exit((int)status);
    /* process_exit() just marks PROCESS_TERMINATED and returns — it does
     * not itself switch context away (see process.c). This handler
     * returns normally like any other; the process keeps running until
     * the next scheduler tick sees PROCESS_TERMINATED and doesn't
     * reschedule it. The userland syscall_exit() wrapper (trpc's
     * trpsys.h) halts in a loop right after making this call, so nothing
     * relies on this returning promptly. */
    return 0;
}

static uint64_t sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *cur = process_get_current();
    return cur ? (uint64_t)cur->pid : (uint64_t)-1;
}

/* ---- entry stub -------------------------------------------------------
 *
 * On `syscall`: rax=number, args in rdi,rsi,rdx,r10,r8,r9 (Linux-style
 * convention — r10 instead of rcx for the 4th arg, because the CPU
 * itself clobbers rcx (return RIP) and r11 (saved RFLAGS)). CS/SS are
 * already the kernel selectors per STAR; RSP is untouched (SYSCALL does
 * NOT switch stacks), so it's safe to push/pop on the caller's own
 * stack here.
 *
 * rcx and r11 are saved FIRST, before anything else touches them, and
 * restored LAST, right before sysretq — they hold the only way back to
 * the caller and must survive the whole dispatch call intact.
 *
 * The middle section reshuffles the Linux-style syscall args into
 * System V C calling convention for calling syscall_dispatch(num, a1..a6):
 * rdi=num, rsi=a1, rdx=a2, rcx=a3, r8=a4, r9=a5, [stack]=a6. Pushing
 * everything then popping it back out in order does the reshuffle
 * without needing scratch registers, and conveniently leaves arg6
 * sitting exactly where the call convention expects a 7th integer
 * argument: at 0(%rsp) at the moment of `call`.
 */
static void __attribute__((naked)) syscall_entry_stub(void) {
    __asm__ volatile(
        "push %rcx\n"       /* save return RIP */
        "push %r11\n"       /* save saved RFLAGS */

        "push %r9\n"        /* arg6 */
        "push %r8\n"        /* arg5 */
        "push %r10\n"       /* arg4 */
        "push %rdx\n"       /* arg3 */
        "push %rsi\n"       /* arg2 */
        "push %rdi\n"       /* arg1 */
        "push %rax\n"       /* syscall number */

        "pop %rdi\n"        /* rdi = number   */
        "pop %rsi\n"        /* rsi = arg1     */
        "pop %rdx\n"        /* rdx = arg2     */
        "pop %rcx\n"        /* rcx = arg3     (original rcx already saved above) */
        "pop %r8\n"         /* r8  = arg4     */
        "pop %r9\n"         /* r9  = arg5     */
        /* arg6 is still on the stack at 0(%rsp) — exactly the 7th SysV
         * integer argument slot the call below expects. */

        "call syscall_dispatch\n"
        "add $8, %rsp\n"    /* drop the arg6 stack slot */

        "pop %r11\n"        /* restore saved RFLAGS */
        "pop %rcx\n"        /* restore return RIP */
        "sysretq\n"
    );
}

void syscall_init(void) {
    for (int i = 0; i < NUM_SYSCALLS; i++) g_handlers[i] = 0;
    syscall_register_handler(SYS_WRITE,  sys_write);
    syscall_register_handler(SYS_EXIT,   sys_exit);
    syscall_register_handler(SYS_GETPID, sys_getpid);

    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1);

    /* STAR[47:32]=0x08 -> SYSCALL entry CS=0x08,SS=0x10 (kernel, GDT
     * indices 1,2). STAR[63:48]=0x1B -> SYSRET CS=0x2B,SS=0x23 (ring-3,
     * GDT indices 5,4 — see the FIX 6 comment added to boot64.s's GDT;
     * without those two entries this MSR value makes the first sysretq
     * fault on a nonexistent selector). */
    uint64_t star = ((uint64_t)0x001B << 48) | ((uint64_t)0x0008 << 32);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry_stub);
    wrmsr(MSR_SFMASK, 0x200); /* clear IF on entry: dispatch runs with interrupts off */

    serial_puts("[SYS] Syscall gate ready: write, exit, getpid.\n");
}
