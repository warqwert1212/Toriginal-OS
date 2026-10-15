#ifndef _KERNEL_TSS_H
#define _KERNEL_TSS_H

#include "types.h"

/* Builds the one static TSS, patches its 16-byte descriptor into
 * gdt64[6]/gdt64[7] (see boot64.s's gdt64 comment), and loads it with
 * ltr. Must run after long_mode_start's segment reload and before the
 * first ring-3 entry (interrupts_init() also has no ordering
 * requirement relative to this - either order is fine, but this must
 * come before scheduler_yield() ever actually drops into ring 3). */
void tss_init(void);

/* Exposed for completeness/future use (e.g. if this kernel ever grows
 * SMP or nested-interrupt support, where a single shared rsp0 stops
 * being safe and each core/process would need its own). NOT currently
 * called anywhere: this kernel is single-core with non-reentrant
 * interrupt handling (isr_trampoline runs to completion before
 * anything else can enter ring 0 again), so the one shared
 * g_kernel_stack tss_init() already sets up is safe for every ring-3
 * -> ring-0 transition regardless of which process triggered it -
 * exactly the same reasoning syscall.c's single shared g_syscall_kstack
 * already relies on for the SYSCALL path. */
void tss_set_rsp0(uint64_t rsp0);

#endif /* _KERNEL_TSS_H */
