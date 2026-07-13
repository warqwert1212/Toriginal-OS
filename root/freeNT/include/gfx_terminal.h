
#ifndef GFX_TERMINAL_H
#define GFX_TERMINAL_H

#include "graphics_core.h"


#define GTERM_FONT_SCALE   2
#define GTERM_CELL_W       (8  * GTERM_FONT_SCALE)   
#define GTERM_CELL_H       (16 * GTERM_FONT_SCALE) 


int gterm_init(void);


int gterm_is_active(void);


void gterm_putc(char c);
void gterm_write(const char *str);
void gterm_clear(void);
void gterm_set_color(uint8_t fg_index, uint8_t bg_index);
void gterm_set_cursor(uint16_t row, uint16_t col);


void gterm_tick(void);


void gterm_request_tick(void);


int gterm_poll_tick(void);


int gterm_get_selection(char *out, uint32_t out_capacity);
uint32_t gterm_selection_length(void);
void gterm_clear_selection(void);

#endif /* GFX_TERMINAL_H */