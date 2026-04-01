# System Call Definitions

/* Definition of system calls */

#define SYS_READ       0
#define SYS_WRITE      1
#define SYS_OPEN       2
#define SYS_CLOSE      3

/* Path-based Calling Convention */

#define SYSCALL_PATH "syscall"

int syscall(const char *path, int syscall_number, ...);