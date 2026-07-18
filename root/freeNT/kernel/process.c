#include "process.h"
#include "mm.h"
#include "pmm.h"
#include "string.h"
#include "io.h"
#include "serial.h"     /* FIX: was calling serial_puts/serial_putc with no declaration */
#include "loader.h"     // use the real loader, not load_and_execute_user_elf
#include "interrupts.h" /* interrupt_frame_t - scheduler_yield() now does a
                          * real save/restore context switch using the
                          * on-stack register frame the timer ISR hands it,
                          * instead of only rotating a queue pointer. */

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

// ---------------------------------------------------------------------------
// Process table
// ---------------------------------------------------------------------------
static process_t *process_table[MAX_PROCESSES] = {0};
static pid_t      next_pid          = 1;
static process_t *current_process   = NULL;
static process_t *run_queue_head    = NULL;
static process_t *run_queue_tail    = NULL;

// ---------------------------------------------------------------------------
// process_init
// ---------------------------------------------------------------------------
void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    next_pid        = 1;
    current_process = NULL;
    run_queue_head  = NULL;
    run_queue_tail  = NULL;
}

// ---------------------------------------------------------------------------
// process_create
// ---------------------------------------------------------------------------
process_t *process_create(const char *name, uint8_t priority) {
    (void)name; /* TODO(v2): store process name for ps/debugging; not yet
                 * tracked in process_t, so explicitly mark unused for now
                 * rather than silently ignoring -Wunused-parameter. */
    if (next_pid >= MAX_PROCESSES) return NULL;

    process_t *proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc) return NULL;
    memset(proc, 0, sizeof(process_t));

    /* memset zeroed context.never_run to 0, but a brand-new process
     * needs the opposite: it's never actually run, so its first
     * scheduler_yield() switch-in must synthesize an initial frame
     * from context.rip/rsp rather than trust the (zeroed, meaningless)
     * saved-register fields as if they were a real prior context. */
    proc->context.never_run = 1;

    proc->pid      = next_pid++;
    proc->ppid     = current_process ? current_process->pid : 0;
    proc->state    = PROCESS_CREATED;
    proc->priority = priority;

    proc->page_table_root = mm_alloc_pages(1);

    proc->stack_end   = 0x7FFFFFFF000ULL;
    proc->stack_start = proc->stack_end - 0x100000ULL;
    proc->heap_start  = 0x0000000010000000ULL;
    proc->heap_end    = proc->heap_start; /* brk() starts at the base - nothing
                                            * mapped until the process actually
                                            * calls brk() to grow it, matching
                                            * real brk(2) semantics (the initial
                                            * break is wherever the loader left
                                            * off, not a pre-grown region). */
    proc->heap_max    = proc->heap_start + 0x10000000ULL; /* 256 MiB budget */

    /* FD table: every slot starts unused. Previously this was an
     * untyped `void *fd_table` array that nothing ever wrote entries
     * into (fs_open()'s return value was used directly as a global
     * fd, bypassing any notion of "this process's fd 3"), so dup()/
     * dup2()/close() had no process-local state to operate on at all. */
    for (int i = 0; i < PROCESS_MAX_FDS; i++) proc->proc_fd[i] = PROCESS_FD_UNUSED;
    proc->fd_count = 0;

    proc->cwd[0] = '/';
    proc->cwd[1] = '\0';

    proc->thread_group_pid = proc->pid;
    proc->is_thread        = 0;

    process_table[proc->pid] = proc;

    // Debug print PID
    {
        char tmp[16];
        int  v   = proc->pid;
        int  idx = 0;
        if (v == 0) {
            tmp[idx++] = '0';
        } else {
            char rev[16]; int ri = 0;
            while (v > 0) { rev[ri++] = '0' + v % 10; v /= 10; }
            while (ri > 0) tmp[idx++] = rev[--ri];
        }
        tmp[idx] = '\0';
        serial_puts("[proc] created pid=");
        serial_puts(tmp);
        serial_puts("\n");
    }

    return proc;
}

