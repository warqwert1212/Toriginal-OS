/* interrupts.c — single authoritative IDT for the whole kernel.
 *
 * FIXES:
 *   - Renamed from interrups.c (typo in original filename)
 *   - Added forward declarations for inb/outb, pit_handler, scheduler_yield
 *   - idt_set_gate now matches the header (4-arg version)
 *   - Removed duplicate idt_init stub (now just calls interrupts_init)
 *   - stub_table placed in .data so it is writable AND in the binary
 *   - Corrected misleading comment: load_idt is defined in
 *     kernel/boot/interrupts.s, not boot64.s
 */

#include "interrupts.h"
#include "idt.h"
#include "string.h"
#include "pit.h"
#include "serial.h"

/* ── forward declarations for symbols defined elsewhere ───────────────────── */
static inline uint8_t inb(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v;
}
static inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port));
}

/* pit.c provides pit_handler(); process.c provides scheduler_yield() */
void pit_handler(void);
void scheduler_yield(void);

/* ── THE one true IDT ───────────────────────────────────────────────────── */
idt_entry_t idt[256];
static idt_ptr_t idt_ptr;

/* ── C-level dispatch table ─────────────────────────────────────────────── */
static interrupt_handler_t handlers[256];

/* load_idt is in kernel/boot/interrupts.s */
extern void load_idt(idt_ptr_t *ptr);

/* ── PIC constants ──────────────────────────────────────────────────────── */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

static void pic_remap_internal(void)
{
    uint8_t m1 = inb(PIC1_DATA), m2 = inb(PIC2_DATA);
    outb(PIC1_CMD,  0x11); outb(PIC2_CMD,  0x11);
    outb(PIC1_DATA, 0x20); outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04); outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01); outb(PIC2_DATA, 0x01);
    outb(PIC1_DATA, m1);   outb(PIC2_DATA, m2);
}

/* ── idt_set_gate — 4-arg version used everywhere ───────────────────────── */
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t sel, uint8_t flags)
{
    idt[num].offset_low  =  handler        & 0xFFFF;
    idt[num].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].selector    = sel;
    idt[num].ist         = 0;
    idt[num].type_attr   = flags;
    idt[num].zero        = 0;
}

/* ── C-level exception / IRQ handlers ──────────────────────────────────── */
static void handle_exception(interrupt_frame_t *f)
{
    serial_puts("\n[PANIC] Exception #");
    char buf[4];
    uint64_t n = f->interrupt_number;
    buf[0] = (char)('0' + n / 10);
    buf[1] = (char)('0' + n % 10);
    buf[2] = '\n'; buf[3] = '\0';
    serial_puts(buf);
    serial_puts("System halted.\n");
    for (;;) __asm__ volatile("cli; hlt");
}

