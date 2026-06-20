#include <stdint.h>
#include <stddef.h>

/* Suppress unused-parameter warnings */
#define UNUSED(x) (void)(x)

/* ── I/O layer (delegates to serial.c) ───────────────────────────────────── */

extern void serial_puts(const char *str);
extern void serial_putc(char c);

void io_put_string(const char *s)  { serial_puts(s); }
void io_put_char(char c)           { serial_putc(c); }
void io_clear_screen(void)         { /* no-op: VGA layer handles this */ }
void io_put_hex(uint64_t v)        { UNUSED(v); }


extern void keyboard_handler(void);

/* ── load_idt weak fallback (kernel/boot/interrupts.s provides the real
 * lidt instruction). This weak definition only activates if that object
 * is somehow not linked in; in the normal Makefile build the strong
 * symbol from interrupts.s always wins. ─────────────────────────────── */

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_stub_t;

__attribute__((weak))
void load_idt(idt_ptr_stub_t *ptr) { UNUSED(ptr); }

/* ── Shell binary symbols ──────────────────────────────────────────────────
 * These were used by the old CMake build's objcopy-embedded-shell trick
 * (compile shell.c standalone, objcopy it to a binary blob, link the blob
 * in as a byte array). The flat Makefile build used by this tree instead
 * compiles kernel/shell.c directly alongside the rest of the kernel, so
 * these symbols are no longer referenced by anything — kept as harmless
 * weak placeholders in case something still expects them to exist. ----- */

__attribute__((weak)) unsigned char _binary_toriginal_shell_bin_start[1] = { 0 };
__attribute__((weak)) unsigned char _binary_toriginal_shell_bin_end[1]   = { 0 };
/* size symbol is an address-difference, not a variable; provide a 1-byte
 * weak placeholder with value 1 to avoid divide-by-zero if code reads it. */
__attribute__((weak)) unsigned char _binary_toriginal_shell_bin_size      = 1;
/* Forwarder: interrupts.c calls pit_handler(); real logic lives in pit.c */
void pit_handler(void);
void pit_tick(void);
void pit_handler(void) {
    pit_tick();
}
