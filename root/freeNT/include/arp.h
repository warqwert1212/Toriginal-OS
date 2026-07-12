#ifndef _ARP_H
#define _ARP_H

#include "types.h"
#include "net.h"

void arp_init(void);
void arp_handle_packet(const uint8_t *packet, uint16_t len);
void arp_send_request(uint32_t target_ip);
int  arp_lookup(uint32_t target_ip, uint8_t mac_out[ETH_ALEN]);
int  arp_resolve(uint32_t target_ip, uint8_t mac_out[ETH_ALEN], uint32_t timeout_ms);

#endif /* _ARP_H */
