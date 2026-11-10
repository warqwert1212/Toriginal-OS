#include "apphost.h"
#include "app_abi.h"
#include "process.h"
#include "loader.h"
#include "paging.h"
#include "mm.h"
#include "pmm.h"
#include "wm.h"
#include "fs.h"
#include "io.h"
#include "string.h"
#include "serial.h"
#include "shell.h"
#include "pit.h"
#include "crash.h"

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rsp, rflags;
    uint64_t req[4];
} hctx_t;

typedef struct {
    int         used;
    process_t  *proc;
    hctx_t      ctx;
    int         exit_code;
    char        path[96];
} app_t;

#define STACK_BYTES 0x10000ULL
#define CODE_BASE   0x500000ULL
#define CODE_PAGES  256

#define REASON_YIELD   1
#define REASON_PREEMPT 2
#define REASON_EXIT    3
#define REASON_CRASH   4

static app_t g_apps[APP_MAX];
static app_t *g_cur = NULL;

volatile uint64_t g_host_force_leave = 0;
uint64_t g_host_krsp = 0;
hctx_t  *g_host_save = NULL;

extern uint64_t host_enter(hctx_t *c);
extern void host_leave_syscall(uint32_t reason) __attribute__((noreturn));

__asm__(
".global host_enter\n"
"host_enter:\n"
"    pushfq\n"
"    push %rbx\n"
"    push %rbp\n"
"    push %r12\n"
"    push %r13\n"
"    push %r14\n"
"    push %r15\n"
"    mov %rsp, g_host_krsp(%rip)\n"
"    mov %rdi, %r15\n"
"    cli\n"
"    pushq $0x23\n"
"    pushq 128(%r15)\n"
"    mov 136(%r15), %rax\n"
"    and $0x8D5, %rax\n"
"    or $0x202, %rax\n"
"    push %rax\n"
"    pushq $0x2B\n"
"    pushq 120(%r15)\n"
"    mov 0(%r15), %rax\n"
"    mov 8(%r15), %rbx\n"
"    mov 16(%r15), %rcx\n"
"    mov 24(%r15), %rdx\n"
"    mov 32(%r15), %rsi\n"
"    mov 40(%r15), %rdi\n"
"    mov 48(%r15), %rbp\n"
"    mov 56(%r15), %r8\n"
"    mov 64(%r15), %r9\n"
"    mov 72(%r15), %r10\n"
"    mov 80(%r15), %r11\n"
"    mov 88(%r15), %r12\n"
"    mov 96(%r15), %r13\n"
"    mov 104(%r15), %r14\n"
"    mov 112(%r15), %r15\n"
"    iretq\n"
".global host_leave\n"
"host_leave:\n"
"    mov g_host_krsp(%rip), %rsp\n"
"    pop %r15\n"
"    pop %r14\n"
"    pop %r13\n"
"    pop %r12\n"
"    pop %rbp\n"
"    pop %rbx\n"
"    popfq\n"
"    ret\n"
".global host_leave_syscall\n"
"host_leave_syscall:\n"
"    mov %edi, %eax\n"
"    swapgs\n"
"    jmp host_leave\n"
".global host_syscall_yield\n"
"host_syscall_yield:\n"
"    mov g_host_save(%rip), %rax\n"
"    mov %rbx, 8(%rax)\n"
"    mov %rbp, 48(%rax)\n"
"    mov %r12, 88(%rax)\n"
"    mov %r13, 96(%rax)\n"
"    mov %r14, 104(%rax)\n"
"    mov %r15, 112(%rax)\n"
"    mov %rcx, 120(%rax)\n"
"    mov %r11, 136(%rax)\n"
"    mov %gs:8, %rcx\n"
"    mov %rcx, 128(%rax)\n"
"    mov %rdi, 144(%rax)\n"
"    mov %rsi, 152(%rax)\n"
"    mov %rdx, 160(%rax)\n"
"    mov %r10, 168(%rax)\n"
"    movq $0, 0(%rax)\n"
"    swapgs\n"
"    mov $1, %eax\n"
"    jmp host_leave\n"
);

int apphost_in_slice(void) { return g_cur != NULL; }

int apphost_running(void) {
    for (int i = 0; i < APP_MAX; i++) if (g_apps[i].used) return 1;
    return 0;
}

static void app_free_pages(app_t *a) {
    paddr_t root = a->proc->page_table_root;
    for (uint64_t i = 0; i < CODE_PAGES; i++) {
        paddr_t f = mm_unmap_page_in(root, CODE_BASE + i * 0x1000ULL);
        if (f) pmm_free_frame((void *)(uintptr_t)f);
    }
    uint64_t top = a->proc->stack_end ? a->proc->stack_end : 0x7FFFFFFF000ULL;
    for (uint64_t va = top - STACK_BYTES; va < top; va += 0x1000ULL) {
        paddr_t f = mm_unmap_page_in(root, va);
        if (f) pmm_free_frame((void *)(uintptr_t)f);
    }
}

