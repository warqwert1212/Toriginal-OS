// ==============================================================================
// IDT.C - CPU Exception and Interrupt Handling
// ==============================================================================
#include <stdint.h>

// Forward declare the VGA print function so we can print panics
void print_vga(const char* str);
extern void load_idt(void* idt_ptr); // Defined in assembly below

// 64-bit IDT Entry Structure
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

void idt_set_gate(uint8_t num, uint64_t handler, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = handler & 0xFFFF;
    idt[num].selector    = sel;
    idt[num].ist         = 0;
    idt[num].type_attr   = flags;
    idt[num].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].zero        = 0;
}

// A generic C handler for CPU panics
void isr_handler(void) {
    print_vga("\n[KERNEL PANIC] CPU Exception Caught! System Halted.\n");
    while(1) { __asm__ volatile("cli; hlt"); }
}

// We will map the first 32 exceptions to this generic handler for now
extern void isr_stub(void);

void init_idt(void) {
    idtp.limit = sizeof(struct idt_entry) * 256 - 1;
    idtp.base  = (uint64_t)&idt;

    // Zero out the IDT
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // Set Gate 0-31 (CPU Exceptions) to point to our stub
    // 0x8E = Present, Ring 0, 64-bit Interrupt Gate
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (uint64_t)isr_stub, 0x08, 0x8E); 
    }

    load_idt(&idtp);
}