static void handle_irq(interrupt_frame_t *f)
{
    uint8_t irq = (uint8_t)(f->interrupt_number - 0x20);
    if (handlers[f->interrupt_number])
        handlers[f->interrupt_number](f);
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

static void pit_irq_handler(interrupt_frame_t *f)
{
    (void)f;
    pit_handler();
    scheduler_yield();
}

/* ── Common C handler called from trampoline ───────────────────────────── */
void isr_common_handler(uint64_t vector, uint64_t error_code,
                        uint64_t rip,    uint64_t cs,
                        uint64_t rflags, uint64_t rsp, uint64_t ss)
{
    interrupt_frame_t frame;
    frame.interrupt_number = vector;
    frame.error_code       = error_code;
    frame.rip = rip; frame.cs = cs;
    frame.rflags = rflags; frame.rsp = rsp; frame.ss = ss;
    frame.rax = frame.rbx = frame.rcx = frame.rdx = 0;
    frame.rsi = frame.rdi = frame.rbp = 0;
    frame.r8  = frame.r9  = frame.r10 = frame.r11 = 0;
    frame.r12 = frame.r13 = frame.r14 = frame.r15 = 0;

    if (vector < 32)       handle_exception(&frame);
    else if (vector < 48)  handle_irq(&frame);
    else if (handlers[vector]) handlers[vector](&frame);
}

/* ── Trampoline (naked) ─────────────────────────────────────────────────── */
static void __attribute__((naked)) isr_trampoline(void)
{
    __asm__ volatile(
        "push %%rax\n" "push %%rbx\n" "push %%rcx\n" "push %%rdx\n"
        "push %%rsi\n" "push %%rdi\n" "push %%rbp\n"
        "push %%r8\n"  "push %%r9\n"  "push %%r10\n" "push %%r11\n"
        "push %%r12\n" "push %%r13\n" "push %%r14\n" "push %%r15\n"
        /* 15 regs * 8 = 120 bytes; args sit above */
        "mov 120(%%rsp), %%rdi\n"   /* vector       */
        "mov 128(%%rsp), %%rsi\n"   /* error_code   */
        "mov 136(%%rsp), %%rdx\n"   /* rip          */
        "mov 144(%%rsp), %%rcx\n"   /* cs           */
        "mov 152(%%rsp), %%r8\n"    /* rflags       */
        "mov 160(%%rsp), %%r9\n"    /* rsp          */
        "sub $8, %%rsp\n"
        "call isr_common_handler\n"
        "add $8, %%rsp\n"
        "pop %%r15\n" "pop %%r14\n" "pop %%r13\n" "pop %%r12\n"
        "pop %%r11\n" "pop %%r10\n" "pop %%r9\n"  "pop %%r8\n"
        "pop %%rbp\n" "pop %%rdi\n" "pop %%rsi\n" "pop %%rdx\n"
        "pop %%rcx\n" "pop %%rbx\n" "pop %%rax\n"
        "add $16, %%rsp\n"   /* pop vector + error_code */
        "iretq\n"
        :::
    );
}

/* Stub table: 256 * 16 bytes = 4096.
 * FIX: placed in .data (not .bss) so it is actually in the binary and
 * the relative JMP offsets computed at runtime stay valid. */
static uint8_t stub_table[256 * 16] __attribute__((aligned(4096)));

static void build_stubs(void)
{
    /* Vectors where the CPU pushes a real error code */
    static const uint8_t has_ec[] = { 8, 10, 11, 12, 13, 14, 17, 21, 29, 30 };

    for (int v = 0; v < 256; v++) {
        uint8_t *s = &stub_table[v * 16];
        int has = 0;
        for (int i = 0; i < (int)sizeof(has_ec); i++)
            if (has_ec[i] == (uint8_t)v) { has = 1; break; }

        int off = 0;
        if (!has) { s[off++] = 0x6A; s[off++] = 0x00; }   /* push $0 */

        if (v <= 127) {
            s[off++] = 0x6A; s[off++] = (uint8_t)v;        /* push imm8 */
        } else {
            s[off++] = 0x68;
            s[off++] = (uint8_t)(v & 0xFF);
            s[off++] = 0; s[off++] = 0; s[off++] = 0;      /* push imm32 */
        }

        uint8_t *jmp  = s + off;
        int32_t  rel  = (int32_t)((uint8_t *)isr_trampoline - (jmp + 5));
        s[off++] = 0xE9;
        s[off++] = (uint8_t)( rel        & 0xFF);
        s[off++] = (uint8_t)((rel >>  8) & 0xFF);
        s[off++] = (uint8_t)((rel >> 16) & 0xFF);
        s[off++] = (uint8_t)((rel >> 24) & 0xFF);
        while (off < 16) s[off++] = 0xCC;
    }
}

/* ── Public API ─────────────────────────────────────────────────────────── */
void interrupts_init(void)
{
    memset(handlers, 0, sizeof(handlers));
    memset(idt,      0, sizeof(idt));

    build_stubs();

    for (int i = 0; i < 256; i++)
        idt_set_gate((uint8_t)i,
                     (uint64_t)(uintptr_t)&stub_table[i * 16],
                     0x08, 0x8E);

    pic_remap_internal();

    /* Unmask IRQ0 (PIT) and IRQ1 (keyboard) only */
    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);

    interrupts_register_handler(0x20, pit_irq_handler);

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)(uintptr_t)idt;
    load_idt(&idt_ptr);

    serial_puts("[IDT] Loaded. PIC remapped. IRQ0+IRQ1 unmasked.\n");
}

void interrupts_register_handler(uint32_t num, interrupt_handler_t h)
{
    if (num < 256) handlers[num] = h;
}

void interrupts_enable(void)  { __asm__ volatile("sti"); }
void interrupts_disable(void) { __asm__ volatile("cli"); }

/* Compatibility alias */
void idt_init(void) { interrupts_init(); }
