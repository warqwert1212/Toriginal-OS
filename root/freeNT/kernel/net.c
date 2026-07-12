#include "net.h"
#include "arp.h"
#include "ip.h"
#include "mm.h"
#include "string.h"
#include "serial.h"

static net_device_t *g_dev = NULL;

void net_init(void)
{
    g_dev = NULL;
    arp_init();
    serial_puts("[NET] core init done (waiting for a driver to register)\n");
}

void net_register_device(net_device_t *dev)
{
    if (g_dev) {
        serial_puts("[NET] a device is already registered — ignoring "); serial_puts(dev->name); serial_puts("\n");
        return;
    }
    g_dev = dev;
    serial_puts("[NET] registered device: "); serial_puts(dev->name); serial_puts("\n");
}

net_device_t *net_get_device(void) { return g_dev; }

int net_send_raw(const uint8_t *frame, uint16_t len)
{
    if (!g_dev) return -1;
    return g_dev->send(g_dev, frame, len);
}

int net_send_frame(const uint8_t dest_mac[ETH_ALEN], uint16_t ethertype,
                    const uint8_t *payload, uint16_t payload_len)
{
    if (!g_dev) return -1;
    if ((uint32_t)payload_len + sizeof(eth_header_t) > ETH_FRAME_MAX) return -1;

    uint8_t buf[ETH_FRAME_MAX];
    eth_header_t *eth = (eth_header_t *)buf;
    memcpy(eth->dest_mac, dest_mac, ETH_ALEN);
    memcpy(eth->src_mac, g_dev->mac, ETH_ALEN);
    eth->ethertype = htons(ethertype);
    memcpy(buf + sizeof(eth_header_t), payload, payload_len);

    uint16_t total = (uint16_t)(sizeof(eth_header_t) + payload_len);
    if (total < 60) { memset(buf + total, 0, 60 - total); total = 60; }
    return net_send_raw(buf, total);
}

void net_receive(const uint8_t *frame, uint16_t len)
{
    if (len < sizeof(eth_header_t)) return;
    const eth_header_t *eth = (const eth_header_t *)frame;
    uint16_t ethertype = ntohs(eth->ethertype);
    const uint8_t *payload = frame + sizeof(eth_header_t);
    uint16_t plen = (uint16_t)(len - sizeof(eth_header_t));

    if (ethertype == ETHERTYPE_ARP) {
        arp_handle_packet(payload, plen);
    } else if (ethertype == ETHERTYPE_IPV4) {
        ip_handle_packet(payload, plen, eth->src_mac);
    }
}

static char hex_digit(uint8_t v) { return (char)(v < 10 ? '0' + v : 'a' + (v - 10)); }

void mac_to_string(const uint8_t mac[ETH_ALEN], char *out)
{
    int o = 0;
    for (int i = 0; i < ETH_ALEN; i++) {
        out[o++] = hex_digit((uint8_t)(mac[i] >> 4));
        out[o++] = hex_digit((uint8_t)(mac[i] & 0xF));
        if (i != ETH_ALEN - 1) out[o++] = ':';
    }
    out[o] = '\0';
}

static int print_u8(uint8_t v, char *out)
{
    char tmp[4]; int n = 0;
    if (v == 0) { out[0] = '0'; return 1; }
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v = (uint8_t)(v / 10); }
    int o = 0;
    while (n) out[o++] = tmp[--n];
    return o;
}

void ip_to_string(uint32_t ip_host_order, char *out)
{
    int o = 0;
    for (int i = 3; i >= 0; i--) {
        uint8_t octet = (uint8_t)((ip_host_order >> (i * 8)) & 0xFF);
        o += print_u8(octet, out + o);
        if (i != 0) out[o++] = '.';
    }
    out[o] = '\0';
}

int ip_parse(const char *s, uint32_t *out)
{
    uint32_t octets[4] = {0,0,0,0};
    int oi = 0, has_digit = 0;
    uint32_t cur = 0;
    for (const char *p = s; ; p++) {
        if (*p >= '0' && *p <= '9') { cur = cur * 10 + (uint32_t)(*p - '0'); has_digit = 1; }
        else if (*p == '.' || *p == '\0') {
            if (!has_digit || oi >= 4 || cur > 255) return 0;
            octets[oi++] = cur;
            cur = 0; has_digit = 0;
            if (*p == '\0') break;
        } else return 0;
    }
    if (oi != 4) return 0;
    *out = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
    return 1;
}
