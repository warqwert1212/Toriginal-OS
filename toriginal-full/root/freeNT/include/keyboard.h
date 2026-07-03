#ifndef _KEYBOARD_H
#define _KEYBOARD_H
 
// ==============================================================================
// KEYBOARD.H - PS/2 Keyboard Driver Public Interface
// ==============================================================================
 
#include <stdint.h>

/* Special key codes (returned by keyboard_getc/_nb for non-character keys).
 * Values > 0x7F so they never collide with printable ASCII. */
#define KEY_LEFT   0x81
#define KEY_RIGHT  0x82
#define KEY_UP     0x83
#define KEY_DOWN   0x84
#define KEY_HOME   0x85
#define KEY_END    0x86
#define KEY_DEL    0x87
 
// ---------------------------------------------------------------------------
// Initialization — call this from kernel_init() after IDT is loaded
// Registers IRQ1 handler internally via PIC
// ---------------------------------------------------------------------------
void keyboard_init(void);
 
// ---------------------------------------------------------------------------
// IRQ1 Handler — wire this into your IDT at vector 0x21
// Call it from your interrupt dispatch in interrupts.c
// ---------------------------------------------------------------------------
void keyboard_irq_handler(void);
 
// ---------------------------------------------------------------------------
// Input API
// ---------------------------------------------------------------------------
 
// Blocking read — spins with HLT until a key is pressed
// Drop-in replacement for serial_getc() in your shell
char keyboard_getc(void);
 
// Non-blocking read — returns 0 immediately if nothing available
char keyboard_getc_nb(void);
 
// Check if any key is waiting in the buffer
int  keyboard_has_input(void);
 
// Read a full line with backspace support (blocking, no echo)
// buf must be at least max_len bytes
void keyboard_readline(char* buf, int max_len);
 
// ---------------------------------------------------------------------------
// Modifier State — useful for future GUI / hotkey handling
// ---------------------------------------------------------------------------
int keyboard_shift_held(void);
int keyboard_ctrl_held(void);
int keyboard_alt_held(void);
int keyboard_caps_active(void);
int keyboard_num_active(void);
 
#endif /* _KEYBOARD_H */
