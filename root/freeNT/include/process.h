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

/* ── Per-process file descriptors ──────────────────────────────────────
 * The underlying filesystem (trpfs.c) has exactly one GLOBAL open-file
 * table - fs_open() returns an index into that single array, shared by
 * every process in the system. That's fine for a single-tasking shell,
 * but it means two processes can never independently close "their"
 * copy of fd 3 without stepping on each other, and dup()/dup2() (POSIX:
 * two process-local fd numbers referring to the same underlying file)
 * has nothing process-local to dup INTO.
 *
 * This table is that process-local layer: proc_fd[i] is either -1
 * (slot unused) or the underlying global fd_t that fs_* calls actually
 * operate on. Two entries can point at the same global fd (that's what
 * dup/dup2 do), and fds 0/1/2 are reserved as stdin/stdout/stderr by
 * convention (unused by the fs layer today - io_put_char/serial handle
 * console I/O directly - but reserved so a future console-as-fd change
 * doesn't collide with already-assigned low numbers). */
#define PROCESS_MAX_FDS       64
#define PROCESS_FD_UNUSED     (-1)

/* ── mmap regions ───────────────────────────────────────────────────────
 * This kernel has no per-process page-table isolation yet (single flat
 * identity-mapped physical range - see syscall.c's syscall_ptr_ok()
 * comment and memory.c's mm_enable_paging() never being called), so
 * "mmap" here can't mean "isolated virtual address space" the way it
 * would on a kernel with real paging. What it CAN honestly mean: hand
 * back a chunk of real, zeroed physical memory (via the PMM, the same
 * frame allocator the kernel heap itself grows from) at a process-
 * chosen-or-kernel-chosen address, track it so it can be unmapped and
 * the frames reclaimed, and refuse to silently double-hand-out the same
 * range to two processes. That's a real, useful primitive for apps
 * that just want "give me N more usable bytes at a stable address"
 * (which is what mmap(MAP_ANONYMOUS) reduces to for the vast majority
 * of real-world callers - big anonymous allocations, not the file-
 * backed-with-COW case) even without full isolation. */
#define PROCESS_MAX_MMAP_REGIONS  32

typedef struct mmap_frame_node {
    paddr_t                  frame;
    struct mmap_frame_node  *next;
} mmap_frame_node_t;

typedef struct {
    vaddr_t  base;       /* 0 if this slot is unused */
    uint64_t length;     /* bytes, page-rounded */
    uint32_t prot;       /* PROT_* flags, informational only today   */
    uint32_t flags;      /* MAP_* flags, informational only today    */
    mmap_frame_node_t *frames; /* the actual PMM frames backing this region -
                                 * tracked individually (not assumed
                                 * contiguous) because pmm_alloc_frame()'s
                                 * first-fit bitmap search makes no
                                 * contiguity guarantee once memory is
                                 * fragmented; munmap must free exactly
                                 * what was allocated. */
} mmap_region_t;

/* ── Signals ────────────────────────────────────────────────────────────
 * Minimal, cooperative signal model: no preemptive delivery mid-
 * instruction (this kernel has no user/kernel ring separation to trap
 * into safely for that yet) - instead, pending signals are checked and
 * dispatched at well-defined points the process is already re-entering
 * the scheduler from (scheduler_yield(), syscall return). That's
 * exactly the same shape as classic Unix signal delivery ("delivered
 * before the process next runs user code"), just without the async-
 * interrupt-anywhere case. Good enough for SIGKILL/SIGTERM/SIGUSR-style
 * app-level signaling (DOOM-class apps wanting e.g. a "pause" signal,
 * a window manager sending "close" to an app) without needing a much
 * larger signal-safe-reentrancy project first. */
#define PROCESS_MAX_SIGNALS   32

typedef void (*signal_handler_t)(int signum);

typedef struct {
    uint32_t          pending_mask;                 /* bit i set = signal i pending */
    uint32_t          blocked_mask;                 /* bit i set = signal i blocked (masked) */
    signal_handler_t  handlers[PROCESS_MAX_SIGNALS]; /* NULL = default action */
} process_signals_t;

