#include "icmp.h"
#include "ip.h"
#include "net.h"
#include "string.h"
#include "serial.h"

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} icmp_header_t;

static struct {
    int valid;
    uint32_t src_ip;
    uint16_t id;
    uint16_t seq;
} g_last_reply;

int icmp_send_echo_request(uint32_t dst_ip, uint16_t id, uint16_t seq)
{
    uint8_t buf[sizeof(icmp_header_t) + 32];
    icmp_header_t *icmp = (icmp_header_t *)buf;
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = htons(id);
    icmp->seq = htons(seq);

    const char *pattern = "toriginal-ping-payload!!";
    memcpy(buf + sizeof(icmp_header_t), pattern, 32);

    icmp->checksum = ip_checksum(buf, sizeof(buf));
    return ip_send(dst_ip, IP_PROTO_ICMP, buf, sizeof(buf));
}

int icmp_last_reply_matches(uint32_t src_ip, uint16_t id, uint16_t seq)
{
    return g_last_reply.valid && g_last_reply.src_ip == src_ip &&
           g_last_reply.id == id && g_last_reply.seq == seq;
}

void icmp_handle_packet(uint32_t src_ip, const uint8_t *packet, uint16_t len)
{
    if (len < sizeof(icmp_header_t)) return;
    const icmp_header_t *icmp = (const icmp_header_t *)packet;

    char ipstr[16];
    ip_to_string(src_ip, ipstr);

    if (icmp->type == ICMP_ECHO_REPLY) {
        serial_puts("[ICMP] echo reply from "); serial_puts(ipstr); serial_puts("\n");
        g_last_reply.valid = 1;
        g_last_reply.src_ip = src_ip;
        g_last_reply.id = ntohs(icmp->id);
        g_last_reply.seq = ntohs(icmp->seq);
        return;
    }

    if (icmp->type == ICMP_ECHO_REQUEST) {
        serial_puts("[ICMP] echo request from "); serial_puts(ipstr); serial_puts(" — replying\n");
        uint8_t buf[ETH_MTU];
        if ((uint32_t)len > sizeof(buf)) return;
        memcpy(buf, packet, len);

        icmp_header_t *reply = (icmp_header_t *)buf;
        reply->type = ICMP_ECHO_REPLY;
        reply->checksum = 0;
        reply->checksum = ip_checksum(buf, len);
        ip_send(src_ip, IP_PROTO_ICMP, buf, len);
    }
}
