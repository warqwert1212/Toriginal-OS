#ifndef _ICMP_H
#define _ICMP_H

#include "types.h"

void icmp_handle_packet(uint32_t src_ip, const uint8_t *packet, uint16_t len);
int icmp_send_echo_request(uint32_t dst_ip, uint16_t id, uint16_t seq);

/* Set by icmp_handle_packet when an echo reply arrives, polled by the
 * ping command — bare-bones but real (not an emulator, a real ICMP
 * round trip over the real stack). */
int icmp_last_reply_matches(uint32_t src_ip, uint16_t id, uint16_t seq);

#endif /* _ICMP_H */