static void app_cleanup(app_t *a) {
    for (int i = 0; i < PROCESS_MAX_OWNED_WINDOWS; i++) {
        uint32_t id = a->proc->owned_windows[i];
        if (id) { wm_destroy_window(id); a->proc->owned_windows[i] = 0; }
    }
    if (a->proc->state != PROCESS_TERMINATED) {
        process_t *prev = process_get_current();
        process_set_current(a->proc);
        process_exit(a->exit_code);
        process_set_current(prev);
    }
    app_free_pages(a);
    {
        process_t *dead = a->proc;
        if (process_get_current() == dead) process_set_current(NULL);
        paging_switch_to(mm_boot_cr3());
        process_reap(dead);
    }
    serial_puts("[apphost] app exited: ");
    serial_puts(a->path);
    serial_puts("\n");
    a->used = 0;
    a->proc = NULL;
}

int apphost_launch(const char *path) {
    if (!path) return -1;
    app_t *a = NULL;
    for (int i = 0; i < APP_MAX; i++) if (!g_apps[i].used) { a = &g_apps[i]; break; }
    if (!a) return -1;

    inode_t st;
    if (fs_stat(path, &st) != 0) return -1;

    process_t *proc = process_create(path, 1);
    if (!proc) return -1;
    if (loader_load_executable(path, proc->pid) != 0) {
        process_kill(proc->pid);
        return -1;
    }

    uint64_t top = proc->stack_end ? proc->stack_end : 0x7FFFFFFF000ULL;
    for (uint64_t va = top - STACK_BYTES; va < top; va += 0x1000ULL) {
        paddr_t frame = (paddr_t)(uintptr_t)mm_alloc_pages(1);
        if (!frame) { process_kill(proc->pid); return -1; }
        memset((void *)(uintptr_t)frame, 0, 0x1000);
        mm_map_page_in(proc->page_table_root, va, (uint64_t)frame, PAGING_WRITE | PAGING_USER);
    }

    memset(a, 0, sizeof(*a));
    a->used = 1;
    a->proc = proc;
    proc->state = PROCESS_RUNNABLE;
    a->ctx.rip = proc->context.rip;
    a->ctx.rsp = top - 64;
    a->ctx.rflags = 0x202;
    strncpy(a->path, path, sizeof(a->path) - 1);
    serial_puts("[apphost] launched ");
    serial_puts(path);
    serial_puts("\n");
    return (int)(a - g_apps);
}

static int user_ok(uint64_t p, uint64_t n) {
    if (p < 0x1000) return 0;
    if (n && p + n < p) return 0;
    return paging_user_range_ok(p, n ? n : 1);
}

static int copy_user_str(char *dst, size_t cap, uint64_t p) {
    if (!p) { dst[0] = 0; return 0; }
    size_t i = 0;
    for (; i + 1 < cap; i++) {
        if (!user_ok(p + i, 1)) return -1;
        char c = ((const char *)(uintptr_t)p)[i];
        dst[i] = c;
        if (!c) return 0;
    }
    dst[i] = 0;
    return 0;
}

