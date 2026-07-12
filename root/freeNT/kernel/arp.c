#include "arp.h"
#include "string.h"
#include "serial.h"
#include "pit.h"

#define ARP_HTYPE_ETHERNET 1
#define ARP_PTYPE_IPV4      0x0800
#define ARP_OP_REQUEST      1
#define ARP_OP_REPLY        2

typedef struct __attribute__((packed)) {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[ETH_ALEN];
    uint32_t spa;
    uint8_t  tha[ETH_ALEN];
    uint32_t tpa;
} arp_packet_t;

#define ARP_CACHE_SIZE 32

typedef struct {
    uint32_t ip;
    uint8_t  mac[ETH_ALEN];
    int      valid;
} arp_entry_t;

static arp_entry_t g_cache[ARP_CACHE_SIZE];

void arp_init(void)
{
    memset(g_cache, 0, sizeof(g_cache));
    serial_puts("[ARP] cache ready\n");
}

static void cache_put(uint32_t ip, const uint8_t mac[ETH_ALEN])
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_cache[i].valid && g_cache[i].ip == ip) { memcpy(g_cache[i].mac, mac, ETH_ALEN); return; }
    }
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_cache[i].valid) {
            g_cache[i].ip = ip; memcpy(g_cache[i].mac, mac, ETH_ALEN); g_cache[i].valid = 1;
            return;
        }
    }
    g_cache[0].ip = ip; memcpy(g_cache[0].mac, mac, ETH_ALEN); g_cache[0].valid = 1;
}

int arp_lookup(uint32_t target_ip, uint8_t mac_out[ETH_ALEN])
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_cache[i].valid && g_cache[i].ip == target_ip) { memcpy(mac_out, g_cache[i].mac, ETH_ALEN); return 1; }
    }
    return 0;
}

void arp_send_request(uint32_t target_ip)
{
    net_device_t *dev = net_get_device();
    if (!dev) return;

    arp_packet_t pkt;
    pkt.htype = htons(ARP_HTYPE_ETHERNET);
    pkt.ptype = htons(ARP_PTYPE_IPV4);
    pkt.hlen = ETH_ALEN; pkt.plen = 4;
    pkt.oper = htons(ARP_OP_REQUEST);
    memcpy(pkt.sha, dev->mac, ETH_ALEN);
    pkt.spa = htonl(dev->ip);
    memset(pkt.tha, 0, ETH_ALEN);
    pkt.tpa = htonl(target_ip);

    static const uint8_t broadcast[ETH_ALEN] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    net_send_frame(broadcast, ETHERTYPE_ARP, (const uint8_t *)&pkt, sizeof(pkt));
}

void arp_handle_packet(const uint8_t *packet, uint16_t len)
{
    if (len < sizeof(arp_packet_t)) return;
    const arp_packet_t *pkt = (const arp_packet_t *)packet;

    uint16_t oper = ntohs(pkt->oper);
    uint32_t sender_ip = ntohl(pkt->spa);
    cache_put(sender_ip, pkt->sha);

    net_device_t *dev = net_get_device();
    if (!dev) return;

    if (oper == ARP_OP_REQUEST && ntohl(pkt->tpa) == dev->ip) {
        arp_packet_t reply;
        reply.htype = htons(ARP_HTYPE_ETHERNET);
        reply.ptype = htons(ARP_PTYPE_IPV4);
        reply.hlen = ETH_ALEN; reply.plen = 4;
        reply.oper = htons(ARP_OP_REPLY);
        memcpy(reply.sha, dev->mac, ETH_ALEN);
        reply.spa = htonl(dev->ip);
        memcpy(reply.tha, pkt->sha, ETH_ALEN);
        reply.tpa = pkt->spa;
        net_send_frame(pkt->sha, ETHERTYPE_ARP, (const uint8_t *)&reply, sizeof(reply));
    }
}

int arp_resolve(uint32_t target_ip, uint8_t mac_out[ETH_ALEN], uint32_t timeout_ms)
{
    if (arp_lookup(target_ip, mac_out)) return 1;
    arp_send_request(target_ip);

    uint32_t waited = 0;
    while (waited < timeout_ms) {
        pit_sleep(50);
        waited += 50;
        if (arp_lookup(target_ip, mac_out)) return 1;
    }
    return 0;
}
