#include "kernel.h"
#include "mm.h"
#include "process.h"
#include "interrupts.h"
#include "syscall.h"
#include "fs.h"
#include "io.h"
#include "string.h"
#include "shell.h"
#include "boot_detect.h"

/* Global kernel state */
static kernel_mem_stats_t kernel_mem_stats = {0};
static volatile int kernel_initialized = 0;

/* Forward declarations */
void kernel_init(void);
void kernel_main(unsigned int multiboot_magic, unsigned int multiboot_info);

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
    
    io_put_string("Toriginal OS Kernel v");
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

/* Main kernel entry point
   Receives Multiboot2 magic and a pointer to the multiboot info tags. */
void kernel_main(unsigned int multiboot_magic, unsigned int multiboot_info) {
    io_clear_screen();

    /* Print early boot banner */
    io_put_string("Toriginal OS Kernel v1.0.0\n");

    /* Initialize serial for early diagnostics and detect boot medium (Multiboot2) */
    serial_init();

    extern void detect_boot_medium(unsigned int, void *);
    extern const char *get_boot_device(void);
    extern const char *get_boot_mode(void);
    extern int get_boot_is_uefi(void);

    serial_puts("[boot] Serial initialized\n");

    detect_boot_medium(multiboot_magic, (void *)(uintptr_t)multiboot_info);

    serial_puts("[boot] Mode: ");
    serial_puts(get_boot_mode());
    serial_puts("\n");

    serial_puts("[boot] Device: ");
    serial_puts(get_boot_device());
    serial_puts("\n\n");

    /* Initialize all kernel subsystems */
    kernel_init();

    /* Enable interrupts */
    interrupts_enable();

    const char *cmdline = get_boot_cmdline();
    serial_puts("[boot] Cmdline: ");
    serial_puts(cmdline ? cmdline : "");
    serial_puts("\n\n");

    if (cmdline && strstr(cmdline, "install") != NULL) {
        io_put_string("Toriginal OS installer mode\n");
        io_put_string("Installing Toriginal OS...\n\n");
        kernel_install_mode();
        kernel_shell();
    } else if (cmdline && strstr(cmdline, "test-installer") != NULL) {
        io_put_string("Toriginal OS installer test mode\n");
        io_put_string("Running installer test commands...\n\n");
        kernel_install_mode();
        kernel_shell();
    } else if (cmdline && strstr(cmdline, "shell") != NULL) {
        io_put_string("Toriginal OS shell mode\n\n");
        kernel_shell();
    } else {
        io_put_string("Toriginal OS GUI mode\n");
        io_put_string("Starting Toriginal OS desktop environment placeholder...\n\n");
        kernel_os_shell();
    }

    /* If the shell ever returns, continue scheduling. */
    while (1) {
        scheduler_yield();
    }
}