static void service_shell(app_t *a, paddr_t kroot) {
    static char cwd[256], cmd[512], out[16384];
    shell_req_t req;
    uint64_t rp = a->ctx.req[1];
    if (!user_ok(rp, sizeof(req))) { a->ctx.rax = (uint64_t)-1; return; }
    memcpy(&req, (const void *)(uintptr_t)rp, sizeof(req));
    if (copy_user_str(cwd, sizeof(cwd), (uint64_t)(uintptr_t)req.cwd) != 0 ||
        copy_user_str(cmd, sizeof(cmd), (uint64_t)(uintptr_t)req.cmd) != 0) {
        a->ctx.rax = (uint64_t)-1; return;
    }
    size_t outmax = req.outmax;
    if (outmax > sizeof(out) - 1) outmax = sizeof(out) - 1;
    if (!user_ok((uint64_t)(uintptr_t)req.out, outmax)) { a->ctx.rax = (uint64_t)-1; return; }

    paging_switch_to(kroot);
    sys_shell_set_cwd(cwd);
    io_capture_begin(out, outmax);
    sys_shell_dispatch(cmd);
    long n = (long)io_capture_end();
    strncpy(cwd, sys_shell_get_cwd(), sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = 0;
    paging_switch_to((paddr_t)a->proc->page_table_root);

    memcpy(req.out, out, (size_t)n);
    if (user_ok((uint64_t)(uintptr_t)req.cwd, 256)) strncpy((char *)(uintptr_t)req.cwd, cwd, 255);
    req.len = n;
    memcpy((void *)(uintptr_t)rp, &req, sizeof(req));
    a->ctx.rax = (uint64_t)n;
}

static void service_request(app_t *a, paddr_t kroot) {
    uint64_t op = a->ctx.req[0];
    if (op == CALL_SHELL) {
        service_shell(a, kroot);
    } else if (op == CALL_SPAWN) {
        char path[160];
        if (copy_user_str(path, sizeof(path), a->ctx.req[1]) != 0) { a->ctx.rax = (uint64_t)-1; return; }
        paging_switch_to(kroot);
        int r = apphost_launch(path);
        paging_switch_to((paddr_t)a->proc->page_table_root);
        a->ctx.rax = (uint64_t)(int64_t)r;
    } else {
        a->ctx.rax = (op == CALL_YIELD) ? 0 : (uint64_t)-1;
    }
    a->ctx.req[0] = 0;
}

static void run_slice(app_t *a) {
    paddr_t kroot = paging_current();
    process_t *prev = process_get_current();
    process_set_current(a->proc);
    g_cur = a;
    g_host_save = &a->ctx;
    paging_switch_to((paddr_t)a->proc->page_table_root);

    uint64_t reason = host_enter(&a->ctx);

    g_host_save = NULL;
    g_cur = NULL;
    process_set_current(prev);

    if (reason == REASON_YIELD) {
        if (a->ctx.req[0] != CALL_YIELD) {
            process_set_current(a->proc);
            service_request(a, kroot);
            process_set_current(prev);
        }
        paging_switch_to(kroot);
    } else if (reason == REASON_PREEMPT) {
        paging_switch_to(kroot);
    } else {
        paging_switch_to(kroot);
        app_cleanup(a);
    }
}

void apphost_step(void) {
    for (int i = 0; i < APP_MAX; i++) {
        if (g_apps[i].used) run_slice(&g_apps[i]);
    }
}

void apphost_preempt(interrupt_frame_t *f) {
    if (!g_cur) return;
    hctx_t *c = &g_cur->ctx;
    c->rax = f->r15; c->rbx = f->r14; c->rcx = f->r13; c->rdx = f->r12;
    c->rsi = f->r11; c->rdi = f->r10; c->rbp = f->r9;
    c->r8 = f->r8; c->r9 = f->rbp; c->r10 = f->rdi; c->r11 = f->rsi;
    c->r12 = f->rdx; c->r13 = f->rcx; c->r14 = f->rbx; c->r15 = f->rax;
    c->rip = f->rip; c->rsp = f->rsp; c->rflags = f->rflags;
    c->req[0] = 0;
    g_host_force_leave = REASON_PREEMPT;
}

void apphost_crash(interrupt_frame_t *f) {
    if (!g_cur) return;
    serial_puts("[apphost] app crashed: ");
    serial_puts(g_cur->path);
    serial_puts(" exception #");
    char b[4]; b[0] = (char)('0' + (f->interrupt_number / 10) % 10);
    b[1] = (char)('0' + f->interrupt_number % 10); b[2] = '\n'; b[3] = 0;
    serial_puts(b);
    g_cur->exit_code = -1;
    {
        char d[40] = "exception #";
        size_t k = 11;
        if (f->interrupt_number >= 10) d[k++] = (char)('0' + (f->interrupt_number / 10) % 10);
        d[k++] = (char)('0' + f->interrupt_number % 10);
        d[k] = 0;
        gui_error_report(g_cur->path, d);
    }
    g_host_force_leave = REASON_CRASH;
}

void apphost_exit_current(int status) {
    g_cur->exit_code = status;
    process_exit(status);
    host_leave_syscall(REASON_EXIT);
}

int apphost_owns_window(uint32_t window_id) {
    for (int i = 0; i < APP_MAX; i++) {
        if (!g_apps[i].used) continue;
        for (int k = 0; k < PROCESS_MAX_OWNED_WINDOWS; k++)
            if (g_apps[i].proc->owned_windows[k] == window_id) return 1;
    }
    return 0;
}

void apphost_close_window(uint32_t window_id) {
    wm_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = WM_EVENT_CLOSE;
    wm_push_event(window_id, &ev);
}

void apphost_shutdown_all(void) {
    for (int i = 0; i < APP_MAX; i++) {
        if (!g_apps[i].used) continue;
        g_apps[i].exit_code = -1;
        app_cleanup(&g_apps[i]);
    }
}
