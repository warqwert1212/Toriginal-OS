// ==============================================================================
// SCHED.H - Preemptive Scheduler Architecture
// ==============================================================================
#pragma once
#include <stdint.h>

struct cpu_context {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss; // Pushed automatically by CPU during interrupt
} __attribute__((packed));

struct task {
    int id;
    uint64_t rsp;
    struct task* next;
};

void sched_init(void);
void sched_create_task(void (*entry_point)(void));
uint64_t schedule(uint64_t current_rsp);