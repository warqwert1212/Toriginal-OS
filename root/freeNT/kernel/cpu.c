// ==============================================================================
// CPU.C - CPUID-derived CPU identification
// ==============================================================================

#include "cpu.h"
#include <stdint.h>
#include <stddef.h>

static inline void do_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                             uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf));
}

void cpu_get_brand_string(char out[49]) {
    uint32_t eax, ebx, ecx, edx;

    do_cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000004) {
        const char *fallback = "Unknown CPU (CPUID brand string unsupported)";
        size_t i = 0;
        for (; fallback[i] && i < 48; i++) out[i] = fallback[i];
        out[i] = '\0';
        return;
    }

    /* Leaves 0x80000002-0x80000004 each return 16 raw ASCII bytes in
     * eax:ebx:ecx:edx, concatenating to the full 48-byte brand string
     * - standard, documented Intel/AMD CPUID behavior. */
    uint32_t *words = (uint32_t *)(void *)out;
    do_cpuid(0x80000002, &words[0], &words[1], &words[2], &words[3]);
    do_cpuid(0x80000003, &words[4], &words[5], &words[6], &words[7]);
    do_cpuid(0x80000004, &words[8], &words[9], &words[10], &words[11]);
    out[48] = '\0';

    /* Brand strings are commonly left-padded with spaces
     * ("  Intel(R) Core(TM)..."); shift left past them so callers get
     * a clean string with no cosmetic leading gap. */
    size_t start = 0;
    while (out[start] == ' ' && start < 48) start++;
    if (start > 0) {
        size_t i = 0;
        while (out[start + i]) { out[i] = out[start + i]; i++; }
        out[i] = '\0';
    }
}
