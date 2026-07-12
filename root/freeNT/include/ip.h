#ifndef _IP_H
#define _IP_H

#include "types.h"
#include "net.h"

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

typedef struct __attribute__((packed)) {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_length;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ip_header_t;

uint16_t ip_checksum(const void *data, uint32_t len);
int ip_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *payload, uint16_t payload_len);
void ip_handle_packet(const uint8_t *packet, uint16_t len, const uint8_t src_mac[ETH_ALEN]);

#endif /* _IP_H */