// ---------------------------------------------------------------------------
// process_get_by_pid / process_get_current
// ---------------------------------------------------------------------------
process_t *process_get_by_pid(pid_t pid) {
    if (pid < 0 || pid >= MAX_PROCESSES) return NULL;
    return process_table[pid];
}

process_t *process_get_current(void) {
    return current_process;
}

// ---------------------------------------------------------------------------
// Per-process file descriptors
// ---------------------------------------------------------------------------
int process_fd_install(process_t *proc, int global_fd) {
    if (!proc || global_fd < 0) return -1;
    for (int i = 0; i < PROCESS_MAX_FDS; i++) {
        if (proc->proc_fd[i] == PROCESS_FD_UNUSED) {
            proc->proc_fd[i] = global_fd;
            proc->fd_count++;
            return i;
        }
    }
    return -1; /* process fd table full */
}

int process_fd_dup(process_t *proc, int proc_fd) {
    if (!proc || proc_fd < 0 || proc_fd >= PROCESS_MAX_FDS) return -1;
    int global_fd = proc->proc_fd[proc_fd];
    if (global_fd == PROCESS_FD_UNUSED) return -1;
    return process_fd_install(proc, global_fd);
}

int process_fd_dup2(process_t *proc, int old_proc_fd, int new_proc_fd) {
    if (!proc) return -1;
    if (old_proc_fd < 0 || old_proc_fd >= PROCESS_MAX_FDS) return -1;
    if (new_proc_fd < 0 || new_proc_fd >= PROCESS_MAX_FDS) return -1;

    int global_fd = proc->proc_fd[old_proc_fd];
    if (global_fd == PROCESS_FD_UNUSED) return -1;

    /* dup2 onto itself is a documented no-op that still "succeeds". */
    if (old_proc_fd == new_proc_fd) return new_proc_fd;

    /* dup2 silently closes whatever was at new_proc_fd first - note
     * this only clears the process-local slot, not the underlying
     * global fd (see process_fd_close()'s comment: the global table
     * is reference-counted elsewhere, other slots/processes may still
     * be using it). */
    if (proc->proc_fd[new_proc_fd] != PROCESS_FD_UNUSED) {
        proc->fd_count--;
    }
    proc->proc_fd[new_proc_fd] = global_fd;
    proc->fd_count++;
    return new_proc_fd;
}

int process_fd_get(process_t *proc, int proc_fd) {
    if (!proc || proc_fd < 0 || proc_fd >= PROCESS_MAX_FDS) return -1;
    return proc->proc_fd[proc_fd];
}

int process_fd_close(process_t *proc, int proc_fd) {
    if (!proc || proc_fd < 0 || proc_fd >= PROCESS_MAX_FDS) return -1;
    if (proc->proc_fd[proc_fd] == PROCESS_FD_UNUSED) return -1;
    proc->proc_fd[proc_fd] = PROCESS_FD_UNUSED;
    proc->fd_count--;
    return 0;
}

