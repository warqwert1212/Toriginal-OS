#include "process.h"
#include "mm.h"
#include "string.h"
#include "io.h"

/* Process table */
static process_t *process_table[MAX_PROCESSES] = {0};
static pid_t next_pid = 1;
static process_t *current_process = NULL;
static process_t *run_queue_head = NULL;
static process_t *run_queue_tail = NULL;

/* Initialize process management */
void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    next_pid = 1;
    current_process = NULL;
    run_queue_head = NULL;
    run_queue_tail = NULL;
}

/* Create a new process */
process_t* process_create(const char *name, uint8_t priority) {
    if (next_pid >= MAX_PROCESSES)
        return NULL;
    
    process_t *proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc)
        return NULL;
    
    memset(proc, 0, sizeof(process_t));
    
    proc->pid = next_pid++;
    proc->ppid = (current_process) ? current_process->pid : 0;
    proc->state = PROCESS_CREATED;
    proc->priority = priority;
    proc->creation_time = 0;  /* TODO: get current time */
    proc->execution_time = 0;
    proc->exit_code = 0;
    
    /* Allocate address space */
    paddr_t page_table = mm_alloc_pages(1);
    proc->page_table_root = page_table;
    
    /* Set up stack and heap */
    proc->stack_end = 0x7FFFFFFF000UL;
    proc->stack_start = proc->stack_end - 0x100000;  /* 1MB stack */
    proc->heap_start = 0x0000000010000000UL;
    proc->heap_end = proc->heap_start + 0x10000000;  /* 256MB heap */
    
    /* Initialize file descriptor table */
    proc->fd_table = kmalloc(sizeof(void *) * MAX_FD_PER_PROCESS);
    proc->fd_count = 0;
    
    /* Add to process table */
    process_table[proc->pid] = proc;
    
    /* Debug: print created PID */
    {
        char decbuf[16];
        int idx = 0;
        int v = proc->pid;
        if (v == 0) decbuf[idx++] = '0';
        else {
            char tmp[16]; int ti = 0;
            while (v > 0 && ti < (int)sizeof(tmp)) {
                tmp[ti++] = '0' + (v % 10);
                v /= 10;
            }
            while (ti-- > 0) decbuf[idx++] = tmp[ti];
        }
        decbuf[idx] = '\0';
        serial_puts("[proc] created pid=");
        serial_puts(decbuf);
        serial_puts("\n");
    }

    return proc;
}

/* Get process by PID */
process_t* process_get_by_pid(pid_t pid) {
    if (pid < 0 || pid >= MAX_PROCESSES)
        return NULL;
    return process_table[pid];
}

/* Get current process */
process_t* process_get_current(void) {
    return current_process;
}

/* Forward declaration for in-place start helper */
static void process_start_inplace(process_t *proc);

/* Public API: start a loaded process by PID (jumps to its entry point) */
void process_start(pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc) return;
    process_start_inplace(proc);
}

/* Fork a process */
pid_t process_fork(void) {
    process_t *parent = current_process;
    if (!parent)
        return -1;
    
    process_t *child = process_create("fork", parent->priority);
    if (!child)
        return -1;
    
    /* Copy parent's context */
    child->context = parent->context;
    
    /* Copy parent's memory space */
    /* TODO: implement copy-on-write */
    
    child->state = PROCESS_RUNNABLE;
    
    return child->pid;
}

/* Execute a program in a process */
int process_exec(pid_t pid, const char *filename, const char **argv) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;
    /* Use loader subsystem to load the executable into this process. */
    extern int loader_load_executable(const char *filename, pid_t pid);

    if (!filename)
        return -1;

    int r = loader_load_executable(filename, pid);
    if (r != 0) {
        serial_puts("[proc] loader returned error\n");
        return -1;
    } else {
        serial_puts("[proc] loader returned OK\n");
    }

    /* Ensure the process has a valid user stack pointer and set state runnable */
    if (proc->stack_end)
        proc->context.rsp = proc->stack_end;
    else
        proc->context.rsp = 0x7FFFFFFF000UL; /* fallback */

    /* If loader set an entry point in context.rip, that'll be used. Otherwise, leave as-is. */

    proc->state = PROCESS_RUNNABLE;

    /* Enqueue into run queue if not already present */
    if (!run_queue_head) {
        run_queue_head = proc;
        run_queue_tail = proc;
        proc->next = NULL;
        proc->prev = NULL;
    } else {
        run_queue_tail->next = proc;
        proc->prev = run_queue_tail;
        proc->next = NULL;
        run_queue_tail = proc;
    }

    /* If exec was called on the current process, start it in-place (replace image) */
    if (current_process == proc) {
        process_start_inplace(proc);
    }

    return 0;
}

/* Start executing the given process immediately in-place (replaces current)
   This is a very small trampoline that sets the stack pointer and jumps to
   the process entry point. It does not perform privilege changes. */
static void process_start_inplace(process_t *proc) {
    if (!proc) return;
    void *entry = (void *)(uintptr_t)proc->context.rip;
    void *sp = (void *)(uintptr_t)proc->context.rsp;

    /* Debug: print entry and stack pointer */
    {
        uint64_t e = (uint64_t)(uintptr_t)entry;
        uint64_t s = (uint64_t)(uintptr_t)sp;
        serial_puts("[proc] start entry=0x");
        for (int i = 15; i >= 0; --i) {
            int nib = (e >> (i*4)) & 0xF;
            char c = (nib < 10) ? ('0' + nib) : ('a' + nib - 10);
            serial_putc(c);
        }
        serial_puts(" rsp=0x");
        for (int i = 15; i >= 0; --i) {
            int nib = (s >> (i*4)) & 0xF;
            char c = (nib < 10) ? ('0' + nib) : ('a' + nib - 10);
            serial_putc(c);
        }
        serial_puts("\n");
    }

    /* Inline assembly: set RSP and jump to entry */
    asm volatile(
        "mov %0, %%rsp\n"
        "xor %%rbp, %%rbp\n"
        "jmp *%1\n"
        :
        : "r" (sp), "r" (entry)
        : "memory"
    );
}

/* Wait for process */
int process_wait(pid_t pid, int *status) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return -1;
    
    /* TODO: Wait until process terminates */
    
    if (status)
        *status = proc->exit_code;
    
    return 0;
}

/* Exit process */
void process_exit(int status) {
    if (!current_process)
        return;
    
    current_process->exit_code = status;
    current_process->state = PROCESS_TERMINATED;
}

/* Kill process */
void process_kill(pid_t pid) {
    process_t *proc = process_get_by_pid(pid);
    if (!proc)
        return;
    
    proc->state = PROCESS_TERMINATED;
    proc->exit_code = -1;
}

/* Initialize scheduler */
void scheduler_init(void) {
    run_queue_head = NULL;
    run_queue_tail = NULL;
}

/* Get next runnable process */
process_t* scheduler_next(void) {
    /* Simple FIFO scheduler */
    if (!run_queue_head)
        return current_process;
    
    process_t *next = run_queue_head;
    
    /* Move to back of queue */
    run_queue_head = next->next;
    if (!run_queue_head)
        run_queue_tail = NULL;
    else
        run_queue_head->prev = NULL;
    
    if (run_queue_tail) {
        run_queue_tail->next = next;
        next->prev = run_queue_tail;
    } else {
        run_queue_head = next;
        next->prev = NULL;
    }
    
    run_queue_tail = next;
    next->next = NULL;
    
    return next;
}

/* Yield to next process */
void scheduler_yield(void) {
    process_t *next = scheduler_next();
    if (next)
        current_process = next;
}
