/* Minimal PIC stubs to satisfy build in QEMU environments.
	Real implementation can go here later. */

#include <stdint.h>

void pic_remap(void) { /* no-op for now */ }

void pic_send_eoi(void) { /* no-op */ }

void pic_mask(uint8_t irq) { (void)irq; }

void pic_unmask(uint8_t irq) { (void)irq; }