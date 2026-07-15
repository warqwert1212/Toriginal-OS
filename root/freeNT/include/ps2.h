#ifndef PS2_H
#define PS2_H

#include <stdint.h>

#define PS2_PORT_KEYBOARD 1
#define PS2_PORT_MOUSE    2

void ps2_controller_init(void);

void ps2_enable_port(int port);
void ps2_disable_port(int port);

void ps2_set_port_irq_enabled(int port, int enabled);

void ps2_send_to_device(int port, uint8_t cmd);

uint8_t ps2_read_data(void);

int ps2_read_data_nb(uint8_t *out_byte);

int ps2_status_output_is_mouse(void);

void ps2_flush_output(void);

#endif

