#include <stdint.h>
#include <stddef.h>

/* Suppress unused-parameter warnings */
#define UNUSED(x) (void)(x)



extern void keyboard_handler(void);



typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_stub_t;

__attribute__((weak))
void load_idt(idt_ptr_stub_t *ptr) { UNUSED(ptr); }

__attribute__((weak)) unsigned char _binary_toriginal_shell_bin_start[1] = { 0 };
__attribute__((weak)) unsigned char _binary_toriginal_shell_bin_end[1]   = { 0 };

__attribute__((weak)) unsigned char _binary_toriginal_shell_bin_size      = 1;
/* 15 monsters deap */
void pit_handler(void);
void pit_tick(void);
void pit_handler(void) {
    pit_tick();
}