// ---------------------------------------------------------------------------
// brk / mmap — see the mmap_region_t / process_t comments in process.h
// for why these look the way they do on a kernel without real per-
// process page-table isolation.
// ---------------------------------------------------------------------------
vaddr_t process_brk(process_t *proc, vaddr_t new_end) {
    if (!proc) return 0;

    if (new_end == 0) return proc->heap_end; /* brk(0) == query current break */

    /* Growing: back the newly-claimed range with real, zeroed physical
     * frames right now. This kernel has no page-fault-driven demand
     * paging (no #PF handler wired to fault in mmap'd/brk'd pages
     * lazily), so "reserve the address range, fault pages in later"
     * isn't an option here - eager backing is the only correct choice
     * given what's actually implemented. Under the flat identity map
     * (see heap.c's heap_grow() comment), a frame's physical address
     * IS its usable virtual address, so no mm_map_page() call is
     * needed beyond what identity mapping already provides. */
    if (new_end > proc->heap_end) {
        if (new_end > proc->heap_max) return proc->heap_end; /* over budget, refuse */

        vaddr_t page = proc->heap_end & ~(vaddr_t)(PAGE_SIZE - 1);
        if (page < proc->heap_end) page += PAGE_SIZE; /* round up to next page */

        vaddr_t target_page = (new_end + PAGE_SIZE - 1) & ~(vaddr_t)(PAGE_SIZE - 1);

        for (vaddr_t va = page; va < target_page; va += PAGE_SIZE) {
            void *frame = pmm_alloc_frame();
            if (!frame) return proc->heap_end; /* out of physical memory, refuse growth */
            /* Zeroed: brk()'d memory is defined to read as zero before
             * the process writes it (matches sbrk/brk on every real
             * Unix - uninitialized-but-nonzero heap memory is a classic
             * source of "works on my machine" bugs for anything ported
             * to this kernel, DOOM included). */
            memset((void *)(uintptr_t)va, 0, PAGE_SIZE);
        }
    } else if (new_end < proc->heap_end) {
        /* Shrinking: free the frames being given back. */
        vaddr_t target_page = (new_end + PAGE_SIZE - 1) & ~(vaddr_t)(PAGE_SIZE - 1);
        vaddr_t cur_top     = (proc->heap_end + PAGE_SIZE - 1) & ~(vaddr_t)(PAGE_SIZE - 1);

        for (vaddr_t va = target_page; va < cur_top; va += PAGE_SIZE) {
            pmm_free_frame((void *)(uintptr_t)va);
        }
    }

    proc->heap_end = new_end;
    return proc->heap_end;
}

vaddr_t process_mmap_anon(process_t *proc, uint64_t length, uint32_t prot, uint32_t flags) {
    if (!proc || length == 0) return 0;

    int slot = -1;
    for (int i = 0; i < PROCESS_MAX_MMAP_REGIONS; i++) {
        if (proc->mmap_regions[i].base == 0) { slot = i; break; }
    }
    if (slot < 0) return 0; /* region table full */

    uint64_t page_count = (length + PAGE_SIZE - 1) / PAGE_SIZE;

    mmap_frame_node_t *head = NULL;
    mmap_frame_node_t *tail = NULL;
    vaddr_t base = 0;

    for (uint64_t i = 0; i < page_count; i++) {
        void *frame = pmm_alloc_frame();
        if (!frame) {
            /* Partial allocation failure: unwind everything acquired
             * so far rather than leaking frames or handing back a
             * region that's smaller than the caller asked for and
             * silently believes is fully backed. */
            mmap_frame_node_t *n = head;
            while (n) {
                pmm_free_frame((void *)(uintptr_t)n->frame);
                mmap_frame_node_t *next = n->next;
                kfree(n);
                n = next;
            }
            return 0;
        }

        memset(frame, 0, PAGE_SIZE);

        if (i == 0) base = (vaddr_t)(uintptr_t)frame;

        mmap_frame_node_t *node = (mmap_frame_node_t *)kmalloc(sizeof(mmap_frame_node_t));
        if (!node) {
            pmm_free_frame(frame);
            mmap_frame_node_t *n = head;
            while (n) {
                pmm_free_frame((void *)(uintptr_t)n->frame);
                mmap_frame_node_t *next = n->next;
                kfree(n);
                n = next;
            }
            return 0;
        }
        node->frame = (paddr_t)(uintptr_t)frame;
        node->next = NULL;
        if (tail) tail->next = node; else head = node;
        tail = node;
    }

    proc->mmap_regions[slot].base   = base;
    proc->mmap_regions[slot].length = page_count * PAGE_SIZE;
    proc->mmap_regions[slot].prot   = prot;
    proc->mmap_regions[slot].flags  = flags;
    proc->mmap_regions[slot].frames = head;

    return base;
}

