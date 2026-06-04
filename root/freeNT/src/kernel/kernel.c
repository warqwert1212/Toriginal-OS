#include "kernel.h"
#include "mm.h"
#include "process.h"
#include "interrupts.h"
#include "syscall.h"
#include "fs.h"
#include "io.h"
#include "string.h"
#include "boot_detect.h"
#include "test_exe_data.h"

/* Core Authentication Integration */
typedef struct {
    char username[32];
    char password[32];
    int timezone_offset;
    uint32_t ui_accent_color;
    int complex_setup_completed;
} user_config_t;

extern user_config_t current_user;
extern void auth_init(void);
extern void main_oobe_setup(void);

/* Global kernel state */
static kernel_mem_stats_t kernel_mem_stats = {0};
static volatile int kernel_initialized = 0;

/* Boot Mode Handlers - Required by Linker */
void kernel_install_mode(void);
void kernel_shell(void);
void kernel_os_shell(void);

/* Forward declarations */
void kernel_init(void);
void kernel_main(unsigned int multiboot_magic, unsigned int multiboot_info);
static void create_test_exe(void);
static void run_test_executable(const char *path);

void kernel_install_mode(void) {
    serial_puts("[kernel] Entering System Installer Context...\n");
    extern void execute_system_installer(void);
    execute_system_installer();
}

void kernel_shell(void) {
    serial_puts("[kernel] C++ shell disabled. Routing to OOBE Fallback instead...\n");
    kernel_os_shell();
}

void kernel_os_shell(void) {
    serial_puts("[kernel] Launching Out-of-Box Experience Setup...\n");
    main_oobe_setup();
}

/* Kernel panic - terminate execution */
void kernel_panic(const char *fmt, ...) {
    interrupts_disable();
    
    serial_puts("=== KERNEL PANIC ===\n");
    serial_puts((char *)fmt);
    serial_puts("\n\nSystem halted.\n");
    
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
    
    serial_puts("Toriginal OS Kernel v1.0.0 initializing...\n");
    
    /* * FIX 1: HEAP CORRECTION FOR HIGHER-HALF CANONICAL 64-BIT MEMORY
     * Previously, your heap was mapped to low memory spaces (0x00A00000UL).
     * Now that the entire kernel compilation workspace resides in 
     * 0xFFFFFFFF80000000, your heap base must sit relative to it to prevent 
     * immediate page space violations during early kmalloc calls.
     */
    serial_puts("  Initializing memory management...\n");
    mm_init_physical(0x100000, 0xFFFFFFFF);  
    mm_init_heap(0xFFFFFFFF80A00000UL, 0xFFFFFFFF81A00000UL);
    
    /* Initialize paging */
    serial_puts("  Paging enabled\n");
    
    /* Initialize interrupt handling */
    serial_puts("  Initializing interrupts...\n");
    
    /* Initialize system calls */
    serial_puts("  Initializing syscalls...\n");
    syscall_init();
    
    /* Initialize process management */
    serial_puts("  Initializing process manager...\n");
    process_init();
    scheduler_init();
    
    /* Initialize filesystem */
    serial_puts("  Initializing filesystem...\n");
    fs_init();
    create_test_exe();

    /* Initialize User Authentication Modules */
    serial_puts("  Initializing account modules...\n");
    auth_init();
    
    serial_puts("Kernel initialization complete!\n\n");
    kernel_initialized = 1;
}

static inline void early_serial_char(char c) {
    asm volatile (
        "mov $0x3f8, %%dx\n"
        "outb %%al, %%dx\n"
        :
        : "a" (c)
        : "dx"
    );
}

static void early_dump_ptr(unsigned long ptr) {
    static const char hex[] = "0123456789abcdef";
    /* * FIX 2: POINTER SIZE BITMASK ADJUSTMENT
     * In 64-bit long mode, pointers are 8 bytes (16 hex nibbles), not 4 bytes. 
     * Adjusted loop boundary parsing to dump full 64-bit structural canonical 
     * memory pointer registers over the early serial lines.
     */
    for (int i = (int)(sizeof(ptr) * 2) - 1; i >= 0; --i) {
        unsigned int nibble = (ptr >> (i * 4)) & 0xF;
        early_serial_char(hex[nibble]);
    }
}

static void create_test_exe(void) {
    fd_t fd = fs_open("/test.exe", O_CREAT | O_WRONLY | O_TRUNC,
                      FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X);
    if (fd < 0) {
        serial_puts("[boot] Test EXE: create failed\n");
        return;
    }

    if (fs_write(fd, _tmp_test_exe, _tmp_test_exe_len) < 0) {
        serial_puts("[boot] Test EXE: write failed\n");
        fs_close(fd);
        return;
    }

    fs_close(fd);
    serial_puts("[boot] Test EXE created at /test.exe\n");
}

static void run_test_executable(const char *path) {
    serial_puts("[boot] Running test EXE: ");
    serial_puts(path);
    serial_puts("\n");

    process_t *p = process_create(path, 1);
    if (!p) {
        serial_puts("[boot] Test EXE: process create failed\n");
        return;
    }

    if (process_exec(p->pid, path, NULL) != 0) {
        serial_puts("[boot] Test EXE: exec failed\n");
        return;
    }

    process_start(p->pid);
}

/* Main kernel entry point */
void kernel_main(unsigned int multiboot_magic, unsigned int multiboot_info) {
    asm volatile ("mov $0x3f8, %%dx; mov $'X', %%al; out %%al, %%dx;" ::: "dx", "al");
    serial_init();
    asm volatile ("mov $0x3f8, %%dx; mov $'Y', %%al; out %%al, %%dx;" ::: "dx", "al");

    early_dump_ptr((unsigned long)kernel_main);
    early_serial_char(':');
    early_dump_ptr((unsigned long)io_clear_screen);
    early_serial_char('|');
    early_dump_ptr((unsigned long)io_put_string);
    early_serial_char('\n');
    asm volatile (
        "mov $0x3f8, %%dx\n"
        "mov $'M', %%al\n"
        "out %%al, %%dx\n"
        "mov $0x3f8, %%dx\n"
        "mov $'N', %%al\n"
        "out %%al, %%dx\n"
        ::: "dx", "al"
    );

    extern void detect_boot_medium(unsigned int, void *);
    extern const char *get_boot_device(void);
    extern const char *get_boot_mode(void);
    extern const char *get_boot_cmdline(void);

    serial_puts("[boot] Serial initialized\n");

    /* * FIX 3: 64-BIT MULTIBOOT INFO MAPPING POINTER
     * GRUB passes the multiboot physical information pointer structure as a 32-bit address.
     * We explicitly cast to uintptr_t before jumping up to our high canonical workspace space 
     * to safely interact with it without pointer clipping.
     */
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
    } else if (cmdline && strstr(cmdline, "run-exe") != NULL) {
        io_put_string("Toriginal OS run-exe mode\n");
        io_put_string("Executing /test.exe...\n");
        run_test_executable("/test.exe");
    } else if (cmdline && strstr(cmdline, "shell") != NULL) {
        io_put_string("Toriginal OS shell mode\n\n");
        kernel_shell();
    } else {
        io_put_string("Toriginal OS Boot Sequence Initializing...\n");
        kernel_os_shell();
    }

    while (1) {
        scheduler_yield();
    }
}

void execute_system_installer(void) {
    while(1) {
        __asm__ volatile("hlt");
    }
}