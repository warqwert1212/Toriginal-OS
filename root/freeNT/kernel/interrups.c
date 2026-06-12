// ==============================================================================
// INTERRUPTS.C
// Single authoritative IDT for the whole kernel.
// Owns: IDT storage, PIC init, IRQ0 (PIT), IRQ1 (keyboard via keyboard_wire.c),
//       CPU exception stubs.
//
// FIXED: Previously idt.c and interrupts.c both declared separate IDT arrays.
// Only one can be loaded into the CPU. This file is now the one true IDT.
// idt.c is no longer needed — remove it from CMakeLists.txt if you want,
// or leave it; its init_idt() is simply not called.
// ==============================================================================

#include "interrupts.h"
#include "idt.h"
#include "io.h"
#include "string.h"
#include "pit.h"

// ---------------------------------------------------------------------------
// THE IDT — one copy, used by everyone
// keyboard_wire.c externs this to install IRQ1
// ---------------------------------------------------------------------------
idt_entry_t idt[256];
static idt_ptr_t idt_ptr;

// ---------------------------------------------------------------------------
// Registered C-level handlers (called from stubs below)
// ---------------------------------------------------------------------------
static interrupt_handler_t handlers[256];

// ---------------------------------------------------------------------------
// Assembly stubs — defined at bottom of this file in inline asm blocks,
// or you can keep them in a .S file. We use a macro to generate them.
// ---------------------------------------------------------------------------
extern void load_idt(idt_ptr_t *ptr);       // boot64.S already has this
extern void enable_interrupts(void);
extern void disable_interrupts(void);

// ---------------------------------------------------------------------------
// PIC port definitions
// ---------------------------------------------------------------------------
#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1
#define PIC_EOI     0x20

// ---------------------------------------------------------------------------
// Remap PIC so IRQ0-7 -> IDT 0x20-0x27, IRQ8-15 -> IDT 0x28-0x2F
// ---------------------------------------------------------------------------
static void pic_remap(void) {
    // Save masks
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    // ICW1 — start init sequence
    outb(PIC1_CMD,  0x11);
    outb(PIC2_CMD,  0x11);

    // ICW2 — vector offsets
    outb(PIC1_DATA, 0x20);   // IRQ0-7  -> INT 0x20-0x27
    outb(PIC2_DATA, 0x28);   // IRQ8-15 -> INT 0x28-0x2F

    // ICW3 — cascade
    outb(PIC1_DATA, 0x04);   // PIC1: slave on IRQ2
    outb(PIC2_DATA, 0x02);   // PIC2: cascade identity 2

    // ICW4 — 8086 mode
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    // Restore masks
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

// ---------------------------------------------------------------------------
// idt_set_gate — install one entry
// ---------------------------------------------------------------------------
void idt_set_gate(uint8_t num, uint64_t handler,
                          uint16_t sel, uint8_t flags) {
    idt[num].offset_low  =  handler        & 0xFFFF;
    idt[num].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].selector    = sel;
    idt[num].ist         = 0;
    idt[num].type_attr   = flags;
    idt[num].zero        = 0;
}

// ---------------------------------------------------------------------------
// Generic exception handler (C side)
// ---------------------------------------------------------------------------
void int_handler_exception(interrupt_frame_t *frame) {
    serial_puts("\n[KERNEL PANIC] CPU Exception #");
    // Print interrupt number
    {
        uint64_t n = frame->interrupt_number;
        char buf[4];
        buf[0] = '0' + (n / 10);
        buf[1] = '0' + (n % 10);
        buf[2] = '\n';
        buf[3] = '\0';
        serial_puts(buf);
    }
    serial_puts("System halted.\n");
    while (1) { asm volatile("cli; hlt"); }
}