int process_munmap(process_t *proc, vaddr_t addr, uint64_t length) {
    (void)length; /* no partial-unmap support - see mmap_region_t comment;
                   * a whole region is released by its base address alone. */
    if (!proc || addr == 0) return -1;

    for (int i = 0; i < PROCESS_MAX_MMAP_REGIONS; i++) {
        if (proc->mmap_regions[i].base == addr) {
            mmap_frame_node_t *n = proc->mmap_regions[i].frames;
            while (n) {
                pmm_free_frame((void *)(uintptr_t)n->frame);
                mmap_frame_node_t *next = n->next;
                kfree(n);
                n = next;
            }
            proc->mmap_regions[i].base   = 0;
            proc->mmap_regions[i].length = 0;
            proc->mmap_regions[i].frames = NULL;
            return 0;
        }
    }
    return -1; /* not a region this process owns */
}

// ---------------------------------------------------------------------------
// Signals — see process_signals_t's comment in process.h for the
// cooperative-delivery model this implements.
// ---------------------------------------------------------------------------
void process_signal_raise(process_t *proc, int signum) {
    if (!proc || signum < 0 || signum >= PROCESS_MAX_SIGNALS) return;
    proc->signals.pending_mask |= (1u << signum);
}

void process_signal_set_handler(process_t *proc, int signum, signal_handler_t handler) {
    if (!proc || signum < 0 || signum >= PROCESS_MAX_SIGNALS) return;
    proc->signals.handlers[signum] = handler;
}

void process_signal_block(process_t *proc, int signum, int blocked) {
    if (!proc || signum < 0 || signum >= PROCESS_MAX_SIGNALS) return;
    if (blocked) proc->signals.blocked_mask |= (1u << signum);
    else         proc->signals.blocked_mask &= ~(1u << signum);
}

void process_signal_dispatch(process_t *proc) {
    if (!proc) return;

    uint32_t deliverable = proc->signals.pending_mask & ~proc->signals.blocked_mask;
    if (!deliverable) return;

    /* Lowest-numbered pending, unblocked signal wins - matches the
     * "process one at a time, in a defined order" shape real signal
     * delivery uses, without needing a priority queue for what's
     * expected to be a handful of app-level signals at most. */
    int signum = 0;
    while (signum < PROCESS_MAX_SIGNALS && !(deliverable & (1u << signum))) signum++;
    if (signum >= PROCESS_MAX_SIGNALS) return;

    proc->signals.pending_mask &= ~(1u << signum);

    signal_handler_t handler = proc->signals.handlers[signum];
    if (handler) {
        handler(signum);
    } else {
        /* Default action for every signal today: terminate. Real
         * Unix varies this per-signal (SIGCHLD/SIGWINCH default to
         * "ignore", SIGSTOP defaults to "stop", etc.) - deliberately
         * not modeled yet since nothing in this OS's app layer relies
         * on those distinctions, and "unhandled signal kills you" is
         * a safe, unsurprising default for the signals apps actually
         * raise today (kill/terminate-style requests). */
        proc->exit_code = 128 + signum;
        proc->state = PROCESS_TERMINATED;
    }
}

