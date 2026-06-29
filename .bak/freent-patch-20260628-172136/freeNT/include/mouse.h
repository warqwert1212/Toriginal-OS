#ifndef _MOUSE_H
#define _MOUSE_H

/* ==============================================================================
 * MOUSE.H — PS/2 Mouse Driver Public Interface
 *
 * Mirrors keybord.h's shape on purpose: same init/IRQ-handler/state-query
 * pattern, so kernel.c wires this up the same way it wires up the keyboard.
 *
 * Hardware notes (so future-you isn't confused reading mouse.c):
 *   - The PS/2 mouse shares the 8042 controller with the keyboard but
 *     lives on the SECOND port, wired to IRQ12 (not IRQ1).
 *   - After PIC remap (interrups.c remaps PIC1 to base 0x20), IRQ12 is
 *     IDT vector 0x20 + 12 = 0x2C.
 *   - The mouse streams 3-byte packets once "data reporting" is enabled:
 *       byte0: bit0=left, bit1=right, bit2=middle, bit3=always 1,
 *              bit4=X sign, bit5=Y sign, bit6=X overflow, bit7=Y overflow
 *       byte1: X movement magnitude (0-255)
 *       byte2: Y movement magnitude (0-255), and PS/2 Y is inverted
 *              relative to screen Y (positive = up), so the driver
 *              negates it before exposing dy to callers.
 * ============================================================================== */

#include <stdint.h>

/* Call from kernel_init(), AFTER idt_init()/interrupts_init() and AFTER
 * keyboard_wire_idt() (both touch the same 8042 controller — order
 * matters less than "controller is sane first", but matching keyboard's
 * init order is the safest bet). Registers IRQ12 internally. */
void mouse_init(void);

/* IRQ12 handler — wire this into the IDT at vector 0x2C, same pattern as
 * keyboard_isr_stub -> keyboard_irq_handler. */
void mouse_irq_handler(void);

/* ── Polled state — the WM/compositor reads this once per frame ────────── */

typedef struct {
    int32_t  x, y;          /* absolute position, clamped to screen bounds */
    int32_t  dx, dy;        /* delta since last mouse_get_state() call     */
    int      left_button;
    int      right_button;
    int      middle_button;
} mouse_state_t;

/* Tell the driver the screen size so absolute x/y stays clamped on-screen.
 * Call once after the framebuffer/mode is known (mirrors how VGA/graphics
 * init happens before input is meaningful). */
void mouse_set_bounds(int32_t screen_w, int32_t screen_h);

/* Copies current state into *out and clears the internal dx/dy accumulator
 * (so the next call reports fresh deltas, not a running total). */
void mouse_get_state(mouse_state_t *out);

#endif /* _MOUSE_H */
