/* ==============================================================================
 * RUNTIME_STUBS.C - Runtime implementations for missing kernel functions
 * Provides stub/implementation of functions referenced during linking
 * ============================================================================== */

#include <stdint.h>
#include <stddef.h>

/* Define ssize_t if not already defined */
typedef long ssize_t;

/* Suppress unused parameter warnings */
#define UNUSED(x) (void)(x)

/* ==============================================================================
 * Port I/O Functions (inb/outb)
 * ============================================================================== */

uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0"
                      : "=a"(ret)
                      : "Nd"(port));
    return ret;
}

void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1"
                      :
                      : "a"(value), "Nd"(port));
}

/* ==============================================================================
 * Timer/Scheduler Stubs
 * ============================================================================== */

static volatile uint32_t pit_ticks = 0;

void pit_handler(void)
{
    pit_ticks++;
}

/* scheduler_yield is implemented in process.c */

/* ==============================================================================
 * Filesystem Stubs
 * ============================================================================== */

typedef struct {
    uint64_t ino;
    uint64_t size;
} inode_t_stub;

int fs_stat(const char *path, inode_t_stub *stat)
{
    UNUSED(path);
    UNUSED(stat);
    return -1; /* File not found */
}

int fs_open(const char *path, int flags, int mode)
{
    UNUSED(path);
    UNUSED(flags);
    UNUSED(mode);
    return -1; /* Cannot open */
}

ssize_t fs_read(int fd, void *buf, size_t count)
{
    UNUSED(fd);
    UNUSED(buf);
    UNUSED(count);
    return -1; /* Read error */
}

ssize_t fs_write(int fd, const void *buf, size_t count)
{
    UNUSED(fd);
    UNUSED(buf);
    UNUSED(count);
    return (ssize_t)count; /* Pretend we wrote */
}

int fs_close(int fd)
{
    UNUSED(fd);
    return 0; /* OK */
}

int fs_mkdir(const char *path, int mode)
{
    UNUSED(path);
    UNUSED(mode);
    return 0; /* OK (created or exists) */
}

/* ==============================================================================
 * I/O Stubs (VGA/Screen)
 * ============================================================================== */

void io_put_string(const char *s)
{
    /* Stub: outputs via serial for now */
    extern void serial_puts(const char *str);
    serial_puts(s);
}

void io_put_char(char c)
{
    extern void serial_putc(char ch);
    serial_putc(c);
}

void io_clear_screen(void)
{
    /* Stub: no-op */
}

void io_put_hex(uint64_t v)
{
    UNUSED(v);
    /* Stub: no-op */
}

/* ==============================================================================
 * Process/Memory Stubs
 * ============================================================================== */

/* process_* functions are implemented in process.c */
/* kmalloc, kfree, krealloc are implemented in memory.c */

/* ==============================================================================
 * Assembly helpers (if not linked from boot64.s)
 * ============================================================================== */

extern void load_idt(void *ptr);
__attribute__((weak))
void load_idt(void *ptr)
{
    UNUSED(ptr);
    /* Weak: boot64.s should provide real implementation */
}

/* ==============================================================================
 * ISR/IRQ Stubs (basic)
 * ============================================================================== */

/* keyboard_irq_handler: called from keyboard_isr_stub assembly */
void keyboard_irq_handler(void)
{
    /* Reads the keyboard scancode and pushes ASCII character to queue */
    extern void keyboard_handler(void);
    keyboard_handler();
}

void isr0(void) { }
void isr1(void) { }
void isr2(void) { }
void isr3(void) { }
void isr4(void) { }
void isr5(void) { }
void isr6(void) { }
void isr7(void) { }
void isr8(void) { }
void isr9(void) { }
void isr10(void) { }
void isr11(void) { }
void isr12(void) { }
void isr13(void) { }
void isr14(void) { }
void isr15(void) { }
void isr16(void) { }
void isr17(void) { }
void isr18(void) { }
void isr19(void) { }
void isr20(void) { }
void isr21(void) { }
void isr22(void) { }
void isr23(void) { }
void isr24(void) { }
void isr25(void) { }
void isr26(void) { }
void isr27(void) { }
void isr28(void) { }
void isr29(void) { }
void isr30(void) { }
void isr31(void) { }

void irq0(void) { }
void irq1(void) { }
void irq2(void) { }
void irq3(void) { }
void irq4(void) { }
void irq5(void) { }
void irq6(void) { }
void irq7(void) { }
void irq8(void) { }
void irq9(void) { }
void irq10(void) { }
void irq11(void) { }
void irq12(void) { }
void irq13(void) { }
void irq14(void) { }
void irq15(void) { }

/* ==============================================================================
 * Memory init stub
 * ============================================================================== */

void memory_init(void)
{
    /* Stub: basic memory initialization already handled by mm_init_physical */
}