// ---------------------------------------------------------------------------
// Threads — see process_t's comment on thread_group_pid/is_thread.
// ---------------------------------------------------------------------------
pid_t process_thread_create(process_t *creator, vaddr_t entry, vaddr_t arg, uint64_t stack_size) {
    if (!creator || entry == 0) return -1;
    if (stack_size == 0) stack_size = 0x100000ULL; /* 1 MiB default, matches
                                                      * process_create()'s
                                                      * normal process stack size */

    process_t *saved_current = current_process;
    current_process = creator; /* so the new thread's ppid comes out as
                                 * the creator's pid, same as any other
                                 * child-of-current relationship */
    process_t *th = process_create("thread", creator->priority);
    current_process = saved_current;
    if (!th) return -1;

    /* Give the thread its own stack (real, backed memory - same
     * reasoning as process_brk()/process_mmap_anon() on why this is
     * eager rather than demand-paged), but explicitly point it at the
     * CREATOR's heap/mmap bookkeeping rather than its own - a thread
     * sharing an address space shares the heap, by definition. */
    uint64_t page_count = (stack_size + PAGE_SIZE - 1) / PAGE_SIZE;
    vaddr_t stack_base = 0;
    for (uint64_t i = 0; i < page_count; i++) {
        void *frame = pmm_alloc_frame();
        if (!frame) {
            /* Out of memory mid-stack-allocation: the thread's
             * process_t is still in process_table at this point with
             * a half-built stack - mark it terminated immediately so
             * it's never scheduled rather than leaving a process_t
             * around that would crash the instant it's switched to. */
            th->state = PROCESS_TERMINATED;
            th->exit_code = -1;
            return -1;
        }
        memset(frame, 0, PAGE_SIZE);
        if (i == 0) stack_base = (vaddr_t)(uintptr_t)frame;
    }

    th->stack_start = stack_base;
    th->stack_end   = stack_base + page_count * PAGE_SIZE;
    th->heap_start  = creator->heap_start;
    th->heap_end    = creator->heap_end;
    th->heap_max    = creator->heap_max;

    th->thread_group_pid = creator->thread_group_pid;
    th->is_thread         = 1;

    /* Entry trampoline: rsp at the top of the fresh stack, rip at the
     * thread's start routine. `arg` is delivered via rdi, matching the
     * SysV ABI's first-integer-argument register - a thread start
     * routine declared as `void start(void *arg)` receives it exactly
     * the way a normal C call would place it, no special-casing
     * needed in the thread body itself. */
    th->context.rsp = th->stack_end;
    th->context.rip = entry;
    th->context.rdi = arg;
    th->context.rflags = 0x202;
    th->state = PROCESS_RUNNABLE;

    /* Enqueue onto the normal run queue - a thread is scheduled
     * exactly like any other process_t, see process_t's comment on
     * why that's a reasonable simplification on a kernel with no
     * real per-process address-space isolation to preserve. */
    if (!run_queue_head) {
        run_queue_head = th; run_queue_tail = th;
        th->next = NULL; th->prev = NULL;
    } else {
        run_queue_tail->next = th;
        th->prev = run_queue_tail;
        th->next = NULL;
        run_queue_tail = th;
    }

    return th->pid;
}

int process_thread_join(pid_t thread_pid, int *out_exit_code) {
    process_t *th = process_get_by_pid(thread_pid);
    if (!th || !th->is_thread) return -1;

    /* Cooperative join: yield to the scheduler until the thread
     * reaches PROCESS_TERMINATED. This kernel has no blocking-wait
     * primitive that parks the caller off the run queue entirely
     * (PROCESS_WAITING exists as a state but nothing currently
     * transitions a process into/out of it), so a spin-yield loop is
     * the correct match for what's actually implemented rather than
     * introducing a new blocking mechanism this join is the only user
     * of. Bounded, not infinite: scheduler_yield() needs an interrupt
     * frame it doesn't have outside the timer ISR, so this polls
     * scheduler state via process_get_by_pid() rather than calling
     * scheduler_yield() directly - the timer ISR keeps preempting
     * normally while this loop spins on the caller's own timeslice. */
    while (th->state != PROCESS_TERMINATED && th->state != PROCESS_ZOMBIE) {
        __asm__ volatile("pause");
    }

    if (out_exit_code) *out_exit_code = th->exit_code;
    return 0;
}

// ---------------------------------------------------------------------------
// Working directory
// ---------------------------------------------------------------------------
int process_set_cwd(process_t *proc, const char *path) {
    if (!proc || !path) return -1;
    size_t len = strlen(path);
    if (len >= sizeof(proc->cwd)) return -1;
    memcpy(proc->cwd, path, len + 1);
    return 0;
}

const char *process_get_cwd(process_t *proc) {
    if (!proc) return "/";
    return proc->cwd;
}

