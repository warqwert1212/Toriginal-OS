#include "dns.h"
#include "udp.h"
#include "net.h"
#include "string.h"
#include "serial.h"
#include "pit.h"

#define DNS_PORT 53

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static struct {
    int valid;
    uint32_t ip;
} g_result;

static uint16_t g_query_id = 1;

static void dns_reply_handler(uint32_t src_ip, uint16_t src_port, const uint8_t *data, uint16_t len)
{
    (void)src_ip; (void)src_port;
    if (len < sizeof(dns_header_t)) return;
    const dns_header_t *hdr = (const dns_header_t *)data;
    uint16_t ancount = ntohs(hdr->ancount);
    if (ancount == 0) return;

    /* Skip the question section: read past the QNAME (length-prefixed
     * labels ending in a 0 byte) + QTYPE(2) + QCLASS(2). */
    uint16_t pos = sizeof(dns_header_t);
    while (pos < len && data[pos] != 0) {
        uint8_t label_len = data[pos];
        pos = (uint16_t)(pos + 1 + label_len);
    }
    pos = (uint16_t)(pos + 1 + 4); /* null byte + QTYPE + QCLASS */

    /* Walk the answer records looking for the first A record (type 1). */
    for (uint16_t a = 0; a < ancount && pos < len; a++) {
        /* NAME: either a pointer (0xC0 prefix, 2 bytes) or inline labels. */
        if ((data[pos] & 0xC0) == 0xC0) {
            pos += 2;
        } else {
            while (pos < len && data[pos] != 0) pos = (uint16_t)(pos + 1 + data[pos]);
            pos += 1;
        }
        if (pos + 10 > len) return;
        uint16_t type = ntohs(*(const uint16_t *)(data + pos));
        uint16_t rdlength = ntohs(*(const uint16_t *)(data + pos + 8));
        pos = (uint16_t)(pos + 10);

        if (type == 1 && rdlength == 4 && pos + 4 <= len) { /* A record */
            uint32_t ip = ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos+1] << 16) |
                          ((uint32_t)data[pos+2] << 8) | (uint32_t)data[pos+3];
            g_result.ip = ip;
            g_result.valid = 1;
            return;
        }
        pos = (uint16_t)(pos + rdlength);
    }
}

int dns_resolve(const char *hostname, uint32_t *out_ip, uint32_t timeout_ms)
{
    net_device_t *dev = net_get_device();
    if (!dev || !dev->dns_ip) {
        serial_puts("[DNS] no DNS server configured (see ifconfig)\n");
        return 0;
    }

    /* Already a dotted IP? No lookup needed. */
    if (ip_parse(hostname, out_ip)) return 1;

    uint8_t buf[512];
    dns_header_t *hdr = (dns_header_t *)buf;
    uint16_t qid = g_query_id++;
    hdr->id = htons(qid);
    hdr->flags = htons(0x0100); /* standard query, recursion desired */
    hdr->qdcount = htons(1);
    hdr->ancount = 0; hdr->nscount = 0; hdr->arcount = 0;

    uint16_t pos = sizeof(dns_header_t);
    /* Encode hostname as length-prefixed labels. */
    const char *label_start = hostname;
    while (1) {
        const char *p = label_start;
        while (*p != '.' && *p != '\0') p++;
        uint8_t label_len = (uint8_t)(p - label_start);
        if (label_len == 0 || (size_t)(pos + 1 + label_len) >= sizeof(buf)) { serial_puts("[DNS] bad hostname\n"); return 0; }
        buf[pos++] = label_len;
        memcpy(buf + pos, label_start, label_len);
        pos = (uint16_t)(pos + label_len);
        if (*p == '\0') break;
        label_start = p + 1;
    }
    buf[pos++] = 0; /* terminator */
    *(uint16_t *)(buf + pos) = htons(1); pos += 2; /* QTYPE=A */
    *(uint16_t *)(buf + pos) = htons(1); pos += 2; /* QCLASS=IN */

    g_result.valid = 0;
    uint16_t local_port = (uint16_t)(20000 + (qid % 10000));
    udp_register_handler(local_port, dns_reply_handler);

    if (udp_send(dev->dns_ip, local_port, DNS_PORT, buf, pos) != 0) {
        udp_unregister_handler(local_port);
        serial_puts("[DNS] send failed\n");
        return 0;
    }

    uint32_t waited = 0;
    while (waited < timeout_ms) {
        pit_sleep(50);
        waited += 50;
        if (g_result.valid) {
            udp_unregister_handler(local_port);
            *out_ip = g_result.ip;
            return 1;
        }
    }
    udp_unregister_handler(local_port);
    serial_puts("[DNS] timed out\n");
    return 0;
}
