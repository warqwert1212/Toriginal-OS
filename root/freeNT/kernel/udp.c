#include "udp.h"
#include "ip.h"
#include "net.h"
#include "string.h"

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

#define UDP_MAX_HANDLERS 8
static struct {
    int valid;
    uint16_t port;
    udp_handler_t handler;
} g_handlers[UDP_MAX_HANDLERS];

void udp_register_handler(uint16_t local_port, udp_handler_t handler)
{
    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (!g_handlers[i].valid) {
            g_handlers[i].valid = 1;
            g_handlers[i].port = local_port;
            g_handlers[i].handler = handler;
            return;
        }
    }
}

void udp_unregister_handler(uint16_t local_port)
{
    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (g_handlers[i].valid && g_handlers[i].port == local_port) g_handlers[i].valid = 0;
    }
}

int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
             const uint8_t *payload, uint16_t payload_len)
{
    uint8_t buf[ETH_MTU];
    if ((uint32_t)payload_len + sizeof(udp_header_t) > ETH_MTU) return -1;

    udp_header_t *udp = (udp_header_t *)buf;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons((uint16_t)(sizeof(udp_header_t) + payload_len));
    udp->checksum = 0; /* optional in IPv4; left as 0 (bare-bones, honest about it) */

    memcpy(buf + sizeof(udp_header_t), payload, payload_len);
    return ip_send(dst_ip, IP_PROTO_UDP, buf, (uint16_t)(sizeof(udp_header_t) + payload_len));
}

void udp_handle_packet(uint32_t src_ip, const uint8_t *packet, uint16_t len)
{
    if (len < sizeof(udp_header_t)) return;
    const udp_header_t *udp = (const udp_header_t *)packet;
    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t total_len = ntohs(udp->length);
    if (total_len > len) total_len = len;
    uint16_t data_len = (uint16_t)(total_len - sizeof(udp_header_t));
    const uint8_t *data = packet + sizeof(udp_header_t);

    for (int i = 0; i < UDP_MAX_HANDLERS; i++) {
        if (g_handlers[i].valid && g_handlers[i].port == dst_port) {
            g_handlers[i].handler(src_ip, src_port, data, data_len);
            return;
        }
    }
}