// ---------------------------------------------------------------------------
// process_start_inplace — trampoline: set RSP and jump to RIP
// ---------------------------------------------------------------------------
static void process_start_inplace(process_t *proc) {
    if (!proc) return;

    void   *entry = (void *)(uintptr_t)proc->context.rip;
    void   *sp    = (void *)(uintptr_t)proc->context.rsp;

    serial_puts("[proc] launching entry=0x");
    {
        uint64_t e = (uint64_t)(uintptr_t)entry;
        for (int i = 15; i >= 0; --i) {
            int nib = (e >> (i * 4)) & 0xF;
            char c  = (nib < 10) ? ('0' + nib) : ('a' + nib - 10);
            serial_putc(c);
        }
    }
    serial_puts("\n");

    asm volatile(
        "mov %0, %%rsp\n"
        "xor %%rbp, %%rbp\n"
        "jmp *%1\n"
        :
        : "r"(sp), "r"(entry)
        : "memory"
    );
}

// ---------------------------------------------------------------------------
// process_start (public)
// ---------------------------------------------------------------------------
void process_start(pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc) return;
    process_start_inplace(proc);
}

// ---------------------------------------------------------------------------
// process_fork
// ---------------------------------------------------------------------------
pid_t process_fork(void) {
    process_t *parent = current_process;
    if (!parent) return -1;

    process_t *child = process_create("fork", parent->priority);
    if (!child) return -1;

    child->context = parent->context;
    child->state   = PROCESS_RUNNABLE;

    return child->pid;
}

// ---------------------------------------------------------------------------
// process_exec
//
// FIXED: Previously called load_and_execute_user_elf(filename) which passed
// the filename STRING as if it were raw ELF bytes — instant triple fault.
//
// Now calls loader_load_executable() which:
//   - opens the file from the VFS
//   - detects .exe / .trp / .elf by extension
//   - loads it properly into the process address space
//   - sets proc->context.rip to the entry point
// ---------------------------------------------------------------------------
int process_exec(pid_t pid, const char *filename, const char **argv) {
    (void)argv;

    process_t *proc = process_get_by_pid(pid);
    if (!proc || !filename) return -1;

    serial_puts("[proc] exec: ");
    serial_puts(filename);
    serial_puts("\n");

    // Call the real loader — it sets proc->context.rip on success
    int result = loader_load_executable(filename, pid);
    if (result != 0) {
        serial_puts("[proc] loader failed\n");
        return -1;
    }

    // Set up user stack pointer
    proc->context.rsp = proc->stack_end ? proc->stack_end : 0x7FFFFFFF000ULL;
    proc->state       = PROCESS_RUNNABLE;

    // Enqueue
    if (!run_queue_head) {
        run_queue_head  = proc;
        run_queue_tail  = proc;
        proc->next      = NULL;
        proc->prev      = NULL;
    } else {
        run_queue_tail->next = proc;
        proc->prev           = run_queue_tail;
        proc->next           = NULL;
        run_queue_tail       = proc;
    }

    // If we exec'd ourselves, launch immediately
    if (current_process == proc)
        process_start_inplace(proc);

    return 0;
}

// ---------------------------------------------------------------------------
// process_free_memory — releases mmap regions and brk-backed heap pages.
//
// Threads (is_thread == 1) share their creator's heap/mmap bookkeeping
// (see process_thread_create()), so a thread exiting must NOT free
// memory that still belongs to the thread group's other members - only
// a real process (or the last surviving member of a thread group)
// releasing its own distinct allocations should reach this. Called
// from both process_exit() and process_kill() so neither path leaks
// physical frames - previously exit/kill only flipped a state enum
// and left every mmap'd/brk'd frame permanently marked used in the
// PMM bitmap for the lifetime of the kernel.
// ---------------------------------------------------------------------------
static void process_free_memory(process_t *proc) {
    if (!proc || proc->is_thread) return; /* threads don't own the memory they used */

    for (int i = 0; i < PROCESS_MAX_MMAP_REGIONS; i++) {
        if (proc->mmap_regions[i].base != 0) {
            process_munmap(proc, proc->mmap_regions[i].base, proc->mmap_regions[i].length);
        }
    }

    if (proc->heap_end > proc->heap_start) {
        process_brk(proc, proc->heap_start);
    }
}