// ---------------------------------------------------------------------------
// IRQ generic handler (C side)
// Calls registered handler, then sends EOI
// ---------------------------------------------------------------------------
void int_handler_irq(interrupt_frame_t *frame) {
    uint8_t irq = (uint8_t)(frame->interrupt_number - 0x20);

    if (handlers[frame->interrupt_number])
        handlers[frame->interrupt_number](frame);

    // Send EOI
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

// ---------------------------------------------------------------------------
// PIT IRQ0 C handler — increments tick and calls scheduler
// ---------------------------------------------------------------------------
static void pit_irq_handler(interrupt_frame_t *frame) {
    (void)frame;
    pit_handler();          // increments system tick counter in pit.c
    scheduler_yield();      // cooperative round-robin on every tick
}

// ---------------------------------------------------------------------------
// Assembly ISR stubs
// We generate one stub per vector using a macro pattern.
// Each stub pushes a fake error code (0) and the vector number,
// then jumps to the common dispatch.
//
// For vectors 8, 10-14, 17, 21 the CPU pushes a real error code —
// those stubs DON'T push a fake one.
// ---------------------------------------------------------------------------

// Common handler called by all stubs
// Stack layout on entry (top -> bottom):
//   vector, error_code, rip, cs, rflags, rsp, ss
void isr_common_handler(uint64_t vector, uint64_t error_code,
                         uint64_t rip,   uint64_t cs,
                         uint64_t rflags, uint64_t rsp, uint64_t ss) {
    interrupt_frame_t frame;
    frame.interrupt_number = vector;
    frame.error_code       = error_code;
    frame.rip              = rip;
    frame.cs               = cs;
    frame.rflags           = rflags;
    frame.rsp              = rsp;
    frame.ss               = ss;
    // General purpose registers not captured here for brevity
    frame.rax = frame.rbx = frame.rcx = frame.rdx = 0;
    frame.rsi = frame.rdi = frame.rbp = 0;
    frame.r8  = frame.r9  = frame.r10 = frame.r11 = 0;
    frame.r12 = frame.r13 = frame.r14 = frame.r15 = 0;

    if (vector < 32) {
        int_handler_exception(&frame);
    } else if (vector < 48) {
        int_handler_irq(&frame);
    } else if (handlers[vector]) {
        handlers[vector](&frame);
    }
}

// ---------------------------------------------------------------------------
// We need actual assembly stubs that the CPU can jump to.
// The cleanest approach for a bare-metal kernel is a small .S file,
// but since we want everything in one place here we use a trampoline.
//
// For 1.0 we use a simplified approach: a single generic stub that
// reads the vector from a per-entry thunk. We implement this with
// a table of 256 tiny stubs, each exactly 16 bytes, generated at
// init time in writable memory.
//
// Each stub looks like:
//   push $0          ; fake error code (skipped for exceptions that push real one)
//   push $N          ; vector number
//   jmp isr_trampoline
// ---------------------------------------------------------------------------

// The trampoline — all stubs jump here
static void isr_trampoline(void) __attribute__((used));
static void __attribute__((naked)) isr_trampoline(void) {
    asm volatile(
        // At entry: stack has [vector, error_code, rip, cs, rflags, rsp, ss]
        // We need to save GPRs and call isr_common_handler
        "push %%rax\n"
        "push %%rbx\n"
        "push %%rcx\n"
        "push %%rdx\n"
        "push %%rsi\n"
        "push %%rdi\n"
        "push %%rbp\n"
        "push %%r8\n"
        "push %%r9\n"
        "push %%r10\n"
        "push %%r11\n"
        "push %%r12\n"
        "push %%r13\n"
        "push %%r14\n"
        "push %%r15\n"
        // Stack now: r15..rax, vector, error, rip, cs, rflags, rsp, ss
        // Set up args for isr_common_handler(vector, error, rip, cs, rflags, rsp, ss)
        // They are at offsets above the saved regs (15 * 8 = 120 bytes)
        "mov 120(%%rsp), %%rdi\n"   // vector
        "mov 128(%%rsp), %%rsi\n"   // error_code
        "mov 136(%%rsp), %%rdx\n"   // rip
        "mov 144(%%rsp), %%rcx\n"   // cs
        "mov 152(%%rsp), %%r8\n"    // rflags
        "mov 160(%%rsp), %%r9\n"    // rsp
        // ss would be 7th arg — on stack, ignore for now
        "sub $8, %%rsp\n"           // align stack
        "call isr_common_handler\n"
        "add $8, %%rsp\n"
        "pop %%r15\n"
        "pop %%r14\n"
        "pop %%r13\n"
        "pop %%r12\n"
        "pop %%r11\n"
        "pop %%r10\n"
        "pop %%r9\n"
        "pop %%r8\n"
        "pop %%rbp\n"
        "pop %%rdi\n"
        "pop %%rsi\n"
        "pop %%rdx\n"
        "pop %%rcx\n"
        "pop %%rbx\n"
        "pop %%rax\n"
        "add $16, %%rsp\n"          // pop vector + error_code
        "iretq\n"
        :::
    );
}

// Stub storage — 256 stubs * 16 bytes each = 4096 bytes (one page)
// Must be executable — place in BSS and we mark it exec at init
static uint8_t stub_table[256 * 16] __attribute__((aligned(4096)));

static void build_stubs(void) {
    // Vectors that push a real error code (CPU does it for us)
    static const uint8_t has_error_code[] = {
        8, 10, 11, 12, 13, 14, 17, 21, 29, 30
    };

    for (int v = 0; v < 256; v++) {
        uint8_t *s = &stub_table[v * 16];
        int has_ec = 0;

        for (int i = 0; i < (int)(sizeof(has_error_code)); i++) {
            if (has_error_code[i] == v) { has_ec = 1; break; }
        }

        int off = 0;

        if (!has_ec) {
            // push $0  (fake error code)   [2 bytes: 6A 00]
            s[off++] = 0x6A; s[off++] = 0x00;
        }

        // push $v  (vector number)
        if (v <= 127) {
            // push imm8  [2 bytes: 6A <v>]
            s[off++] = 0x6A; s[off++] = (uint8_t)v;
        } else {
            // push imm32  [5 bytes: 68 <v> 00 00 00]
            s[off++] = 0x68;
            s[off++] = (uint8_t)(v & 0xFF);
            s[off++] = 0x00; s[off++] = 0x00; s[off++] = 0x00;
        }

        // jmp rel32  [5 bytes: E9 <rel32>]
        uint8_t *jmp_site  = s + off;
        uint8_t *jmp_dest  = (uint8_t *)isr_trampoline;
        int32_t  rel       = (int32_t)(jmp_dest - (jmp_site + 5));
        s[off++] = 0xE9;
        s[off++] = (uint8_t)( rel        & 0xFF);
        s[off++] = (uint8_t)((rel >>  8) & 0xFF);
        s[off++] = (uint8_t)((rel >> 16) & 0xFF);
        s[off++] = (uint8_t)((rel >> 24) & 0xFF);

        // Fill remainder with INT3 (0xCC) so a bug is obvious
        while (off < 16) s[off++] = 0xCC;
    }
}

// ---------------------------------------------------------------------------
// interrupts_init — the one function kernel_init() should call
// ---------------------------------------------------------------------------
void interrupts_init(void) {
    memset(handlers, 0, sizeof(handlers));
    memset(idt,      0, sizeof(idt));

    // Build the per-vector assembly stubs
    build_stubs();

    // Install all 256 stubs into the IDT
    // 0x8E = Present | Ring0 | 64-bit interrupt gate
    for (int i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i,
                     (uint64_t)(uintptr_t)&stub_table[i * 16],
                     0x08,
                     0x8E);
    }

    // Remap PIC — MUST happen before loading IDT or spurious IRQs
    // will hit CPU exception vectors 0x08-0x0F (double fault etc.)
    pic_remap();

    // Mask all IRQs except IRQ0 (PIT) and IRQ1 (keyboard)
    // IRQ0 = PIT timer, IRQ1 = PS/2 keyboard
    // All others masked for now — unmask as you add drivers
    outb(PIC1_DATA, 0xFC);   // 1111 1100 — unmask IRQ0 and IRQ1
    outb(PIC2_DATA, 0xFF);   // all masked on PIC2

    // Wire IRQ0 (PIT) -> our C handler
    interrupts_register_handler(0x20, pit_irq_handler);

    // Load IDT
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)(uintptr_t)idt;
    load_idt(&idt_ptr);

    serial_puts("[IDT] Interrupt descriptor table loaded. PIC remapped.\n");
    serial_puts("[IDT] IRQ0 (PIT) and IRQ1 (keyboard) unmasked.\n");
}

// ---------------------------------------------------------------------------
// interrupts_register_handler
// ---------------------------------------------------------------------------
void interrupts_register_handler(uint32_t num, interrupt_handler_t handler) {
    if (num < 256) handlers[num] = handler;
}

// ---------------------------------------------------------------------------
// interrupts_enable / interrupts_disable
// ---------------------------------------------------------------------------
void interrupts_enable(void) {
    asm volatile("sti");
}

void interrupts_disable(void) {
    asm volatile("cli");
}

// ---------------------------------------------------------------------------
// idt_init — kept for compatibility with any code that calls it
// Just calls interrupts_init()
// ---------------------------------------------------------------------------
void idt_init(void) {
    interrupts_init();
}