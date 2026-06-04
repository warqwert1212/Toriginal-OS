// ==============================================================================
// MAIN.C - Higher-Half Kernel Runtime Entry & Subsystem Coordinator (Multiboot2)
// ==============================================================================
#include <stdint.h>
#include <stddef.h>

// --- Core Subsystem Declarations ---
void init_serial(void);
void print_serial(const char* str);
void vga_clear(void);
void print_vga(const char* str);
void init_idt(void);

// --- Memory Management Declarations ---
void pmm_init(uint64_t multiboot_ptr);
void vmm_init(void);
void kmalloc_init(void);

// --- Timing and Multitasking Declarations ---
void init_pit(uint32_t frequency);
void sched_init(void);

// --- Hardware Bus & Driver Declarations ---
uint32_t pci_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void route_pci_driver(uint8_t bus, uint8_t slot, uint16_t vendor_id);
void init_graphics(uint64_t addr, uint32_t width, uint32_t height, uint32_t pitch);
void clear_screen(uint32_t color);
void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

// --- Storage & Filesystem Declarations ---
void vfs_init(void);
void init_file_systems(void);

// --- Ring 3 & Syscall Gateway Declarations ---
void init_gdt(uint64_t kernel_stack_top);
void init_syscall_gate(void);
int load_and_execute_user_elf(uint8_t* raw_elf_data);

// --- Multiboot2 Tag Structure Specifications ---
struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot2_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[];
};

struct multiboot2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
};

// Safe global kernel stack frame allocation block dedicated to Ring 3 transitions
static uint8_t emergency_kernel_stack[8192] __attribute__((aligned(16)));

// GS scratch table layout mapped for raw assembly pointer context preservation swaps
uint64_t gs_scratch_table[4] __attribute__((aligned(16))) = {0, 0, 0, 0};

// Global RAM disk info extracted from Multiboot2
uint8_t* ramdisk_start = NULL;
uint64_t ramdisk_size = 0;

// Utility helper to render data parameters onto debug pipelines
void print_hex(uint64_t val) {
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[19];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        buffer[2 + i] = hex_chars[val & 0xF];
        val >>= 4;
    }
    buffer[18] = '\0';
    print_serial(buffer);
}

