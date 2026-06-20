#include "process.h"
#include "mm.h"
#include "string.h"
#include "io.h"
#include "serial.h"     /* FIX: was calling serial_puts/serial_putc with no declaration */
#include "loader.h"     // use the real loader, not load_and_execute_user_elf

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

void scheduler_yield(void) {
    process_t *next = scheduler_next();
    if (next) current_process = next;
}
