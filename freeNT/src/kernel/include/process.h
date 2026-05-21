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

/* CPU context (for context switching) */
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cr3;  /* Page table root */
} cpu_context_t;

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

/* Scheduler */
void scheduler_init(void);
process_t* scheduler_next(void);
void scheduler_yield(void);

#endif /* _KERNEL_PROCESS_H */
