// ==============================================================================
// SYSCALL.C - High Performance Hardware System Call Controller Matrix
// ==============================================================================
#include <stdint.h>

#define MSR_EFER       0xC0000080
#define MSR_STAR       0xC0000081
#define MSR_LSTAR      0xC0000082
#define MSR_SFMASK     0xC0000084

extern void syscall_stub(void); // Native assembly receiver point

// Linked directly to your serial output function found in io.o
extern void serial_puts(const char* str); 

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = val & 0xFFFFFFFF;
    uint32_t high = (val >> 32) & 0xFFFFFFFF;
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// Global System Call C Handler Linkage Matrix
void native_syscall_handler(uint64_t call_id, uint64_t arg1, uint64_t arg2) {
    if (call_id == 1) { // Syscall 1: print_string_user
        serial_puts((const char*)arg1);
    }
    (void)arg2;
}

// Renamed to match kernel_init's external reference tracking
void syscall_init(void) {
    // 1. Enable System Call Extensions inside Extended Feature Enable Register (EFER)
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | 1); // Turn on bit 0 (SCE - System Call Enable)

    // 2. Configure target base segment components inside STAR register
    uint64_t star = ((uint64_t)0x001B << 48) | ((uint64_t)0x0008 << 32);
    wrmsr(MSR_STAR, star);

    // 3. Register the direct low-level assembly instruction entry address point
    wrmsr(MSR_LSTAR, (uint64_t)syscall_stub);

    // 4. Configure entry flag masks to auto-disable interrupts during execution entries
    wrmsr(MSR_SFMASK, 0x200); // Mask IF flag (Interrupt Flag)
}
