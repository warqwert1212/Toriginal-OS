// ==============================================================================
// SCHED.C - Round-Robin Task Switcher
// ==============================================================================
#include "sched.h"
#include "heap.h"

struct task {
    int id;
    uint64_t rsp;
    struct task* next;
};

void print_serial(const char* str);

static struct task* task_queue_head = NULL;
static struct task* current_task = NULL;
static int next_task_id = 1;

void sched_init(void) {
    // Main execution tracking envelope
    current_task = kmalloc(sizeof(struct task));
    current_task->id = 0;
    current_task->rsp = 0; // Filled on first context switch tick
    current_task->next = current_task;
    task_queue_head = current_task;
    
    print_serial("[SCHED] Preemptive Task Scheduling Matrix Operational.\n");
}

void sched_create_task(void (*entry_point)(void)) {
    struct task* new_task = kmalloc(sizeof(struct task));
    new_task->id = next_task_id++;
    
    // Allocate explicit isolated execution stack
    void* stack_mem = kmalloc(4096);
    uint64_t* stack = (uint64_t*)((uintptr_t)stack_mem + 4096);

    // Forge a simulated context frame structure matching an interrupt frame sequence
    *(--stack) = 0x10;          // SS (Kernel Data Segment)
    *(--stack) = (uintptr_t)stack; // RSP target point
    *(--stack) = 0x202;         // RFLAGS (Interrupts enabled)
    *(--stack) = 0x08;          // CS (Kernel Code Segment)
    *(--stack) = (uintptr_t)entry_point; // RIP target point

    // General purpose tracking register placeholders
    for (int i = 0; i < 15; i++) {
        *(--stack) = 0;
    }

    new_task->rsp = (uint64_t)stack;
    
    // Insert into round-robin ring
    new_task->next = task_queue_head->next;
    task_queue_head->next = new_task;
}

uint64_t schedule(uint64_t current_rsp) {
    if (!current_task) return current_rsp;

    // Save active context snapshot address pointer location
    current_task->rsp = current_rsp;
    
    // Cycle to adjacent target task inside execution loop rings
    current_task = current_task->next;
    
    return current_task->rsp;
}