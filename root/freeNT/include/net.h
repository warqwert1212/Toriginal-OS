#ifndef _NET_H
#define _NET_H

#include "types.h"

#define ETH_ALEN        6
#define ETH_MTU         1500
#define ETH_FRAME_MAX   1514

#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IPV4 0x0800

typedef struct net_device {
    char     name[16];
    uint8_t  mac[ETH_ALEN];
    uint32_t ip;               /* host byte order, 0 = unconfigured */
    uint32_t gateway_ip;
    uint32_t netmask;
    uint32_t dns_ip;           /* configured DNS server, host byte order */

    int  (*send)(struct net_device *dev, const uint8_t *frame, uint16_t len);
    void *driver_data;
} net_device_t;

typedef struct __attribute__((packed)) {
    uint8_t  dest_mac[ETH_ALEN];
    uint8_t  src_mac[ETH_ALEN];
    uint16_t ethertype;
} eth_header_t;

static inline uint16_t htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }
static inline uint32_t htonl(uint32_t v) {
    return ((v & 0xFF)       << 24) | ((v & 0xFF00)     << 8) |
           ((v & 0xFF0000)   >> 8)  | ((v & 0xFF000000) >> 24);
}
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }

void net_init(void);
void net_register_device(net_device_t *dev);
net_device_t *net_get_device(void);

int net_send_raw(const uint8_t *frame, uint16_t len);
int net_send_frame(const uint8_t dest_mac[ETH_ALEN], uint16_t ethertype,
                    const uint8_t *payload, uint16_t payload_len);
void net_receive(const uint8_t *frame, uint16_t len);

void mac_to_string(const uint8_t mac[ETH_ALEN], char *out);
void ip_to_string(uint32_t ip_host_order, char *out);
/* Parses "a.b.c.d" -> host-order uint32. Returns 0 on failure. */
int ip_parse(const char *s, uint32_t *out);

#endif /* _NET_H */
