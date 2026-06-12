/* =============================================================================
 * kernel.c - Toriginal OS freeNT Kernel Entry Point
 * Only includes headers that actually exist in root/freeNT/include/
 * ============================================================================= */

#include "kernel.h"
#include "types.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keybord.h"
#include "panic.h"

/* =============================================================================
 * Forward declarations for functions in other .c files
 * ============================================================================= */

/* interrups.c */
void interrupts_init(void);

/* syscall.c */
void syscall_init(void);

/* keyboard wire.c */
void keyboard_wire_idt(void);

/* boot/shell.c - our shell, not built yet, stubbed below */
void kernel_os_shell(void);

/* =============================================================================
 * Print to both VGA and serial at once
 * ============================================================================= */

static void kprint(const char *str) {
    serial_write(str);
    vga_write(str);
}

/* =============================================================================
 * Multiboot2 memory map parser — just logs what RAM we have
 * ============================================================================= */

#define MB2_TAG_END  0
#define MB2_TAG_MMAP 6

typedef struct {
    uint32_t type;
    uint32_t size;
} __attribute__((packed)) mb2_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed)) mb2_tag_mmap_t;

typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed)) mb2_mmap_entry_t;

static uint64_t g_multiboot_ptr = 0;

static void parse_multiboot(uint64_t mb_ptr) {
    if (!mb_ptr) return;
    uint8_t *ptr = (uint8_t *)(uintptr_t)(mb_ptr + 8);
    while (1) {
        mb2_tag_t *tag = (mb2_tag_t *)ptr;
        if (tag->type == MB2_TAG_END || tag->size == 0) break;
        if (tag->type == MB2_TAG_MMAP) {
            mb2_tag_mmap_t *mt = (mb2_tag_mmap_t *)tag;
            uint8_t *ep  = (uint8_t *)mt + sizeof(mb2_tag_mmap_t);
            uint8_t *end = (uint8_t *)tag + tag->size;
            serial_write("[MEM] Memory map:\n");
            while (ep < end) {
                mb2_mmap_entry_t *e = (mb2_mmap_entry_t *)ep;
                if (e->type == 1) {
                    serial_write("[MEM]   Available: 0x");
                    serial_write_hex(e->base_addr);
                    serial_write("  len=");
                    serial_write_hex(e->length);
                    serial_write("\n");
                }
                ep += mt->entry_size;
            }
        }
        ptr += (tag->size + 7) & ~7u;
    }
}

/* =============================================================================
 * kernel_init
 * ============================================================================= */

static volatile int kernel_initialized = 0;

static void kernel_init(void) {
    if (kernel_initialized) return;

    kprint("\n");
    kprint("================================================\n");
    kprint("        Toriginal OS  -  freeNT v1.0\n");
    kprint("        Made by warqwert\n");
    kprint("================================================\n\n");

    kprint("[1/5] Memory init...\n");
    memory_init();
    kprint("[1/5] Memory OK\n");

    kprint("[2/5] IDT init...\n");
    idt_init();   /* This includes pic_remap() internally */
    kprint("[2/5] IDT OK\n");

    kprint("[3/5] PIT init (100Hz)...\n");
    pit_init(100);
    kprint("[3/5] PIT OK\n");

    kprint("[3/5] Keyboard init...\n");
    keyboard_wire_idt();
    kprint("[3/5] Keyboard OK\n");

    kprint("\nKernel ready.\n\n");
    kernel_initialized = 1;
}

/* =============================================================================
 * kernel_main
 * ============================================================================= */

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    serial_init();
    vga_init();
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();

    if (multiboot_magic != 0x36D76289) {
        vga_set_color(VGA_WHITE, VGA_RED);
        vga_write("FATAL: Not booted by Multiboot2!\n");
        serial_write("FATAL: bad multiboot magic\n");
        for (;;) __asm__ volatile("hlt");
    }

    serial_write("[boot] Multiboot2 OK\n");
    g_multiboot_ptr = (uint64_t)(uintptr_t)multiboot_info;
    parse_multiboot(g_multiboot_ptr);

    kernel_init();

    __asm__ volatile("sti");
    serial_write("[boot] Interrupts enabled\n");

    kernel_os_shell();

    for (;;) __asm__ volatile("hlt");
}

/* =============================================================================
 * kernel_panic
 * ============================================================================= */

void kernel_panic(const char *reason) {
    __asm__ volatile("cli");
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_clear();
    vga_write("\n  KERNEL PANIC\n\n  ");
    vga_write(reason);
    serial_write("\nKERNEL PANIC: ");
    serial_write(reason);
    serial_write("\n");
    for (;;) __asm__ volatile("hlt");
}

/* =============================================================================
 * kernel_os_shell stub — replaced when we build the real shell
 * ============================================================================= */

void kernel_os_shell(void) {
    kprint("================================================\n");
    kprint("        Welcome to Toriginal OS v1.0\n");
    kprint("        Shell coming soon...\n");
    kprint("================================================\n");
    kprint("\n  Kernel is running. Halting until shell is ready.\n");
    serial_write("[shell] stub - halting\n");
    for (;;) __asm__ volatile("hlt");
}