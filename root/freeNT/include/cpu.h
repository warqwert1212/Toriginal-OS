#ifndef _KERNEL_CPU_H
#define _KERNEL_CPU_H

/* cpu.c — CPUID-derived CPU identification, for the shell's `hardware`
 * command. Real CPUID leaves, work identically on physical silicon
 * and any hypervisor that passes CPUID through (all of them do, by
 * spec - a VM reporting an emulated brand string is still reporting
 * the actual string that CPUID leaves 0x80000002-0x80000004 return on
 * whatever underlying hardware it's ultimately running on). */

/* Writes a NUL-terminated CPU brand string into out (at least 49
 * bytes) - e.g. "AMD Ryzen 9 5900X 12-Core Processor". Falls back to
 * a plain message if the CPU doesn't support the extended CPUID
 * leaves (leaf 0x80000000 < 0x80000004) - every x86-64 chip made this
 * century does, but older/exotic virtual CPUs might not. */
void cpu_get_brand_string(char out[49]);

#endif /* _KERNEL_CPU_H */
