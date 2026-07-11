#include "process.h"
#include "mm.h"
#include "string.h"
#include "io.h"
#include "serial.h"     /* FIX: was calling serial_puts/serial_putc with no declaration */
#include "loader.h"     // use the real loader, not load_and_execute_user_elf
#include "interrupts.h" /* interrupt_frame_t - scheduler_yield() now does a
                          * real save/restore context switch using the
                          * on-stack register frame the timer ISR hands it,
                          * instead of only rotating a queue pointer. */

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
    proc->heap_end    = proc->heap_start + 0x10000000ULL;

    proc->fd_table  = kmalloc(sizeof(void *) * MAX_FD_PER_PROCESS);
    if (!proc->fd_table) {
        kfree(proc);
        return NULL;
    }
    proc->fd_count  = 0;

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
}

void process_kill(pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc) return;
    proc->state    = PROCESS_TERMINATED;
    proc->exit_code = -1;
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
}