/* Process structure */
typedef struct process {
    pid_t pid;
    pid_t ppid;  /* Parent PID */
    process_state_t state;
    uint8_t priority;
    
    cpu_context_t context;
    
    /* Memory */
    paddr_t page_table_root;
    vaddr_t heap_start;    /* brk() base - fixed at process_create()  */
    vaddr_t heap_end;      /* brk() current break                    */
    vaddr_t heap_max;      /* brk() ceiling - heap_start + fixed budget */
    vaddr_t stack_start;
    vaddr_t stack_end;

    mmap_region_t mmap_regions[PROCESS_MAX_MMAP_REGIONS];
    
    /* File descriptors — see PROCESS_MAX_FDS comment above. */
    int      proc_fd[PROCESS_MAX_FDS];
    uint32_t fd_count;   /* number of currently-open (non -1) slots */

    /* Working directory, process-local (POSIX chdir()/getcwd()) —
     * previously only the shell had a notion of cwd (g_cwd in
     * sys/shell/shell.c); a real process-level chdir needs its own
     * copy so two processes can be in different directories. */
    char cwd[256];

    /* Signals — see process_signals_t comment above. */
    process_signals_t signals;

    /* ── Lightweight threads ("thread-ish primitives") ───────────────
     * Real preemptive kernel threads would need per-thread kernel
     * stacks distinct from their process's, plus scheduler awareness
     * of "these N process_t's share one address space" for anything
     * that currently assumes 1 process_t == 1 address space (there
     * isn't one today - see mmap comment - so this is less of a
     * stretch here than on a kernel with real per-process paging).
     * A "thread" is implemented as a genuine second process_t
     * (its own context, its own stack region) explicitly marked as
     * sharing the creator's heap/mmap bookkeeping and grouped by
     * thread_group_pid, rather than a separate light-weight-process
     * concept the scheduler would need to special-case. */
    pid_t thread_group_pid;   /* pid of the "main" thread; == pid for a real process */
    int   is_thread;          /* 1 if this process_t is a thread, not a distinct process */

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

/* ── Per-process file descriptors ─────────────────────────────────────
 * process_fd_install() finds a free slot and points it at an already-
 * open global fd (the common case: sys_open() opens via fs_open() then
 * installs the result). process_fd_dup()/process_fd_dup2() implement
 * dup()/dup2() semantics: a NEW process-local slot referring to the
 * SAME underlying global fd (not a fresh fs_open()). process_fd_get()
 * resolves a process-local fd to the global fd_t that fs_* calls need;
 * returns -1 if the slot is unused. process_fd_close() clears a slot
 * (does NOT close the underlying global fd if other process-local
 * slots, in this or another process, still reference it - reference
 * counting for the shared global table lives in sysmem.c). */
int process_fd_install(process_t *proc, int global_fd);
int process_fd_dup(process_t *proc, int proc_fd);
int process_fd_dup2(process_t *proc, int old_proc_fd, int new_proc_fd);
int process_fd_get(process_t *proc, int proc_fd);
int process_fd_close(process_t *proc, int proc_fd);

/* ── brk / mmap ────────────────────────────────────────────────────────
 * process_brk() implements the classic brk(2): grow/shrink the heap to
 * end at new_end, returning the new break on success or the CURRENT
 * break (unchanged) on failure - exactly like the real syscall's
 * "returns the new/updated break" contract, letting a caller `brk(0)`
 * to just query the current value.
 *
 * process_mmap_anon() reserves `length` bytes of real, zeroed physical
 * memory and records it in the process's mmap_regions table, returning
 * the base virtual address (== physical address under this kernel's
 * flat identity map) or 0 on failure. process_munmap() releases a
 * region previously returned by process_mmap_anon() (must match an
 * existing region's base exactly - no partial-unmap support, matching
 * the scope of what real-world DOOM/SDL-class ports actually need:
 * one big anonymous region, released as a whole at exit). */
vaddr_t process_brk(process_t *proc, vaddr_t new_end);
vaddr_t process_mmap_anon(process_t *proc, uint64_t length, uint32_t prot, uint32_t flags);
int     process_munmap(process_t *proc, vaddr_t addr, uint64_t length);

/* ── Signals ───────────────────────────────────────────────────────────
 * process_signal_raise() marks `signum` pending on `proc` (this is
 * what sys_kill()/a future SYS_TKILL would call - "deliver this signal
 * to that process"). process_signal_set_handler() installs a handler
 * (NULL = restore default action, which for every signal today is
 * "terminate the process" - there's no SIG_IGN-equivalent distinct
 * from "no handler installed" yet, matching how few signals a v1 app
 * layer actually needs to customize). process_signal_dispatch() is
 * called from scheduler_yield() right before a process resumes: it
 * checks pending_mask against blocked_mask, and for the lowest-
 * numbered unblocked pending signal either calls the installed handler
 * (on the process's own stack, as a normal function call - not a real
 * signal-stack-frame-in-userspace mechanism, see process_signals_t's
 * comment on why this is cooperative-delivery rather than fully
 * POSIX-accurate) or applies the default action (process_exit()) if
 * no handler is installed. */
void process_signal_raise(process_t *proc, int signum);
void process_signal_set_handler(process_t *proc, int signum, signal_handler_t handler);
void process_signal_block(process_t *proc, int signum, int blocked);
void process_signal_dispatch(process_t *proc);

/* ── Threads ───────────────────────────────────────────────────────────
 * process_thread_create() spawns a new process_t that shares the
 * creator's mmap_regions/heap bookkeeping (thread_group_pid ==
 * creator's pid, is_thread == 1) with its own stack region and its
 * own cpu_context_t (own RIP/RSP - entry point + arg, like
 * pthread_create's (start_routine, arg)), scheduled by the normal
 * scheduler like any other process_t. process_thread_join() blocks
 * (cooperatively yields) until the target thread has exited, then
 * returns its exit code - see process_wait() for the same shape at
 * the process level. */
pid_t process_thread_create(process_t *creator, vaddr_t entry, vaddr_t arg, uint64_t stack_size);
int   process_thread_join(pid_t thread_pid, int *out_exit_code);

/* getcwd/chdir operate on proc->cwd - see the field comment in
 * process_t. Returns -1 if the new path wouldn't fit in proc->cwd. */
int process_set_cwd(process_t *proc, const char *path);
const char *process_get_cwd(process_t *proc);

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