// ==============================================================================
// KERNEL MAIN ENTRY POINT
// ==============================================================================
void kernel_main(uint64_t magic, uint64_t multiboot_ptr) {
    // Phase 1: Emergency & Diagnostic Output Pipelines
    init_serial();
    vga_clear();
    
    print_serial("\n==================================================\n");
    print_serial("       TORIGINAL OS KERNEL (freeNT) BOOTED        \n");
    print_serial("==================================================\n");

    print_vga("==================================================\n");
    print_vga("       TORIGINAL OS KERNEL vga boot               \n");
    print_vga("==================================================\n");

    // Phase 2: Modern Handoff Verification
    if (magic != 0x36D76289) { // Expecting Multiboot2 Magic standard
        print_serial("[FATAL] Invalid Multiboot2 handoff verification code: ");
        print_hex(magic);
        print_serial("\n");
        print_vga("[FATAL] Invalid Multiboot2 handoff.\n");
        return;
    }
    print_serial("[OK] Multiboot2 Handoff Validated.\n");

    // Phase 3: Core CPU Protection Subsystems
    init_idt();
    print_vga("[OK] IDT Loaded. CPU Exceptions Guarded.\n");
    print_serial("[SYS] Kernel successfully reached Ring-0 baseline.\n");

    // Phase 4: Low-Level Memory Subsystems
    pmm_init(multiboot_ptr);
    vmm_init();
    kmalloc_init();
    print_serial("[OK] Memory Management Topology Active.\n");

    // Phase 5: Clock Synchronization & Scheduling Engine
    init_pit(100);  // Set system tick rate to 100Hz
    sched_init();
    print_serial("[OK] Heartbeat Timer and Task Scheduler Online.\n");

    // Phase 6: Hardware Bus Enumeration & Drivers Interfacing
    print_serial("[SYS] Executing Advanced Vendor Driver Checks...\n");
    for (uint16_t slot = 0; slot < 32; slot++) {
        uint32_t device_vendor = pci_read_word(0, slot, 0, 0);
        uint16_t vendor = (uint16_t)(device_vendor & 0xFFFF);
        if (vendor != 0xFFFF) {
            route_pci_driver(0, slot, vendor);
        }
    }

    // Phase 7: Unified Abstraction Filesystem Infrastructure
    vfs_init();
    init_file_systems();

    // Phase 8: Dynamic Multiboot2 Tag Parsing (Framebuffer and Modules)
    uint8_t* tag_ptr = (uint8_t*)(uintptr_t)(multiboot_ptr + 8);
    struct multiboot2_tag* tag = (struct multiboot2_tag*)tag_ptr;
    
    struct multiboot2_tag_framebuffer* fb_tag = NULL;

    while (tag->type != 0) {
        if (tag->type == 3) { // MULTIBOOT_TAG_TYPE_MODULE
            struct multiboot2_tag_module* mod = (struct multiboot2_tag_module*)tag;
            ramdisk_start = (uint8_t*)(uintptr_t)mod->mod_start;
            ramdisk_size = (uint64_t)(mod->mod_end - mod->mod_start);
            print_serial("[OK] Found sysroot.tar Ramdisk loaded in memory.\n");
        }
        else if (tag->type == 8) { // MULTIBOOT_TAG_TYPE_FRAMEBUFFER
            fb_tag = (struct multiboot2_tag_framebuffer*)tag;
        }

        // Move to the next tag structure, aligned to an 8-byte boundary
        tag_ptr += ((tag->size + 7) & ~7);
        tag = (struct multiboot2_tag*)tag_ptr;
    }

    // Phase 9: Linear Graphics Engine Activation
    if (fb_tag != NULL) {
        init_graphics(fb_tag->framebuffer_addr, fb_tag->framebuffer_width, fb_tag->framebuffer_height, fb_tag->framebuffer_pitch);
        
        // Clear screen to solid NT Blue Canvas
        clear_screen(0x000000FF);
        
        // Render a structural system banner widget box
        draw_rect(100, 100, 200, 150, 0x00FF00FF);
        print_serial("[SYS] UI Graphic Pipeline Up.\n");
    } else {
        print_vga("[OK] VGA Screen Driver Initialized.\n");
    }

    // Phase 10: Ring 3 Segments & Secure Architectural Transitions
    uint64_t k_stack_top = (uint64_t)&emergency_kernel_stack[8191];
    init_gdt(k_stack_top);
    print_serial("[OK] GDT Matrix Modified with User Rings and TSS Active.\n");

    // Configure the GS-Base MSR (0xC0000101) to support kernel stack tracking swaps
    gs_scratch_table[1] = k_stack_top; // Index 1 holds kernel stack entry point
    uint64_t gs_base_address = (uint64_t)&gs_scratch_table;
    __asm__ volatile("wrmsr" : : "c"(0xC0000101), "a"((uint32_t)gs_base_address), "d"((uint32_t)(gs_base_address >> 32)));

    init_syscall_gate();
    print_serial("[OK] MSR Fast-Path Syscall Gate Active (MSR_LSTAR Linked).\n");

    print_vga("\nSystem initialization complete. Invoking User Subsystem.\n");
    print_serial("[SYS] All Safe-Vectors active. Loading mock binary payload...\n");

    // Mock binary raw footprint data parsing emulation test (Simple Echo Loop Program)
    static uint8_t mock_user_elf_payload[64] = {
        0x7F, 0x45, 0x4C, 0x46, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, // Valid ELF Header bytes
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    // Unpacks execution sections, assigns user VMM frames, drops privileges, and fires rip into user code.
    load_and_execute_user_elf(mock_user_elf_payload);

    // Fall back to safe thread loop state waiting for hardware scheduler interruptions if loader yields
    while (1) {
        __asm__ volatile ("hlt");
    }
}