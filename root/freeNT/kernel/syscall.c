// ==============================================================================
// SYSCALL.C - System Call Gate (safe stub for 1.0)
// FIXED: No dependency on GS-base MSR setup from drivers/main.c
// ==============================================================================

#include <stdint.h>
#include "serial.h"

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

static void __attribute__((naked)) syscall_entry_stub(void) {
    __asm__ volatile("sysretq\n":::);
}

void native_syscall_handler(uint64_t call_id, uint64_t arg1, uint64_t arg2) {
    (void)call_id; (void)arg1; (void)arg2;
}

void syscall_init(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1);

    uint64_t star = ((uint64_t)0x001B << 48) | ((uint64_t)0x0008 << 32);
    wrmsr(MSR_STAR, star);

    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry_stub);
    wrmsr(MSR_SFMASK, 0x200);

    serial_puts("[SYS] Syscall gate ready (stub).\n");
}
