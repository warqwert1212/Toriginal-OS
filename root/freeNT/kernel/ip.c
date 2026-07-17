#include "ip.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "string.h"
#include "serial.h"

static uint16_t g_next_id = 1;

uint16_t ip_checksum(const void *data, uint32_t len)
{
    const uint16_t *words = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *words++; len -= 2; }
    if (len == 1) sum += *(const uint8_t *)words;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

int ip_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *payload, uint16_t payload_len)
{
    net_device_t *dev = net_get_device();
    if (!dev) return -1;

    uint8_t dest_mac[ETH_ALEN];
    uint32_t next_hop = dst_ip;
    if (dev->netmask && (dst_ip & dev->netmask) != (dev->ip & dev->netmask))
        next_hop = dev->gateway_ip;

    if (!arp_resolve(next_hop, dest_mac, 2000)) {
        serial_puts("[IP] ARP resolve failed — dropping packet\n");
        return -1;
    }

    uint8_t buf[ETH_MTU];
    if ((uint32_t)payload_len + sizeof(ip_header_t) > ETH_MTU) return -1;

    ip_header_t *ip = (ip_header_t *)buf;
    ip->version_ihl = (4 << 4) | 5;
    ip->tos = 0;
    ip->total_length = htons((uint16_t)(sizeof(ip_header_t) + payload_len));
    ip->id = htons(g_next_id++);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src_ip = htonl(dev->ip);
    ip->dst_ip = htonl(dst_ip);
    ip->checksum = ip_checksum(ip, sizeof(ip_header_t));

    memcpy(buf + sizeof(ip_header_t), payload, payload_len);

    return net_send_frame(dest_mac, ETHERTYPE_IPV4, buf, (uint16_t)(sizeof(ip_header_t) + payload_len));
}

void ip_handle_packet(const uint8_t *packet, uint16_t len, const uint8_t src_mac[ETH_ALEN])
{
    (void)src_mac;
    if (len < sizeof(ip_header_t)) return;
    const ip_header_t *ip = (const ip_header_t *)packet;

    uint8_t ihl_words = ip->version_ihl & 0x0F;
    uint16_t header_len = (uint16_t)(ihl_words * 4);
    if (header_len < sizeof(ip_header_t) || header_len > len) return;

    /* Verify the header checksum before trusting anything in it - an
     * attacker/corruption-controlled header shouldn't be acted on. */
    if (ip_checksum(ip, header_len) != 0) return;

    uint16_t total_len = ntohs(ip->total_length);
    if (total_len > len) total_len = len;
    /* FIX: total_len is attacker-controlled and was never checked against
     * header_len before subtracting. A packet claiming total_length <
     * header_len (e.g. 0) underflowed plen to ~65535, which got handed to
     * icmp/udp/tcp handlers as a payload length far larger than any real
     * data behind `payload` - an out-of-bounds read triggerable by any
     * single crafted IP packet. */
    if (total_len < header_len) return;

    const uint8_t *payload = packet + header_len;
    uint16_t plen = (uint16_t)(total_len - header_len);
    uint32_t src_ip = ntohl(ip->src_ip);

    if (ip->protocol == IP_PROTO_ICMP) {
        icmp_handle_packet(src_ip, payload, plen);
    } else if (ip->protocol == IP_PROTO_UDP) {
        udp_handle_packet(src_ip, payload, plen);
    } else if (ip->protocol == IP_PROTO_TCP) {
        tcp_handle_packet(src_ip, payload, plen);
    }
}
/* fuck any fucking kernel this one gets on my nerves*/