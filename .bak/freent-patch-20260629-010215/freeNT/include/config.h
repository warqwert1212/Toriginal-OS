#ifndef _KERNEL_CONFIG_H
#define _KERNEL_CONFIG_H

/* Architecture */
#define ARCH_X86_64 1

/* Memory configuration */
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

/* Maximum number of processes */
#define MAX_PROCESSES 1024

/* Maximum number of open file descriptors per process */
#define MAX_FD_PER_PROCESS 1024

/* Maximum number of memory pages */
#define MAX_PAGES 1048576  /* 4GB with 4K pages */

/* Process priority levels */
#define PROCESS_PRIORITY_MIN 0
#define PROCESS_PRIORITY_MAX 255
#define PROCESS_PRIORITY_DEFAULT 128

/* Timer frequency (Hz) */
#define TIMER_FREQUENCY 1000

/* Debug flags */
#define DEBUG_MEMORY 0
#define DEBUG_PROCESS 0
#define DEBUG_FS 0

#endif /* _KERNEL_CONFIG_H */
