#pragma once

#include <stdint.h>

void io_put_string(const char *s);
void io_put_char(char c);
void io_clear_screen(void);

// VGA output helper
void io_put_hex(uint64_t v);
