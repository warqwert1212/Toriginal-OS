#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>

#define KEY_LEFT   0x81
#define KEY_RIGHT  0x82
#define KEY_UP     0x83
#define KEY_DOWN   0x84
#define KEY_HOME   0x85
#define KEY_END    0x86
#define KEY_DEL    0x87

void keyboard_init(void);

void keyboard_irq_handler(void);

void keyboard_handle_byte(uint8_t scancode);

char keyboard_getc(void);

char keyboard_getc_nb(void);

int  keyboard_has_input(void);

void keyboard_readline(char* buf, int max_len);

int keyboard_shift_held(void);
int keyboard_ctrl_held(void);
int keyboard_alt_held(void);
int keyboard_caps_active(void);
int keyboard_num_active(void);

#endif /* _KEYBOARD_H */