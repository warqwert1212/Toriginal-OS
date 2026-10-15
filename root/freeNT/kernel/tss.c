#include "tss.h"
#include "string.h"
#include "serial.h"

#define TSS_SELECTOR 0x30

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} tss_t;

/* Kernel stack the CPU switches to on any ring-3 -> ring-0 transition
 * (interrupt, exception, or a fault) BEFORE it pushes ss/rsp/rflags/
 * cs/rip. Static rather than heap-allocated: needed before the heap
 * necessarily has come up in every possible boot ordering, and one
 * kernel-entry stack per CPU is exactly what a single-core kernel
 * needs (see g_syscall_kstack in syscall.c for the SYSCALL path's own
 * copy of this same idea - this is the equivalent for the general
 * interrupt/exception path via isr_trampoline). 32 KiB: generous vs.
 * the ~KiB isr_trampoline itself needs, with real room for whatever
 * handler it calls into (scheduler_yield, page fault handling, etc.)
 * to nest a few calls deep without any risk of walking off the end. */
static uint8_t g_kernel_stack[32768] __attribute__((aligned(16)));

static tss_t g_tss;

extern uint64_t gdt64[];

void tss_init(void)
{
    memset(&g_tss, 0, sizeof(g_tss));
    g_tss.rsp0 = (uint64_t)(uintptr_t)(g_kernel_stack + sizeof(g_kernel_stack));
    g_tss.iomap_base = sizeof(tss_t); /* no I/O bitmap - value >= TSS limit means "none" */

    uint64_t base  = (uint64_t)(uintptr_t)&g_tss;
    uint32_t limit = (uint32_t)sizeof(tss_t) - 1;

    /* 64-bit TSS descriptor layout (System Descriptor, type 0x9 =
     * available 64-bit TSS). Low quad: limit[15:0] | base[23:0] << 16
     * | type/flags | limit[19:16] | base[31:24] << 56. High quad:
     * base[63:32] in the low 32 bits, rest reserved/zero. */
    uint64_t low =
          (limit & 0xFFFFull)
        | ((base & 0xFFFFFFull) << 16)
        | (0x89ull << 40)                       /* P=1, DPL=0, type=0x9 (64-bit TSS, available) */
        | (((uint64_t)(limit >> 16) & 0xFull) << 48)
        | (((base >> 24) & 0xFFull) << 56);
    uint64_t high = (base >> 32) & 0xFFFFFFFFull;

    gdt64[6] = low;
    gdt64[7] = high;

    __asm__ volatile("ltr %%ax" :: "a"((uint16_t)TSS_SELECTOR));

    serial_puts("[TSS] Loaded - rsp0=");
    /* quick hex dump of rsp0, no snprintf available at this layer */
    {
        char buf[19]; buf[0]='0'; buf[1]='x';
        for (int i = 0; i < 16; i++) {
            uint8_t nib = (uint8_t)((g_tss.rsp0 >> ((15 - i) * 4)) & 0xF);
            buf[2 + i] = (char)(nib < 10 ? ('0' + nib) : ('a' + nib - 10));
        }
        buf[18] = '\0';
        serial_puts(buf);
    }
    serial_puts("\n");
}

void tss_set_rsp0(uint64_t rsp0)
{
    g_tss.rsp0 = rsp0;
}
