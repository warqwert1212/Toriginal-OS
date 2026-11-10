#pragma once

#include <stdint.h>
#include <stddef.h>

void io_put_string(const char *s);
void io_put_char(char c);
void io_clear_screen(void);
void io_capture_begin(char *buf, size_t max);
size_t io_capture_end(void);

// VGA output helper
void io_put_hex(uint64_t v);
