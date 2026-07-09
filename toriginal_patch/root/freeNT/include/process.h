#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include "types.h"
#include "config.h"

/* Process states */
typedef enum {
    PROCESS_CREATED,
    PROCESS_RUNNING,
    PROCESS_RUNNABLE,
    PROCESS_WAITING,
    PROCESS_STOPPED,
    PROCESS_ZOMBIE,
    PROCESS_TERMINATED
} process_state_t;

/* CPU context (for context switching).
 *
 * Field order/types mirror interrupt_frame_t (see interrupts.h)
 * intentionally, since scheduler_yield() copies field-by-field
 * between the two - keeping them structurally parallel makes that
 * copy easy to verify by eye instead of by offset arithmetic.
 *
 * fxsave_storage / never_run: see below. FXSAVE/FXRSTOR require a
 * 16-byte-aligned 512-byte operand (a #GP fault otherwise) - but
 * cpu_context_t lives inside process_t, which process_create()
 * allocates via kmalloc(), and this kernel's heap (heap.c) does NOT
 * guarantee any particular alignment for arbitrary-sized allocations
 * beyond the first block in a fresh page. A plain
 * `__attribute__((aligned(16)))` on the array only constrains its
 * offset *within* the struct, not the struct's absolute address once
 * heap-allocated - relying on it here would "work" by accident until
 * some unlucky allocation pattern puts the struct at an address whose
 * low 4 bits aren't 0, at which point fxsave/fxrstor fault. Instead,
 * fxsave_storage is deliberately oversized by 16 bytes and
 * fxsave_area() computes the aligned pointer within it at runtime -
 * this is correct regardless of kmalloc's actual alignment. */
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cr3;  /* Page table root */
    uint8_t  fxsave_storage[512 + 16];
    int      never_run;
} cpu_context_t;

/* Returns the 16-byte-aligned 512-byte FXSAVE region within
 * ctx->fxsave_storage. Always safe to call regardless of ctx's own
 * address alignment - see comment above. */
static inline uint8_t *cpu_context_fxsave_area(cpu_context_t *ctx) {
    uintptr_t raw = (uintptr_t)ctx->fxsave_storage;
    uintptr_t aligned = (raw + 15u) & ~(uintptr_t)15u;
    return (uint8_t *)aligned;
}

/* Process structure */
typedef struct process {
    pid_t pid;
    pid_t ppid;  /* Parent PID */
    process_state_t state;
    uint8_t priority;
    
    cpu_context_t context;
    
    /* Memory */
    paddr_t page_table_root;
    vaddr_t heap_start;
    vaddr_t heap_end;
    vaddr_t stack_start;
    vaddr_t stack_end;
    
    /* File descriptors */
    void *fd_table;
    uint32_t fd_count;
    
    /* Process info */
    uint64_t creation_time;
    uint64_t execution_time;
    
    /* Exit status */
    int exit_code;
    
    struct process *next;
    struct process *prev;
} process_t;

/* Process management */
void process_init(void);
process_t* process_create(const char *name, uint8_t priority);
pid_t process_fork(void);
int process_exec(pid_t pid, const char *filename, const char **argv);
int process_wait(pid_t pid, int *status);
void process_exit(int status);
void process_kill(pid_t pid);
process_t* process_get_current(void);
process_t* process_get_by_pid(pid_t pid);
void process_start(pid_t pid);

/* Scheduler */
void scheduler_init(void);
process_t* scheduler_next(void);
/* Forward-declared rather than #include "interrupts.h" here, to avoid
 * a header cycle (interrupts.h doesn't need process.h, but several
 * other headers reachable from process.h's existing includes could
 * eventually chain back). Callers that need the real definition
 * already include interrupts.h directly (pit_irq_handler in
 * interrupts.c does). */
struct interrupt_frame;
void scheduler_yield(struct interrupt_frame *frame);

#endif /* _KERNEL_PROCESS_H */
