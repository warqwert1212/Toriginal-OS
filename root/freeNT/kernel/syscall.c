// ==============================================================================
// SYSCALL.C - System Call look i dont care.
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

/* idk what to say but this is a dissapointment */
uint64_t syscall_dispatch(uint32_t syscall_num, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    if (syscall_num >= NUM_SYSCALLS || !g_handlers[syscall_num]) {
        return (uint64_t)-1; /* unimplemented/out-of-range: -ENOSYS-ish */
    }
    return g_handlers[syscall_num](arg1, arg2, arg3, arg4, arg5, arg6);
}


static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)fd; (void)a4; (void)a5; (void)a6;
    const char *p = (const char *)(uintptr_t)buf;
    for (uint64_t i = 0; i < len; i++) io_put_char(p[i]);
    return len;
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

//what could one person do to make this better, i dont know, but i think this is a good start. PLEASE SOMEONE PLEAS PUSH A FIX TO THER FUCKING KEYBOARD.
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
        "pop %rcx\n"        /* rcx = arg3     */
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


    uint64_t star = ((uint64_t)0x001B << 48) | ((uint64_t)0x0008 << 32);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry_stub);
    wrmsr(MSR_SFMASK, 0x200); 

    serial_puts("[SYS] Syscall gate ready: write, exit, getpid.\n");
}
