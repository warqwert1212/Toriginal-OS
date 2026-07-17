#ifndef _MOUSE_H
#define _MOUSE_H

#include <stdint.h>

void mouse_init(void);

void mouse_irq_handler(void);

void mouse_handle_byte(uint8_t byte);

typedef struct {
    int32_t  x, y;
    int32_t  dx, dy;
    int      left_button;
    int      right_button;
    int      middle_button;
} mouse_state_t;

void mouse_set_bounds(int32_t screen_w, int32_t screen_h);

void mouse_get_state(mouse_state_t *out);

#endif /* _MOUSE_H */