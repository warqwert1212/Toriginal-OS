#include "kernel.h"
#include "mm.h"
#include "process.h"
#include "interrupts.h"
#include "syscall.h"
#include "fs.h"
#include "io.h"
#include "string.h"

/* Global kernel state */
static kernel_mem_stats_t kernel_mem_stats = {0};
static volatile int kernel_initialized = 0;

/* Forward declarations */
void kernel_init(void);
void kernel_main(void);

/* Kernel panic - terminate execution */
void kernel_panic(const char *fmt, ...) {
    interrupts_disable();
    
    io_clear_screen();
    io_put_string("=== KERNEL PANIC ===\n");
    io_put_string((char *)fmt);
    io_put_string("\n\nSystem halted.\n");
    
    while (1) {
        asm volatile("hlt");
    }
}

/* Get kernel memory statistics */
kernel_mem_stats_t* kernel_get_mem_stats(void) {
    return &kernel_mem_stats;
}

/* Initialize kernel subsystems */
void kernel_init(void) {
    if (kernel_initialized)
        return;
    
    io_put_string("freeNT Kernel v");
    io_put_string("1.0.0");
    io_put_string(" initializing...\n");
    
    /* Initialize memory management */
    io_put_string("  Initializing memory management...\n");
    mm_init_physical(0x100000, 0x100000000);  /* 1MB to 4GB */
    mm_init_heap(0xFFFF800000000000UL, 0xFFFF800000100000UL);
    
    /* Initialize paging */
    mm_init_paging();
    mm_enable_paging();
    io_put_string("  Paging enabled\n");
    
    /* Initialize interrupt handling */
    io_put_string("  Initializing interrupts...\n");
    idt_init();
    interrupts_init();
    
    /* Initialize system calls */
    io_put_string("  Initializing syscalls...\n");
    syscall_init();
    
    /* Initialize process management */
    io_put_string("  Initializing process manager...\n");
    process_init();
    scheduler_init();
    
    /* Initialize filesystem */
    io_put_string("  Initializing filesystem...\n");
    fs_init();
    
    io_put_string("Kernel initialization complete!\n\n");
    kernel_initialized = 1;
}

/* Main kernel entry point */
void kernel_main(void) {
    io_clear_screen();
    
    /* Initialize all kernel subsystems */
    kernel_init();
    
    /* Enable interrupts */
    interrupts_enable();
    
    io_put_string("freeNT kernel running\n");
    io_put_string("Ready to execute Toriginal OS shell...\n\n");
    
    /* The shell would be loaded and executed here */
    /* For now, main kernel loop */
    while (1) {
        scheduler_yield();
    }
}
