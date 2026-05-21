#ifndef NETWORK_DRIVER_H
#define NETWORK_DRIVER_H

#include <stdint.h>

#define MAX_NETWORK_DEVICES 10
#define MAX_PACKET_SIZE 1500

typedef enum {
    NET_STATUS_DISCONNECTED = 0,
    NET_STATUS_CONNECTING = 1,
    NET_STATUS_CONNECTED = 2,
    NET_STATUS_ERROR = 3
} NetworkStatus;

typedef enum {
    NET_DRIVER_ETHERNET = 0,
    NET_DRIVER_WIFI = 1,
    NET_DRIVER_PPP = 2
} NetworkDriverType;

typedef struct {
    uint8_t mac[6];
    uint32_t ip_address;
    uint32_t gateway;
    uint32_t netmask;
    uint32_t dns1;
    uint32_t dns2;
} NetworkConfig;

typedef struct {
    char name[32];
    NetworkDriverType type;
    NetworkStatus status;
    NetworkConfig config;
    uint64_t packets_sent;
    uint64_t packets_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t errors;
} NetworkDevice;

typedef struct {
    uint8_t destination_mac[6];
    uint8_t source_mac[6];
    uint16_t type;
    uint8_t payload[MAX_PACKET_SIZE];
    uint16_t payload_len;
} EthernetFrame;

typedef struct {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment_offset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint32_t source_ip;
    uint32_t dest_ip;
} IPv4Header;

/* Ethernet Driver */
int eth_init();
int eth_enable_device(const char *name);
int eth_disable_device(const char *name);
int eth_send_packet(const char *device, EthernetFrame *frame);
int eth_receive_packet(const char *device, EthernetFrame *frame);

/* WiFi Driver */
int wifi_init();
int wifi_scan(const char *device);
int wifi_connect(const char *device, const char *ssid, const char *password);
int wifi_disconnect(const char *device);
int wifi_get_signal_strength(const char *device);

/* Network Configuration */
int net_configure_ip(const char *device, const char *ip, const char *gateway, const char *netmask);
int net_get_config(const char *device, NetworkConfig *config);
int net_set_dns(const char *device, const char *dns1, const char *dns2);

/* Packet Processing */
int net_process_packet(EthernetFrame *frame);
int net_send_ip_packet(const char *device, uint32_t dest_ip, uint8_t *data, uint16_t len);

/* Network Status */
NetworkStatus net_get_status(const char *device);
void net_print_stats(const char *device);

#endif /* NETWORK_DRIVER_H */
