#include "syscall.h"
#include "io.h"
#include "process.h"
#include "fs.h"
#include "string.h"

/* Syscall handlers */
static syscall_handler_t syscall_handlers[NUM_SYSCALLS] = {0};

/* Syscall dispatch */
uint64_t syscall_dispatch(uint32_t syscall_num, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    if (syscall_num >= NUM_SYSCALLS)
        return -1;
    
    if (!syscall_handlers[syscall_num])
        return -1;
    
    return syscall_handlers[syscall_num](arg1, arg2, arg3, arg4, arg5, arg6);
}

/* Syscall implementations */
static uint64_t sys_exit(uint64_t status, uint64_t a2, uint64_t a3, 
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    process_exit((int)status);
    return 0;
}

static uint64_t sys_fork(uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    return process_fork();
}

static uint64_t sys_exec(uint64_t filename, uint64_t argv, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    process_t *proc = process_get_current();
    if (!proc)
        return -1;
    
    return process_exec(proc->pid, (const char *)filename, (const char **)argv);
}

static uint64_t sys_wait(uint64_t pid, uint64_t status_ptr, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    return process_wait((pid_t)pid, (int *)status_ptr);
}

static uint64_t sys_open(uint64_t path, uint64_t flags, uint64_t mode,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    return fs_open((const char *)path, (int)flags, (int)mode);
}

static uint64_t sys_close(uint64_t fd, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    return fs_close((fd_t)fd);
}

static uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    return fs_read((fd_t)fd, (void *)buf, (size_t)count);
}

static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    return fs_write((fd_t)fd, (const void *)buf, (size_t)count);
}

static uint64_t sys_seek(uint64_t fd, uint64_t offset, uint64_t whence,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    return fs_seek((fd_t)fd, (int64_t)offset, (int)whence);
}

static uint64_t sys_stat(uint64_t path, uint64_t stat_ptr, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    return fs_stat((const char *)path, (inode_t *)stat_ptr);
}

static uint64_t sys_mkdir(uint64_t path, uint64_t mode, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    return fs_mkdir((const char *)path, (int)mode);
}

static uint64_t sys_rmdir(uint64_t path, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    return fs_rmdir((const char *)path);
}

static uint64_t sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5, uint64_t a6) {
    process_t *proc = process_get_current();
    return (proc) ? proc->pid : -1;
}

static uint64_t sys_getppid(uint64_t a1, uint64_t a2, uint64_t a3,
                           uint64_t a4, uint64_t a5, uint64_t a6) {
    process_t *proc = process_get_current();
    return (proc) ? proc->ppid : -1;
}

static uint64_t sys_yield(uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6) {
    scheduler_yield();
    return 0;
}

/* Initialize syscalls */
void syscall_init(void) {
    memset(syscall_handlers, 0, sizeof(syscall_handlers));
    
    syscall_handlers[SYS_EXIT] = sys_exit;
    syscall_handlers[SYS_FORK] = sys_fork;
    syscall_handlers[SYS_EXEC] = sys_exec;
    syscall_handlers[SYS_WAIT] = sys_wait;
    syscall_handlers[SYS_OPEN] = sys_open;
    syscall_handlers[SYS_CLOSE] = sys_close;
    syscall_handlers[SYS_READ] = sys_read;
    syscall_handlers[SYS_WRITE] = sys_write;
    syscall_handlers[SYS_SEEK] = sys_seek;
    syscall_handlers[SYS_STAT] = sys_stat;
    syscall_handlers[SYS_MKDIR] = sys_mkdir;
    syscall_handlers[SYS_RMDIR] = sys_rmdir;
    syscall_handlers[SYS_GETPID] = sys_getpid;
    syscall_handlers[SYS_GETPPID] = sys_getppid;
    syscall_handlers[SYS_YIELD] = sys_yield;
}

/* Register syscall handler */
void syscall_register_handler(uint32_t syscall_num, syscall_handler_t handler) {
    if (syscall_num < NUM_SYSCALLS)
        syscall_handlers[syscall_num] = handler;
}
