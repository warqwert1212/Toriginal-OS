#ifndef _UDP_H
#define _UDP_H

#include "types.h"

int  udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
              const uint8_t *payload, uint16_t payload_len);
void udp_handle_packet(uint32_t src_ip, const uint8_t *packet, uint16_t len);

/* Register a handler for a local UDP port (e.g. DNS uses an ephemeral
 * source port and needs to see the reply). Bare-bones: one handler per
 * port, no real socket table. */
typedef void (*udp_handler_t)(uint32_t src_ip, uint16_t src_port, const uint8_t *data, uint16_t len);
void udp_register_handler(uint16_t local_port, udp_handler_t handler);
void udp_unregister_handler(uint16_t local_port);

#endif /* _UDP_H */
