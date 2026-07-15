#ifndef UHCI_H
#define UHCI_H

#include <stdint.h>

int uhci_available(void);

void uhci_init(void);

int uhci_control_transfer(uint8_t device_addr, uint8_t max_packet,
                           int low_speed,
                           const uint8_t setup[8],
                           void *data, uint16_t data_len);

int uhci_setup_interrupt_endpoint(uint8_t device_addr, uint8_t endpoint,
                                   uint8_t max_packet, int low_speed,
                                   uint8_t interval_ms);

int uhci_poll_interrupt_endpoint(int handle, uint8_t *buf, uint8_t buf_len);
void uhci_service_interrupt_endpoints(void);

#endif