// ---------------------------------------------------------------------------
// process_wait / process_exit / process_kill
// ---------------------------------------------------------------------------
int process_wait(pid_t pid, int *status) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc) return -1;
    if (status) *status = proc->exit_code;
    return 0;
}

void process_exit(int status) {
    if (!current_process) return;
    current_process->exit_code = status;
    current_process->state     = PROCESS_TERMINATED;
    process_free_memory(current_process);
}

void process_kill(pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc) return;
    proc->state    = PROCESS_TERMINATED;
    proc->exit_code = -1;
    process_free_memory(proc);
}

// ---------------------------------------------------------------------------
// Scheduler
// ---------------------------------------------------------------------------
void scheduler_init(void) {
    run_queue_head = NULL;
    run_queue_tail = NULL;
}

process_t *scheduler_next(void) {
    if (!run_queue_head) return current_process;

    process_t *next = run_queue_head;
    run_queue_head  = next->next;
    if (!run_queue_head)
        run_queue_tail = NULL;
    else
        run_queue_head->prev = NULL;

    // Rotate to back of queue
    if (run_queue_tail) {
        run_queue_tail->next = next;
        next->prev           = run_queue_tail;
    } else {
        run_queue_head = next;
        next->prev     = NULL;
    }
    run_queue_tail = next;
    next->next     = NULL;

    return next;
}

// ---------------------------------------------------------------------------
// FXSAVE / FXRSTOR helpers
//
// These require their operand to be a 16-byte-aligned 512-byte region -
// see cpu_context_fxsave_area() in process.h for why we compute that
// pointer at runtime rather than trusting struct/heap alignment.
// ---------------------------------------------------------------------------
static inline void fxsave_state(cpu_context_t *ctx) {
    uint8_t *area = cpu_context_fxsave_area(ctx);
    __asm__ volatile("fxsave (%0)" :: "r"(area) : "memory");
}

static inline void fxrstor_state(cpu_context_t *ctx) {
    uint8_t *area = cpu_context_fxsave_area(ctx);
    __asm__ volatile("fxrstor (%0)" :: "r"(area) : "memory");
}

// ---------------------------------------------------------------------------
// Frame <-> context copy helpers
//
// interrupt_frame_t (interrupts.h) and cpu_context_t (process.h) are kept
// field-order-parallel on purpose (see the comment on cpu_context_t) so
// this copy is a straightforward one-to-one mapping rather than derived
// offset math - easy to check by eye against both struct definitions.
// ---------------------------------------------------------------------------
static void frame_to_context(const interrupt_frame_t *f, cpu_context_t *c) {
    c->rax = f->rax; c->rbx = f->rbx; c->rcx = f->rcx; c->rdx = f->rdx;
    c->rsi = f->rsi; c->rdi = f->rdi; c->rbp = f->rbp;
    c->r8  = f->r8;  c->r9  = f->r9;  c->r10 = f->r10; c->r11 = f->r11;
    c->r12 = f->r12; c->r13 = f->r13; c->r14 = f->r14; c->r15 = f->r15;
    c->rip     = f->rip;
    c->rflags  = f->rflags;
    c->rsp     = f->rsp;
    /* cr3 is deliberately NOT copied from the frame - the CPU never
     * pushes cr3 as part of an interrupt frame, and process_create()
     * is the sole owner of what a process's page_table_root/cr3
     * should be. Overwriting it here from a frame that never carried
     * a valid value would silently corrupt the process's address
     * space on the very next switch back in. */
}

static void context_to_frame(const cpu_context_t *c, interrupt_frame_t *f) {
    f->rax = c->rax; f->rbx = c->rbx; f->rcx = c->rcx; f->rdx = c->rdx;
    f->rsi = c->rsi; f->rdi = c->rdi; f->rbp = c->rbp;
    f->r8  = c->r8;  f->r9  = c->r9;  f->r10 = c->r10; f->r11 = c->r11;
    f->r12 = c->r12; f->r13 = c->r13; f->r14 = c->r14; f->r15 = c->r15;
    f->rip    = c->rip;
    f->rflags = c->rflags;
    f->rsp    = c->rsp;
    /* f->cs/ss/error_code/interrupt_number are left untouched - they
     * describe the interrupt gate/segment the CPU used to get *into*
     * the trampoline, which is the same for every process in this
     * single-privilege-level kernel today, so the incoming process
     * resumes through the exact same iretq path the outgoing one used. */
}

