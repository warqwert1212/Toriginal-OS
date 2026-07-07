/* =============================================================================
 * kernel.c - Toriginal OS freeNT v1.0 kernel entry point
 * ============================================================================= */

#include "kernel.h"
#include "types.h"
#include "vga.h"
#include "serial.h"
#include "memory.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "panic.h"
#include "fs.h"
#include "process.h"
#include "shell.h"
#include "installer.h"

void interrupts_init(void);
void syscall_init(void);
void keyboard_wire_idt(void);
void mouse_wire_init(void);
void scheduler_init(void);

static void early_print(const char *s) {
    serial_write(s);
}

static void kprint(const char *s) {
    serial_write(s);
    vga_write(s);
}

static const char g_hex_digits[] = "0123456789ABCDEF";

static void early_print_hex(uint64_t v) {
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++)
        buf[2 + i] = g_hex_digits[(v >> (60 - i*4)) & 0xF];
    buf[18] = '\0';
    serial_write(buf);
}

#define MB2_TAG_END   0u
#define MB2_TAG_MMAP  6u
#define MB2_MAX_ITER  64u

typedef struct { uint32_t type; uint32_t size; }
    __attribute__((packed)) mb2_tag_t;

typedef struct { uint32_t type; uint32_t size;
                 uint32_t entry_size; uint32_t entry_version; }
    __attribute__((packed)) mb2_tag_mmap_t;

typedef struct { uint64_t base_addr; uint64_t length;
                 uint32_t type; uint32_t reserved; }
    __attribute__((packed)) mb2_mmap_entry_t;

static void parse_multiboot(uint32_t mb_info_phys) {
    if (!mb_info_phys) { early_print("[MEM] No multiboot info pointer\n"); return; }

    uint8_t *ptr = (uint8_t *)(uintptr_t)mb_info_phys + 8;
    uint32_t total_size = *(uint32_t *)(uintptr_t)mb_info_phys;

    if (total_size < 8 || total_size > 4 * 1024 * 1024) {
        early_print("[MEM] Multiboot info size invalid - skipping mmap\n");
        return;
    }

    uint8_t *struct_end = (uint8_t *)(uintptr_t)mb_info_phys + total_size;

    for (uint32_t iter = 0; iter < MB2_MAX_ITER; iter++) {
        if (ptr + sizeof(mb2_tag_t) > struct_end) break;

        mb2_tag_t *tag = (mb2_tag_t *)ptr;
        if (tag->type == MB2_TAG_END) break;
        if (tag->size < 8) break;

        if (tag->type == MB2_TAG_MMAP) {
            mb2_tag_mmap_t *mt = (mb2_tag_mmap_t *)ptr;
            uint32_t es = mt->entry_size;

            if (es < 24 || es > 256 || (es & 7)) {
                early_print("[MEM] Bad mmap entry_size - skipping\n");
            } else {
                uint8_t *ep  = ptr + sizeof(mb2_tag_mmap_t);
                uint8_t *end = ptr + tag->size;
                early_print("[MEM] Memory map:\n");
                while (ep + es <= end) {
                    mb2_mmap_entry_t *e = (mb2_mmap_entry_t *)ep;
                    if (e->type == 1) {
                        early_print("[MEM]   avail  base=");
                        early_print_hex(e->base_addr);
                        early_print("  len=");
                        early_print_hex(e->length);
                        early_print("\n");
                    }
                    ep += es;
                }
            }
        }

        uint32_t next = (tag->size + 7u) & ~7u;
        if (next == 0) break;
        ptr += next;
    }
}

static volatile int kernel_initialized = 0;

static void kernel_init(uint32_t mb_info_phys) {
    if (kernel_initialized) return;

    kprint("\n");
    kprint("  ================================================\n");
    kprint("       Toriginal OS  -  freeNT v1.1\n");
    kprint("       Made by warqwert\n");
    kprint("  ================================================\n\n");

    parse_multiboot(mb_info_phys);
    kprint("\n");

    kprint("[1/6] Memory init...\n");
    memory_init(mb_info_phys);
    kprint("[1/6] Memory OK\n");

    kprint("[2/6] IDT init...\n");
    idt_init();
    kprint("[2/6] IDT OK\n");

    kprint("[3/6] PIT init (100 Hz)...\n");
    pit_init(100);
    kprint("[3/6] PIT OK\n");

    kprint("[4/6] Keyboard init...\n");
    keyboard_wire_idt();
    kprint("[4/6] Keyboard OK\n");

    kprint("[5/6] Mouse init...\n");
    mouse_wire_init();
    kprint("[5/6] Mouse OK\n");

    kprint("[6/6] Filesystem + process init...\n");
    fs_init();
    if (installer_try_automount() == 0) {
        kprint("[6/6] Persistent filesystem mounted (data restored from disk)\n");
        vga_set_statusbar_enabled(1);
    } else {
        kprint("[6/6] No persistent disk found - run 'install' to set one up\n");
    }
    process_init();
    scheduler_init();
    syscall_init();
    kprint("[6/6] Subsystems OK\n");

    kprint("\n[BOOT] freeNT ready. Type 'help' for commands.\n\n");
    kernel_initialized = 1;
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info) {
    serial_init();
    vga_init();
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();

    if (multiboot_magic != 0x36D76289) {
        vga_write("FATAL: Not booted via Multiboot2!\n");
        serial_write("FATAL: bad multiboot magic\n");
        for (;;) __asm__ volatile("hlt");
    }

    serial_write("[boot] Multiboot2 OK\n");

    kernel_init(multiboot_info);

    __asm__ volatile("sti");

    serial_write("[boot] Interrupts enabled\n");
    serial_write("[boot] *** KERNEL FULLY BOOTED - freeNT v1.1 READY ***\n");
    serial_write("[boot] Shell starting - type commands below\n");
    serial_write("os~$ ");

    kernel_os_shell();

    for (;;) __asm__ volatile("hlt");
}

void kernel_panic(const char *reason) {
    __asm__ volatile("cli");
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_clear();
    vga_write("\n  !!! KERNEL PANIC !!!\n\n  ");
    vga_write(reason);
    vga_write("\n\n  System halted.\n");
    serial_write("\nKERNEL PANIC: ");
    serial_write(reason);
    serial_write("\n");
    for (;;) __asm__ volatile("hlt");
}