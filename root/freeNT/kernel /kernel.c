#include "kernel.h"
#include "mm.h"
#include "pmm.h"
#include "process.h"
#include "interrupts.h"
#include "syscall.h"
#include "fs.h"
#include "io.h"
#include "string.h"
#include "boot_detect.h"
#include "test_exe_data.h"
#include "keyboard.h"
#include "pit.h"

typedef struct {
    char     username[32];
    char     password[32];
    int      timezone_offset;
    uint32_t ui_accent_color;
    int      complex_setup_completed;
} user_config_t;

extern user_config_t current_user;
extern void auth_init(void);
extern void main_oobe_setup(void);

static kernel_mem_stats_t kernel_mem_stats = {0};
static volatile int       kernel_initialized = 0;

void kernel_install_mode(void);
void kernel_shell(void);
void kernel_os_shell(void);

static void create_test_exe(void);
static void run_test_executable(const char *path);

void kernel_install_mode(void) {
    serial_puts("[kernel] Installer mode.\n");
    extern void execute_system_installer(void);
    execute_system_installer();
}

void kernel_shell(void) {
    kernel_os_shell();
}

void kernel_os_shell(void) {
    main_oobe_setup();
}

void kernel_panic(const char *fmt, ...) {
    interrupts_disable();
    serial_puts("=== KERNEL PANIC ===\n");
    serial_puts((char *)fmt);
    serial_puts("\nSystem halted.\n");
    while (1) asm volatile("hlt");
}

kernel_mem_stats_t *kernel_get_mem_stats(void) {
    return &kernel_mem_stats;
}

static uint64_t g_multiboot_ptr = 0;

void kernel_init(void) {
    if (kernel_initialized) return;

    serial_puts("\n--- Toriginal OS freeNT v1.0 ---\n\n");

    serial_puts("[1/9] PMM init...\n");
    pmm_init(g_multiboot_ptr);
    serial_puts("[1/9] PMM OK\n");

    serial_puts("[2/9] Heap init...\n");
    mm_init_heap(0xFFFFFFFF80A00000ULL, 0xFFFFFFFF81A00000ULL);
    serial_puts("[2/9] Heap OK\n");

    serial_puts("[3/9] Interrupts init...\n");
    interrupts_init();
    serial_puts("[3/9] IDT OK\n");

    serial_puts("[4/9] Keyboard init...\n");
    keyboard_wire_idt();
    serial_puts("[4/9] Keyboard OK\n");

    serial_puts("[5/9] Syscall init...\n");
    syscall_init();
    serial_puts("[5/9] Syscalls OK\n");

    serial_puts("[6/9] Process init...\n");
    process_init();
    scheduler_init();
    serial_puts("[6/9] Process OK\n");

    serial_puts("[7/9] Filesystem init...\n");
    fs_init();
    create_test_exe();
    serial_puts("[7/9] FS OK\n");

    serial_puts("[8/9] Auth init...\n");
    auth_init();
    serial_puts("[8/9] Auth OK\n");

    serial_puts("[9/9] PIT + scheduler init...\n");
    init_pit(100);
    sched_init();
    serial_puts("[9/9] PIT OK\n");

    serial_puts("\nKernel init complete.\n\n");
    kernel_initialized = 1;
}

static inline void early_serial_char(char c) {
    asm volatile(
        "mov $0x3f8, %%dx\n"
        "outb %%al, %%dx\n"
        : : "a"(c) : "dx"
    );
}

static void early_dump_ptr(unsigned long ptr) {
    static const char hex[] = "0123456789abcdef";
    for (int i = (int)(sizeof(ptr) * 2) - 1; i >= 0; --i) {
        unsigned int nibble = (ptr >> (i * 4)) & 0xF;
        early_serial_char(hex[nibble]);
    }
}

static void create_test_exe(void) {
    fd_t fd = fs_open("/test.exe",
                      O_CREAT | O_WRONLY | O_TRUNC,
                      FILE_PERM_OWNER_R | FILE_PERM_OWNER_W | FILE_PERM_OWNER_X);
    if (fd < 0) { serial_puts("[boot] test.exe create failed\n"); return; }
    fs_write(fd, _tmp_test_exe, _tmp_test_exe_len);
    fs_close(fd);
    serial_puts("[boot] /test.exe created\n");
}

static void run_test_executable(const char *path) {
    serial_puts("[boot] run-exe: ");
    serial_puts(path);
    serial_puts("\n");
    process_t *p = process_create(path, 1);
    if (!p) { serial_puts("[boot] process create failed\n"); return; }
    if (process_exec(p->pid, path, NULL) != 0)
        serial_puts("[boot] exec failed\n");
    else
        process_start(p->pid);
}

void kernel_main(unsigned int multiboot_magic, unsigned int multiboot_info) {
    early_serial_char('X');
    serial_init();
    early_serial_char('Y');

    early_dump_ptr((unsigned long)kernel_main);
    early_serial_char(':');
    early_dump_ptr((unsigned long)io_clear_screen);
    early_serial_char('\n');

    serial_puts("[boot] Serial OK\n");

    g_multiboot_ptr = (uint64_t)(uintptr_t)multiboot_info;

    extern void        detect_boot_medium(unsigned int, void *);
    extern const char *get_boot_device(void);
    extern const char *get_boot_mode(void);
    extern const char *get_boot_cmdline(void);

    detect_boot_medium(multiboot_magic, (void *)(uintptr_t)multiboot_info);

    serial_puts("[boot] Mode:   "); serial_puts(get_boot_mode());   serial_puts("\n");
    serial_puts("[boot] Device: "); serial_puts(get_boot_device()); serial_puts("\n\n");

    kernel_init();

    interrupts_enable();
    serial_puts("[boot] Interrupts enabled. Keyboard + PIT live.\n");

    const char *cmdline = get_boot_cmdline();
    serial_puts("[boot] Cmdline: ");
    serial_puts(cmdline ? cmdline : "(none)");
    serial_puts("\n\n");

    if (cmdline && strstr(cmdline, "install") != NULL) {
        io_put_string("Toriginal OS - Installer\n");
        kernel_install_mode();
        kernel_shell();
    } else if (cmdline && strstr(cmdline, "run-exe") != NULL) {
        io_put_string("Toriginal OS - run-exe\n");
        run_test_executable("/test.exe");
    } else if (cmdline && strstr(cmdline, "shell") != NULL) {
        io_put_string("Toriginal OS - Shell\n");
        kernel_shell();
    } else {
        kernel_os_shell();
    }

    while (1) asm volatile("hlt");
}

void execute_system_installer(void) {
    while (1) asm volatile("hlt");
}