// ---------------------------------------------------------------------------
// scheduler_yield
//
// REWRITTEN: previously this only rotated run_queue_head/tail and
// reassigned current_process - it never touched a single register, so
// "switching" a process only ever changed which pointer the kernel
// considered current; the actual CPU kept executing whatever
// instruction stream it was already on. Called every timer tick via
// pit_irq_handler() -> scheduler_yield(frame), so that bug meant
// freeNT had never actually preempted anything - it just relabeled it.
//
// Now: takes the interrupt_frame_t* that isr_trampoline built on the
// kernel stack for this tick (every GPR pushed, plus the CPU's own
// iretq frame beneath). If there's a current process, its register
// state is copied out of that frame into its cpu_context_t and its
// FPU/SSE state is saved via fxsave. scheduler_next() then picks the
// process to run. If that's the same process (nothing else runnable,
// or only one process exists), the frame is left untouched and
// isr_trampoline's normal pop+iretq resumes exactly where it was. If
// it's a different process, that process's saved context (or, for a
// never-run process, a synthesized initial frame - see below) is
// written back into the SAME on-stack frame, so isr_trampoline's
// existing pop+iretq sequence transparently resumes the *new*
// process instead - no separate return path needed.
//
// never_run handling: a process that has only ever been through
// process_create() has a cpu_context_t with a valid rip/rsp (set by
// the loader) but no meaningful saved GPR/rflags state, because it
// has never actually been interrupted - it hasn't run yet. Restoring
// its raw (zeroed) register fields as if they were a real saved frame
// would still work functionally (garbage register values a fresh
// process doesn't care about), but rflags=0 would leave interrupts
// disabled forever after switching in, hanging the whole system the
// instant this becomes the running process. So the never_run path
// explicitly sets rflags=0x202 (IF=1, reserved bit 1 set) instead of
// trusting the zeroed field.
// ---------------------------------------------------------------------------
void scheduler_yield(struct interrupt_frame *frame) {
    if (current_process) {
        frame_to_context(frame, &current_process->context);
        fxsave_state(&current_process->context);
        current_process->context.never_run = 0;
    }

    process_t *next = scheduler_next();
    if (!next || next == current_process) {
        /* Nothing to switch to (or only one runnable process) - leave
         * the frame as-is; isr_trampoline resumes the same process. */
        current_process = next;
        return;
    }

    current_process = next;

    if (next->context.never_run) {
        /* First-ever switch-in for this process: synthesize a frame
         * from its entry point/stack rather than trusting a
         * never-populated saved-register context. */
        context_to_frame(&next->context, frame);
        frame->rip    = next->context.rip;
        frame->rsp    = next->context.rsp;
        frame->rflags = 0x202; /* IF=1 - see comment above */
        next->context.never_run = 0;
        /* Fresh process: no meaningful FPU state to restore yet.
         * fxrstor of a zeroed area is well-defined (loads the FPU's
         * power-on-equivalent state) so this is safe, just skip the
         * "restore prior state" framing since there isn't any. */
        fxrstor_state(&next->context);
    } else {
        context_to_frame(&next->context, frame);
        fxrstor_state(&next->context);
    }

    /* Deliver any pending signal right before this process resumes -
     * see process_signals_t's comment in process.h for why "right
     * before the process next runs" is this kernel's delivery point
     * instead of true async-anywhere delivery. Checked after the
     * context is already restored into `frame` so a default-action
     * termination (process_signal_dispatch() calling process_exit(),
     * which itself just flips a state enum) takes effect before any
     * of this process's instructions actually execute again - the
     * process never gets to run even one more instruction between
     * "signal was pending" and "process is terminated". */
    process_signal_dispatch(next);
}
